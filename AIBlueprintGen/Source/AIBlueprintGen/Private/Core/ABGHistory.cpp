#include "Core/ABGHistory.h"
#include "ABGSettings.h"
#include "Core/AIBlueprintGenLog.h"

#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FString FABGHistory::GetDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AIBlueprintGen/History"));
}

void FABGHistory::Add(const TCHAR* Kind, const FString& Target, const FString& Body)
{
	const FString Dir = GetDirectory();

	// File names sort by time: 20260105-142233-512_answer_M_lava.txt
	FString SafeTarget = Target;
	for (TCHAR& Char : SafeTarget)
	{
		if (!FChar::IsAlnum(Char) && Char != TEXT('_') && Char != TEXT('-'))
		{
			Char = TEXT('_');
		}
	}
	SafeTarget = SafeTarget.Left(60);
	const FDateTime Now = FDateTime::Now();
	const FString Name = FString::Printf(TEXT("%s_%s%s%s.txt"),
		*Now.ToString(TEXT("%Y%m%d-%H%M%S-%s")), Kind, SafeTarget.IsEmpty() ? TEXT("") : TEXT("_"), *SafeTarget);

	const FString Content = FString::Printf(TEXT("Time: %s\nKind: %s\nTarget: %s\n\n%s\n"), *Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")), Kind, *Target, *Body);
	if (!FFileHelper::SaveStringToFile(Content, *(Dir / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogAIBlueprintGen, Warning, TEXT("History: could not write %s"), *(Dir / Name));
		return;
	}
	UE_LOG(LogAIBlueprintGen, Log, TEXT("History: %s (%d chars) -> %s"), Kind, Body.Len(), *(Dir / Name));

	// Keep the newest N files.
	const UABGSettings* Settings = UABGSettings::Get();
	const int32 MaxEntries = FMath::Max(1, Settings ? Settings->HistoryMaxEntries : 100);
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.txt")), true, false);
	if (Files.Num() > MaxEntries)
	{
		Files.Sort();
		for (int32 Index = 0; Index < Files.Num() - MaxEntries; ++Index)
		{
			IFileManager::Get().Delete(*(Dir / Files[Index]), false, true, true);
		}
	}
}
