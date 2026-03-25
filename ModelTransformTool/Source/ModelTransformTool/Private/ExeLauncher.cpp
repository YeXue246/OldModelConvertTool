#include "ExeLauncher.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Misc/MessageDialog.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

TQueue<FExeTask> UExeLauncher::TaskQueue;
bool UExeLauncher::bIsRunning = false;
bool UExeLauncher::bAllTasksSuccess = true;
FString UExeLauncher::LasstTasksOutputFloder = "";
TSet<FString> UExeLauncher::FailedFolderTaskKeys;

FOnTaskCompleted UExeLauncher::TaskCompletedCallback;
FOnAllTasksCompleted UExeLauncher::AllTasksCompletedCallback;
FOnTaskStarted UExeLauncher::TaskStartedCallback;

FProcHandle UExeLauncher::CurrentProcHandle;
FCriticalSection UExeLauncher::Mutex;
bool bDependenciesCopied = false;

bool GetDiskFreeSpaceBytes(const FString& AnyPathOnDisk, int64& OutFreeBytes)
{
    FString PathRoot = FPaths::GetPath(AnyPathOnDisk);
    if (PathRoot.IsEmpty())
    {
        return false;
    }

    ULARGE_INTEGER FreeBytesAvailable, TotalNumberOfBytes, TotalNumberOfFreeBytes;
    BOOL bSuccess = ::GetDiskFreeSpaceExW(*PathRoot, &FreeBytesAvailable, &TotalNumberOfBytes, &TotalNumberOfFreeBytes);
    if (bSuccess)
    {
        OutFreeBytes = static_cast<int64>(FreeBytesAvailable.QuadPart);
        return true;
    }
    return false;
}


void UExeLauncher::HandleTaskFailure(const FExeTask& Task, const FString& ErrorMessage, bool bShowDialog, int64 RequiredBytes, int64 FreeBytes)
{
    bAllTasksSuccess = false;
    MarkFolderTaskFailed(Task);

    UE_LOG(LogTemp, Error, TEXT("%s : %s"), *ErrorMessage, *Task.InputFile);

    if (bShowDialog)
    {
        FString Msg = FString::Printf(
            TEXT("磁盘空间不足，无法转换文件：\n需要 %lld MB，剩余 %lld MB"),
            RequiredBytes,
            FreeBytes
        );

        FText MsgText = FText::FromString(Msg);
        FMessageDialog::Open(EAppMsgType::Ok, MsgText);
    }

    if (TaskCompletedCallback.IsBound())
    {
        TaskCompletedCallback.Execute(Task.InputFile, TEXT(""), TEXT(""));
    }

    ExecuteNextTask();
}


void UExeLauncher::AddTask(const FString& InputFile, const FString& OutputFolder, const FString& TemplateFile)
{
    FScopeLock Lock(&Mutex);

    FString InFile = FPaths::ConvertRelativePathToFull(InputFile);
    FString OutDir = FPaths::ConvertRelativePathToFull(OutputFolder);
    FString TeFile = FPaths::ConvertRelativePathToFull(TemplateFile);

    FPaths::NormalizeFilename(InFile);
    FPaths::NormalizeFilename(OutDir);
    FPaths::NormalizeFilename(TeFile);

    if (!IFileManager::Get().DirectoryExists(*OutDir))
    {
        if (IFileManager::Get().MakeDirectory(*OutDir, true))
        {
            UE_LOG(LogTemp, Log, TEXT("文件夹已创建: %s"), *OutDir);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("无法创建文件夹: %s"), *OutDir);
        }
    }

    if (IFileManager::Get().DirectoryExists(*InFile))
    {
        TArray<FString> FilesInFolder;
        IFileManager::Get().FindFilesRecursive(FilesInFolder, *InFile, TEXT("*.*"), true, false, false);
        FilesInFolder.Sort();

        if (FilesInFolder.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("Folder task has no files: %s"), *InFile);
            return;
        }

        for (const FString& FilePath : FilesInFolder)
        {
            FString NormalizedFile = FPaths::ConvertRelativePathToFull(FilePath);
            FPaths::NormalizeFilename(NormalizedFile);

            FExeTask FolderTask;
            FolderTask.InputFile = NormalizedFile;
            FolderTask.OutputFolder = OutDir;
            FolderTask.TemplateFile = TeFile;
            FolderTask.bIsFolderTask = true;
            FolderTask.FolderTaskKey = InFile;

            TaskQueue.Enqueue(FolderTask);
        }
        return;
    }

    TaskQueue.Enqueue(FExeTask{ InFile, OutDir, TeFile, false, TEXT("") });
}

