#pragma once

#include "CoreMinimal.h"
#include "Schema/ABGGraphSpec.h"

/** JSON text -> FABGGraphSpec. Only checks structure; names are validated against the engine later. */
class FABGGraphJson
{
public:
	/** @return true if the JSON was well-formed and structurally valid. Problems are appended to OutErrors. */
	static bool Parse(const FString& InJson, FABGGraphSpec& OutSpec, TArray<FString>& OutErrors);

	/**
	 * Pulls the JSON object out of a raw model reply: strips ```json fences and any prose around it
	 * by taking the text between the first '{' and the last '}'. Returns the trimmed input if there is no object.
	 */
	static FString ExtractJson(const FString& RawModelText);
};
