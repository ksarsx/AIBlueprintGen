#pragma once

#include "CoreMinimal.h"
#include "Schema/ABGCreateSpec.h"

/** Reads the "asset" and "textures" keys of an answer. Every other key is left to the graph / material parsers. */
class FABGCreateJson
{
public:
	/** @return false when a present key is malformed (problems are appended to OutErrors). Missing keys are fine. */
	static bool Parse(const FString& InJson, FABGCreateSpec& OutSpec, TArray<FString>& OutErrors);

	/** Letters, digits and underscores; must not start with a digit. */
	static bool IsValidAssetName(const FString& Name);
};