void UExeLauncher::RunQueue(const FOnTaskCompleted& OnTaskCompleted, const FOnAllTasksCompleted& OnAllTasksCompleted, const FOnTaskStarted& OnTaskStarted)
{
    if (bIsRunning)
    {
        UE_LOG(LogTemp, Warning, TEXT("Task queue already running"));
        return;
    }

    bIsRunning = true;
    bAllTasksSuccess = true;
    LasstTasksOutputFloder = TEXT("");
    FailedFolderTaskKeys.Reset();
    TaskCompletedCallback = OnTaskCompleted;
    AllTasksCompletedCallback = OnAllTasksCompleted;
    TaskStartedCallback = OnTaskStarted;

    if (!bDependenciesCopied)
    {
#if WITH_EDITOR
        FString ExeDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("ModelTransformTool/Binaries/Win64/AMFTZ3"));
#else
        FString ExeDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64/AMFTZ3"));
#endif
        FPaths::NormalizeDirectoryName(ExeDir);
        CopyDependencies(ExeDir);
        bDependenciesCopied = true;
    }


    ExecuteNextTask();
}

void UExeLauncher::ExecuteNextTask()
{
    FExeTask CurrentTask;

    while (TaskQueue.Dequeue(CurrentTask))
    {
        if (!ShouldSkipTask(CurrentTask))
        {
            break;
        }

        UE_LOG(LogTemp, Warning, TEXT("Skip folder task file due to previous failure: %s"), *CurrentTask.InputFile);
        if (TaskCompletedCallback.IsBound())
        {
            TaskCompletedCallback.Execute(CurrentTask.InputFile, TEXT(""), TEXT(""));
        }
    }

    if (ShouldSkipTask(CurrentTask) || CurrentTask.InputFile.IsEmpty())
    {
        bIsRunning = false;
        if (AllTasksCompletedCallback.IsBound())
        {
            AllTasksCompletedCallback.Execute(LasstTasksOutputFloder, bAllTasksSuccess);
        }
        return;
    }
    LasstTasksOutputFloder = PathUEToWindows(CurrentTask.OutputFolder);


    int64 FileSize = GetFileSizeByPath(CurrentTask.InputFile);
    if (FileSize < 0)
    {
        HandleTaskFailure(CurrentTask, TEXT("Input file not found"));
        return;
    }


    int64 FreeBytes = 0;
    if (!GetDiskFreeSpaceBytes(CurrentTask.OutputFolder, FreeBytes))
    {
        HandleTaskFailure(CurrentTask, TEXT("Unable to query free space"), false);
        return;
    }

    FString InputExt = FPaths::GetExtension(CurrentTask.InputFile, true).ToLower();
    float Multiplier = 2.6;
    if (InputExt == TEXT(".json"))
    {
        Multiplier = 15;
    }
    else if (InputExt == TEXT(".dxf"))
    {
        Multiplier = 3.5;
    }

    int64 Required = static_cast<int64>(static_cast<double>(FileSize) * Multiplier);
    int64 FreeSizeMB = FreeBytes / 1024 / 1024;
    if (FreeSizeMB < Required)
    {
        HandleTaskFailure(CurrentTask, TEXT("Not enough disk space"), true, Required, FreeSizeMB);
        return;
    }



    FString ExeName;
    FString ExeDir;


    if (InputExt == TEXT(".json"))
    {
        ExeName = TEXT("MeshToDXF.exe");
    }
    else
    {
        ExeName = TEXT("AssimpTest.exe");
    }

#if WITH_EDITOR

    ExeDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("ModelTransformTool/Binaries/Win64/AMFTZ3"));
#else
    ExeDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64/AMFTZ3"));
