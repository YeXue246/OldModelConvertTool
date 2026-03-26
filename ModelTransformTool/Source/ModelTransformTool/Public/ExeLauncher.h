// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Delegates/DelegateCombinations.h" 
#include "ExeLauncher.generated.h"


DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnTaskCompleted, const FString&, InputFile, const FString&, OutputFilePath, const FString&, OutputFileName);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnAllTasksCompleted, const FString&, OutputFolder, bool, bAllSuccess);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnTaskStarted, const FString&, InputPath);

USTRUCT()
struct FExeTask
{
    GENERATED_BODY()
    FString InputFile;
    FString OutputFolder;
    FString TemplateFile;
    bool bIsFolderTask = false;
    FString FolderTaskKey;
};

UENUM(BlueprintType)
enum class EExeType : uint8
{
    Background, // Background process
    GuiHidden   // GUI EXE hidden launch
};


/**
 * 
 */
UCLASS()
class MODELTRANSFORMTOOL_API UExeLauncher : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "ExeLauncher")
    static void AddTask(const FString& InputFile, const FString& OutputFolder, const FString& TemplateFile, const FString& FolderModelExtensionsCsv = TEXT("obj,stl,3ds,ase,stp,step"));

    UFUNCTION(BlueprintCallable, Category = "ExeLauncher")
    static void RunQueue(const FOnTaskCompleted& OnTaskCompleted, const FOnAllTasksCompleted& OnAllTasksCompleted, const FOnTaskStarted& OnTaskStarted);

    UFUNCTION(BlueprintCallable, Category = "ExeLauncher")
    static void CancelTaskByInput(const FString& InputFile);

    UFUNCTION(BlueprintCallable, Category = "ExeLauncher")
    static void ClearAllTasks();

    UFUNCTION(BlueprintCallable, Category = "ExeLauncher")
    static void AbortAllTasks();

    UFUNCTION(BlueprintCallable, Category = "ExeLauncher")
    static bool LaunchGuiExe(const FString& ExeName, const FString& Args);

    UFUNCTION(BlueprintPure, Category = "ExeLauncher")
    static FString PathUEToWindows(FString Path);
    UFUNCTION(BlueprintPure, Category = "ExeLauncher")
    static FString PathWindowsToUE(FString Path);

    UFUNCTION(BlueprintPure, Category = "ExeLauncher")
    static int32  GetFileSizeByPath(const FString& FilePath);

    UFUNCTION(BlueprintPure, Category = "ExeLauncher")
    static bool  FileIsLargerThanKB(const FString& FilePath,const int32 TargetSize);

private:
    static void ExecuteNextTask();
    static void CopyDependencies(const FString& ExeDir);
    static void LaunchExeAsync(const FString& ExePath, const FString& Args, const FString& OutputFolder, const FExeTask& Task);
    static void MarkFolderTaskFailed(const FExeTask& Task);
    static bool ShouldSkipTask(const FExeTask& Task);

    static void HandleTaskFailure(const FExeTask& Task, const FString& ErrorMessage, bool bShowDialog = false, int64 RequiredBytes = 0, int64 FreeBytes = 0);

private:
    static TQueue<FExeTask> TaskQueue;
    static bool bIsRunning;
    static bool bAllTasksSuccess;
    static FString LasstTasksOutputFloder;

    static FOnTaskCompleted TaskCompletedCallback;
    static FOnAllTasksCompleted AllTasksCompletedCallback;
    static FOnTaskStarted TaskStartedCallback;
    static TSet<FString> FailedFolderTaskKeys;

    static FProcHandle CurrentProcHandle;
    static FCriticalSection Mutex;
	
};
