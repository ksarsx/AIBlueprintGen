#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;

/**
 * Dumps everything useful about a Blueprint as text for an external AI: variables, components,
 * functions, and every graph with its nodes (ids like "e_1a2b3c4d") and wires in the plugin's own JSON dialect.
 */
class FABGBlueprintExporter
{
public:
	/** MaxChars <= 0 means no limit. The focused graph is listed first so truncation hits the less relevant graphs. */
	static FString Export(UBlueprint* Blueprint, UEdGraph* FocusGraph, int32 MaxChars);
};
