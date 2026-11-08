#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialFunction;

/** Dumps a material (or material function) as text for an external AI: settings, expressions with ids, wires. */
class FABGMaterialExporter
{
public:
	/** Exactly one of Material / Function. MaxChars <= 0 means no limit. */
	static FString Export(UMaterial* Material, UMaterialFunction* Function, int32 MaxChars);
};
