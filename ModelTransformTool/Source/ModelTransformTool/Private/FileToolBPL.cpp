#include "FileToolBPL.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

namespace
{
    FString EscapePowerShellSingleQuote(const FString& In)
    {
        FString Out = In;
        Out.ReplaceInline(TEXT("'"), TEXT("''"));
        return Out;
    }
}

bool UFileToolBPL::ClearFolderContents(const FString& FolderPath)
{
    FString FullFolderPath = FPaths::ConvertRelativePathToFull(FolderPath);
    FPaths::NormalizeDirectoryName(FullFolderPath);

    IFileManager& FileManager = IFileManager::Get();
    if (!FileManager.DirectoryExists(*FullFolderPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Folder not found: %s"), *FullFolderPath);
        return false;
    }

    TArray<FString> Files;
    FileManager.FindFilesRecursive(Files, *FullFolderPath, TEXT("*"), true, false, false);
    for (const FString& File : Files)
    {
        if (!FileManager.Delete(*File, false, true, true))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to delete file in folder: %s"), *File);
            return false;
        }
    }

    TArray<FString> Directories;
    FileManager.FindFilesRecursive(Directories, *FullFolderPath, TEXT("*"), false, true, false);
    Directories.Sort([](const FString& A, const FString& B)
        {
            return A.Len() > B.Len();
        });

    for (const FString& Dir : Directories)
    {
        if (!FileManager.DeleteDirectory(*Dir, false, true))
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to delete sub-folder: %s"), *Dir);
            return false;
        }
    }

    if (!FileManager.DirectoryExists(*FullFolderPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Root folder was deleted unexpectedly: %s"), *FullFolderPath);
        return false;
    }

    return true;
}

void UFileToolBPL::GetPathType(const FString& InputPath, bool& bIsDirectory, bool& bIsFile)
{
    FString FullPath = FPaths::ConvertRelativePathToFull(InputPath);
    FPaths::NormalizeFilename(FullPath);

    IFileManager& FileManager = IFileManager::Get();
    bIsDirectory = FileManager.DirectoryExists(*FullPath);
    bIsFile = FileManager.FileExists(*FullPath);
}

TArray<FString> UFileToolBPL::GetAllFbxFilesInFolder(const FString& FolderPath)
{
    TArray<FString> OutFbxFiles;

    FString FullFolderPath = FPaths::ConvertRelativePathToFull(FolderPath);
    FPaths::NormalizeDirectoryName(FullFolderPath);

    IFileManager& FileManager = IFileManager::Get();
    if (!FileManager.DirectoryExists(*FullFolderPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Folder not found for FBX search: %s"), *FullFolderPath);
        return OutFbxFiles;
    }

    TArray<FString> FoundFiles;
    FileManager.FindFilesRecursive(FoundFiles, *FullFolderPath, TEXT("*.*"), true, false, false);
    FoundFiles.Sort();

    for (FString FilePath : FoundFiles)
    {
        if (FPaths::GetExtension(FilePath, false).ToLower() != TEXT("fbx"))
        {
            continue;
        }
        FString FullFilePath = FPaths::ConvertRelativePathToFull(FilePath);
        FPaths::NormalizeFilename(FullFilePath);
        OutFbxFiles.Add(FullFilePath);
    }

    return OutFbxFiles;
}

FString UFileToolBPL::ZipFolder(const FString& FolderPath)
{
#if PLATFORM_WINDOWS
    FString FullFolderPath = FPaths::ConvertRelativePathToFull(FolderPath);
    FPaths::NormalizeDirectoryName(FullFolderPath);

    IFileManager& FileManager = IFileManager::Get();
    if (!FileManager.DirectoryExists(*FullFolderPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Zip source folder does not exist: %s"), *FullFolderPath);
        return TEXT("");
    }

    FString ParentDir = FPaths::GetPath(FullFolderPath);
    FString FolderName = FPaths::GetBaseFilename(FullFolderPath);
    FString ZipPath = FPaths::Combine(ParentDir, FolderName + TEXT(".zip"));
    FString FullZipPath = FPaths::ConvertRelativePathToFull(ZipPath);
    FPaths::NormalizeFilename(FullZipPath);

    FileManager.Delete(*FullZipPath, false, true, true);

    const FString Script = FString::Printf(
        TEXT("Compress-Archive -Path '%s\\*' -DestinationPath '%s' -Force"),
        *EscapePowerShellSingleQuote(FullFolderPath),
        *EscapePowerShellSingleQuote(FullZipPath)
    );

    int32 ReturnCode = -1;
    FString StdOut;
    FString StdErr;
    FPlatformProcess::ExecProcess(
        TEXT("powershell.exe"),
        *FString::Printf(TEXT("-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"%s\""), *Script),
        &ReturnCode,
        &StdOut,
        &StdErr
    );

    if (ReturnCode != 0 || !FileManager.FileExists(*FullZipPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Zip failed for folder: %s, Err: %s"), *FullFolderPath, *StdErr);
        return TEXT("");
    }

    return FullZipPath;
#else
    UE_LOG(LogTemp, Error, TEXT("ZipFolder is only supported on Windows."));
    return TEXT("");
#endif
}

FString UFileToolBPL::UnzipToFolder(const FString& ZipFilePath, const FString& TargetRootFolder)
{
#if PLATFORM_WINDOWS
    FString FullZipPath = FPaths::ConvertRelativePathToFull(ZipFilePath);
    FPaths::NormalizeFilename(FullZipPath);

    IFileManager& FileManager = IFileManager::Get();
    if (!FileManager.FileExists(*FullZipPath))
    {
        UE_LOG(LogTemp, Error, TEXT("Zip file does not exist: %s"), *FullZipPath);
        return TEXT("");
    }

    FString FullTargetRoot = FPaths::ConvertRelativePathToFull(TargetRootFolder);
    FPaths::NormalizeDirectoryName(FullTargetRoot);

    if (!FileManager.DirectoryExists(*FullTargetRoot) && !FileManager.MakeDirectory(*FullTargetRoot, true))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create target root folder: %s"), *FullTargetRoot);
        return TEXT("");
    }

    FString ZipBaseName = FPaths::GetBaseFilename(FullZipPath);
    FString ExtractFolder = FPaths::Combine(FullTargetRoot, ZipBaseName);
    FPaths::NormalizeDirectoryName(ExtractFolder);

    if (!FileManager.DirectoryExists(*ExtractFolder) && !FileManager.MakeDirectory(*ExtractFolder, true))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create extract folder: %s"), *ExtractFolder);
        return TEXT("");
    }

    const FString Script = FString::Printf(
        TEXT("Expand-Archive -Path '%s' -DestinationPath '%s' -Force"),
        *EscapePowerShellSingleQuote(FullZipPath),
        *EscapePowerShellSingleQuote(ExtractFolder)
    );

    int32 ReturnCode = -1;
    FString StdOut;
    FString StdErr;
    FPlatformProcess::ExecProcess(
        TEXT("powershell.exe"),
        *FString::Printf(TEXT("-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"%s\""), *Script),
        &ReturnCode,
        &StdOut,
        &StdErr
    );

    if (ReturnCode != 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Unzip failed for file: %s, Err: %s"), *FullZipPath, *StdErr);
        return TEXT("");
    }

    return ExtractFolder;
#else
    UE_LOG(LogTemp, Error, TEXT("UnzipToFolder is only supported on Windows."));
    return TEXT("");
#endif
}