#endif

    FPaths::NormalizeDirectoryName(ExeDir); 
    FString ExePath = FPaths::Combine(ExeDir, ExeName);
    ExePath = FPaths::ConvertRelativePathToFull(ExePath);
    FPaths::NormalizeDirectoryName(ExePath);


    FString Args;
    if (InputExt == TEXT(".json"))
    {
        Args = FString::Printf(TEXT("--cli \"%s\" \"%s\""),
            *CurrentTask.InputFile,
            *CurrentTask.OutputFolder
        );
    }
    else
    {
        Args = FString::Printf(TEXT("--cli \"%s\" \"%s\" \"%s\""),
            *CurrentTask.InputFile,
            *CurrentTask.OutputFolder,
            *CurrentTask.TemplateFile
        );
    }

    if (TaskStartedCallback.IsBound())
    {
        TaskStartedCallback.Execute(CurrentTask.InputFile);
    }

    LaunchExeAsync(ExePath, Args, CurrentTask.OutputFolder, CurrentTask);
}

void UExeLauncher::CopyDependencies(const FString& ExeDirRaw)
{
    FString ExeDir = FPaths::ConvertRelativePathToFull(ExeDirRaw);
    FPaths::NormalizeDirectoryName(ExeDir);

    if (!IFileManager::Get().DirectoryExists(*ExeDir))
    {
        UE_LOG(LogTemp, Warning, TEXT("Dependency directory not found: %s"), *ExeDir);
        return;
    }

    TArray<FString> SubDirs;
    IFileManager::Get().FindFiles(SubDirs, *(ExeDir / TEXT("*")), false, true);

    for (const FString& Sub : SubDirs)
    {
        FString SrcDir = ExeDir / Sub;
        if (!IFileManager::Get().DirectoryExists(*SrcDir))
            continue;

        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(SrcDir / TEXT("*.*")), true, true);

        for (const FString& File : Files)
        {
            FString Src = FPaths::Combine(SrcDir, File);
            FString Dest = FPaths::Combine(ExeDir, File);
            if (IFileManager::Get().FileExists(*Src))
            {
                if (IFileManager::Get().Copy(*Dest, *Src, true, true) != COPY_OK)
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to copy dependency: %s"), *Src);
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("Copied dependency: %s -> %s"), *Src, *Dest);
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Dependencies copied successfully for directory: %s"), *ExeDir);
}


void UExeLauncher::LaunchExeAsync(const FString& ExePath, const FString& Args, const FString& OutputFolder, const FExeTask& Task)
{
    Async(EAsyncExecution::Thread, [ExePath, Args, OutputFolder, Task]()
        {
            void* ReadPipe = nullptr;
            void* WritePipe = nullptr;
            FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

            FTCHARToUTF8 ArgsUTF8(*Args);
            CurrentProcHandle = FPlatformProcess::CreateProc(
                *ExePath,
                UTF8_TO_TCHAR(ArgsUTF8.Get()),
                true,  
                true,  
                false,
                nullptr,
                0,
                *FPaths::GetPath(ExePath),
                WritePipe
            );

            if (!CurrentProcHandle.IsValid())
            {
                FString FullExePath = FPaths::ConvertRelativePathToFull(ExePath);
                UE_LOG(LogTemp, Warning, TEXT("Failed to start exe: %s"), *FullExePath);

                bAllTasksSuccess = false;
                MarkFolderTaskFailed(Task);

                if (TaskCompletedCallback.IsBound())
                {
                    AsyncTask(ENamedThreads::GameThread, [Task]()
                        {
                            TaskCompletedCallback.Execute(Task.InputFile, TEXT("ERROR"), TEXT("Failed to start exe"));
                        });
                }

                FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
                AsyncTask(ENamedThreads::GameThread, []()
                    {
                        ExecuteNextTask();
                    });
                return;
            }




            FString StdOut;
            TArray<uint8> Buffer;
            const int32 ChunkSize = 4096;
            while (FPlatformProcess::IsProcRunning(CurrentProcHandle))
            {
                Buffer.Reset();
                if (FWindowsPlatformProcess::ReadPipeToArray(ReadPipe, Buffer) && Buffer.Num() > 0)
                {
                    Buffer.Add(0);
                    FString Chunk(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));
                    StdOut += Chunk;
                }
                FPlatformProcess::Sleep(0.01f);
            }


            Buffer.Reset();
            if (FWindowsPlatformProcess::ReadPipeToArray(ReadPipe, Buffer) && Buffer.Num() > 0)
            {
                Buffer.Add(0);
                FString Chunk(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Buffer.GetData())));
                StdOut += Chunk;
            }



            StdOut.TrimStartAndEndInline();
            StdOut.ReplaceInline(TEXT("\r"), TEXT(""));

            UE_LOG(LogTemp, Warning, TEXT("Python CLI StdOut:\n%s"), *StdOut);

            int32 ReturnCode = -1;
            FPlatformProcess::GetProcReturnCode(CurrentProcHandle, &ReturnCode);
            bool bSuccess = (ReturnCode == 0);

            FString OutputFilePath;
            FString OutputFileName;


            bool bGotOutput = false;
            if (bSuccess && !StdOut.IsEmpty())
            {
                TArray<TSharedPtr<FJsonValue>> JsonArray;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StdOut);
                if (FJsonSerializer::Deserialize(Reader, JsonArray))
                {
                    for (auto& Val : JsonArray)
                    {
                        FString FilePath = Val->AsString();
                        if (FilePath.Contains(FPaths::GetBaseFilename(Task.InputFile)))
                        {
                            OutputFilePath = FilePath;
                            OutputFileName = FPaths::GetCleanFilename(FilePath);
                            bGotOutput = true;
                            break;
                        }
                    }
                }
            }


            if (!bGotOutput && bSuccess && !StdOut.IsEmpty())
            {
                FString JsonPart;
                int32 StartIdx = -1;
                int32 BracketCount = 0;

                for (int32 i = 0; i < StdOut.Len(); ++i)
                {
                    if (StdOut[i] == '[')
                    {
                        if (StartIdx == -1)
                        {
                            StartIdx = i;
                        }
                        BracketCount++;
                    }
                    else if (StdOut[i] == ']')
                    {
                        BracketCount--;
                        if (BracketCount == 0 && StartIdx != -1)
                        {
                            JsonPart = StdOut.Mid(StartIdx, i - StartIdx + 1);
                            StartIdx = -1;
                        }
                    }
                }


                if (bSuccess && !JsonPart.IsEmpty())
                {
                    TArray<TSharedPtr<FJsonValue>> JsonArray;
                    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonPart);
                    if (FJsonSerializer::Deserialize(Reader, JsonArray))
                    {
                        for (auto& Val : JsonArray)
                        {
                            FString FilePath = Val->AsString();
                            if (FilePath.Contains(FPaths::GetBaseFilename(Task.InputFile)))
                            {
                                OutputFilePath = FilePath;
                                OutputFileName = FPaths::GetCleanFilename(FilePath);
                                break;
                            }
                        }
                    }
                }
            }


            if (TaskCompletedCallback.IsBound())
            {
                AsyncTask(ENamedThreads::GameThread, [Task, OutputFilePath, OutputFileName]()
                    {
                        TaskCompletedCallback.Execute(Task.InputFile, OutputFilePath, OutputFileName);
                    });
            }

            if (!bSuccess || OutputFilePath.IsEmpty())
            {
                AsyncTask(ENamedThreads::GameThread, [Task]()
                    {
                        bAllTasksSuccess = false;
                        MarkFolderTaskFailed(Task);
                    });
            }

            FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
            FPlatformProcess::CloseProc(CurrentProcHandle);
            CurrentProcHandle.Reset();

            AsyncTask(ENamedThreads::GameThread, []()
                {
                    ExecuteNextTask();
                });
        });
}

