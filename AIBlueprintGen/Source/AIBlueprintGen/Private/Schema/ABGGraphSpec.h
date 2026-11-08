#pragma once

#include "CoreMinimal.h"

/**
 * In-memory representation of the JSON graph format exchanged with the AI provider.
 *
 * {
 *   "nodes": [
 *     { "id": "begin", "type": "Event", "event": "ReceiveBeginPlay" },
 *     { "id": "print", "type": "CallFunction", "function": "KismetSystemLibrary.PrintString",
 *       "pins": { "InString": "Hello World" } }
 *   ],
 *   "connections": [ { "from": "begin.then", "to": "print.execute" } ]
 * }
 */

namespace ABGNodeType
{
	static const TCHAR* const Event = TEXT("Event");
	static const TCHAR* const CallFunction = TEXT("CallFunction");
	static const TCHAR* const Branch = TEXT("Branch");
	static const TCHAR* const SpawnActor = TEXT("SpawnActor");
	static const TCHAR* const GetVariable = TEXT("GetVariable");
	static const TCHAR* const SetVariable = TEXT("SetVariable");
	static const TCHAR* const Cast = TEXT("Cast");
	static const TCHAR* const Self = TEXT("Self");
	/** An already existing node; only used to change its pin defaults. */
	static const TCHAR* const Existing = TEXT("Existing");
	/** The start node of a function graph / construction script (existing or created). */
	static const TCHAR* const FunctionEntry = TEXT("FunctionEntry");
	/** The return node of a function graph (created when missing). */
	static const TCHAR* const FunctionResult = TEXT("FunctionResult");
}

struct FABGNodeSpec
{
	FString Id;
	/** One of ABGNodeType::*, stored in canonical casing. */
	FString Type;
	/** Event nodes: name of the overridable event function, e.g. ReceiveBeginPlay. */
	FString Event;
	/** CallFunction nodes: "Class.Function", e.g. KismetSystemLibrary.PrintString. */
	FString Function;
	/** GetVariable / SetVariable: name of a Blueprint member variable (or component). */
	FString Variable;
	/** SpawnActor / Cast: class name ("StaticMeshActor") or a full class path. */
	FString ClassName;
	/** Input pin name -> default value (as text). */
	TMap<FString, FString> PinDefaults;
};

struct FABGPinRef
{
	FString NodeId;
	FString PinName;
};

struct FABGConnectionSpec
{
	FABGPinRef From;
	FABGPinRef To;
};

/** A Blueprint member variable the graph needs. Created if it does not exist yet. */
struct FABGVariableSpec
{
	FString Name;
	/** bool, int, float, string, name, text, vector, rotator, transform, or a class name (object reference). */
	FString Type;
	FString Default;
};

/** One parameter of a new function. */
struct FABGParamSpec
{
	FString Name;
	FString Type;
};

/** A new function graph. Its body is described by a part with the same graph name. */
struct FABGFunctionSpec
{
	FString Name;
	TArray<FABGParamSpec> Inputs;
	TArray<FABGParamSpec> Outputs;
};

/** Nodes and wires for ONE graph. An empty GraphName means "the graph the user has focused". */
struct FABGGraphPart
{
	FString GraphName;
	TArray<FABGNodeSpec> Nodes;
	TArray<FABGConnectionSpec> Connections;
	/** Ids ("e_xxxxxxxx") of existing nodes to delete. */
	TArray<FString> Remove;

	bool IsEmpty() const { return Nodes.Num() == 0 && Connections.Num() == 0 && Remove.Num() == 0; }
};

struct FABGGraphSpec
{
	TArray<FABGVariableSpec> Variables;
	TArray<FABGFunctionSpec> Functions;
	TArray<FABGGraphPart> Parts;
	/** Things the model says it could not do (with reasons). May be the only content of a reply. */
	TArray<FString> Unsupported;

	bool HasWork() const
	{
		if (Variables.Num() > 0 || Functions.Num() > 0)
		{
			return true;
		}
		for (const FABGGraphPart& Part : Parts)
		{
			if (!Part.IsEmpty())
			{
				return true;
			}
		}
		return false;
	}
};
