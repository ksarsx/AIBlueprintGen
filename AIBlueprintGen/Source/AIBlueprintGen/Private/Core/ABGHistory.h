#pragma once

#include "CoreMinimal.h"

/**
 * Journal of the copy-paste exchange with the external AI: every prompt that was copied, every fix prompt and every
 * reply that was applied, one text file per event in <Project>/Saved/AIBlueprintGen/History. Only the newest
 * entries are kept (UABGSettings::HistoryMaxEntries, 100 by default).
 */
struct FABGHistory
{
	/** Kind: "prompt", "fixprompt" or "answer". Target: the asset the panel works with (may be empty). */
	static void Add(const TCHAR* Kind, const FString& Target, const FString& Body);

	static FString GetDirectory();
};