void UExeLauncher::MarkFolderTaskFailed(const FExeTask& Task)
{
    if (Task.bIsFolderTask && !Task.FolderTaskKey.IsEmpty())
    {
        FailedFolderTaskKeys.Add(Task.FolderTaskKey);
    }
}

bool UExeLauncher::ShouldSkipTask(const FExeTask& Task)
{
    return Task.bIsFolderTask && FailedFolderTaskKeys.Contains(Task.FolderTaskKey);
}


void UExeLauncher::CancelTaskByInput(const FString& InputFile)
{
    FScopeLock Lock(&Mutex);

    FString NormalizedInput = FPaths::ConvertRelativePathToFull(InputFile);
    FPaths::MakePlatformFilename(NormalizedInput);

    TQueue<FExeTask, EQueueMode::Spsc> NewQueue;
    FExeTask Task;

    while (TaskQueue.Dequeue(Task))
    {
        FString NormalizedTask = FPaths::ConvertRelativePathToFull(Task.InputFile);
        FPaths::MakePlatformFilename(NormalizedTask);
        FString NormalizedTaskFolderKey = FPaths::ConvertRelativePathToFull(Task.FolderTaskKey);
        FPaths::MakePlatformFilename(NormalizedTaskFolderKey);

        if (NormalizedTask != NormalizedInput && NormalizedTaskFolderKey != NormalizedInput)
        {
            NewQueue.Enqueue(Task);
        }
    }

    while (NewQueue.Dequeue(Task))
    {
        TaskQueue.Enqueue(Task);
    }

    UE_LOG(LogTemp, Log, TEXT("Cancelled task for input: %s"), *NormalizedInput);
}

