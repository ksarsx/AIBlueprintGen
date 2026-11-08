#pragma once

#include "CoreMinimal.h"
#include "Schema/ABGMaterialSpec.h"

/** JSON text -> FABGMaterialSpec. Only checks structure; names are validated against the engine by the builder. */
class FABGMaterialJson
{
public:
	static bool Parse(const FString& InJson, FABGMaterialSpec& OutSpec, TArray<FString>& OutErrors);
};
