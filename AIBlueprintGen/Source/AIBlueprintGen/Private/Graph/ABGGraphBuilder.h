#pragma once

#include "CoreMinimal.h"
#include "Schema/ABGGraphSpec.h"

class UBlueprint;
class UEdGraph;

struct FABGBuildResult
{
	bool bSuccess = false;
	bool bCompiled = false;
	int32 NodesCreated = 0;
	int32 ConnectionsMade = 0;
	int32 VariablesCreated = 0;
	int32 FunctionsCreated = 0;
	int32 NodesRemoved = 0;
	TArray<FString> Errors;
	TArray<FString> Warnings;
	/** Messages the Blueprint compiler put on the created nodes (only when the graph was built but does not compile). */
	TArray<FString> CompileErrors;
	/** What the model said it could not do (its "unsupported" list). */
	TArray<FString> Notes;

	/** Multi-line human readable summary for the panel/log. */
	FString ToDisplayString() const;
};

/** Spawns real Blueprint nodes from a parsed FABGGraphSpec. */
class FABGGraphBuilder
{
public:
	/**
	 * Finds the Blueprint + graph the user is currently working in.
	 * Requires exactly one Blueprint editor to be open.
	 */
	static bool FindTargetGraph(UBlueprint*& OutBlueprint, UEdGraph*& OutGraph, FString& OutError);

	/** The graph to edit in a SPECIFIC Blueprint: the focused graph of its editor, else its first event graph. */
	static bool FindTargetGraphFor(UBlueprint* Blueprint, UEdGraph*& OutGraph, FString& OutError);

	/**
	 * Applies variables, new functions and every graph part of the spec. A part without a graph name goes into FocusGraph.
	 * All-or-nothing: if any error occurs the whole operation is rolled back via the transaction.
	 * On success the Blueprint is compiled.
	 */
	static FABGBuildResult Build(UBlueprint* Blueprint, UEdGraph* FocusGraph, const FABGGraphSpec& Spec);
};