void UExeLauncher::ClearAllTasks()
{
    FScopeLock Lock(&Mutex);
    FExeTask Dummy;
    while (TaskQueue.Dequeue(Dummy)) {}
    FailedFolderTaskKeys.Reset();
    UE_LOG(LogTemp, Log, TEXT("Cleared all pending tasks"));
}

void UExeLauncher::AbortAllTasks()
{
    {
        FScopeLock Lock(&Mutex);
        if (CurrentProcHandle.IsValid())
        {
            FPlatformProcess::TerminateProc(CurrentProcHandle, true);
            FPlatformProcess::CloseProc(CurrentProcHandle);
            CurrentProcHandle.Reset();
            UE_LOG(LogTemp, Warning, TEXT("Aborted current running EXE process"));
        }

        FExeTask Dummy;
        while (TaskQueue.Dequeue(Dummy)) {}
        FailedFolderTaskKeys.Reset();
    }

    bIsRunning = false;
    bAllTasksSuccess = false;

    if (AllTasksCompletedCallback.IsBound())
    {
        AllTasksCompletedCallback.Execute(TEXT(""), false);
    }

    UE_LOG(LogTemp, Log, TEXT("All tasks aborted"));
}



bool UExeLauncher::LaunchGuiExe(const FString& ExeName, const FString& Args)
{
    FString ExeDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("ModelTransformTool/Binaries/Win64/AMFTZ3"));
    FString ExePath = FPaths::Combine(ExeDir, ExeName);

    if (!FPaths::FileExists(ExePath))
    {
        UE_LOG(LogTemp, Error, TEXT("GUI exe not found: %s"), *ExePath);
        return false;
    }

    FProcHandle Handle = FPlatformProcess::CreateProc(
        *ExePath,
        *Args,
        true,  
        false,  
        false,
        nullptr, 0,
        *FPaths::GetPath(ExePath),
        nullptr
    );

    if (!Handle.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to launch GUI exe: %s"), *ExePath);
        return false;
    }

    return true;
}

FString UExeLauncher::PathUEToWindows(FString Path)
{

    FString WindowsPath = FPaths::ConvertRelativePathToFull(Path);
    FPaths::MakePlatformFilename(WindowsPath);
    return WindowsPath;
}

FString UExeLauncher::PathWindowsToUE(FString Path)
{
    FString UEPath = Path;

    FPaths::NormalizeFilename(UEPath);
    return UEPath;
}

int32 UExeLauncher::GetFileSizeByPath(const FString& FilePath)
{
    return IFileManager::Get().FileSize(*FilePath)/1024/1024;
}

bool UExeLauncher::FileIsLargerThanKB(const FString& FilePath, const int32 TargetSize)
{
    return ((IFileManager::Get().FileSize(*FilePath)) > (TargetSize * 1024));
}
