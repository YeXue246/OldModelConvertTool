#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FileToolBPL.generated.h"

#ifndef MODELCONVERTTOOL_API
#define MODELCONVERTTOOL_API MODELTRANSFORMTOOL_API
#endif

UCLASS()
class MODELCONVERTTOOL_API UFileToolBPL : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "FileTool")
    static bool ClearFolderContents(const FString& FolderPath);

    UFUNCTION(BlueprintCallable, Category = "FileTool")
    static FString ZipFolder(const FString& FolderPath);

    UFUNCTION(BlueprintCallable, Category = "FileTool")
    static FString UnzipToFolder(const FString& ZipFilePath, const FString& TargetRootFolder);
};
