#pragma once

#include "CoreMinimal.h"

/**
 * Material graph format (JSON):
 * {
 *   "settings": { "BlendMode": "Translucent", "TwoSided": "true" },
 *   "nodes": [ { "id": "tint", "type": "Constant3Vector", "props": { "Constant": "1,0,0" } } ],
 *   "connections": [ { "from": "tint", "to": "Material.BaseColor" } ],
 *   "disconnect": [ "Material.Roughness" ],
 *   "remove": [ "e_Multiply_0" ],
 *   "unsupported": [ "..." ]
 * }
 */

namespace ABGMaterialNodeType
{
	/** An already existing expression; only used to change its properties or as a wire endpoint. */
	static const TCHAR* const Existing = TEXT("Existing");
}

/** Name used in connections for the material's own outputs (BaseColor, Roughness, ...). */
static const TCHAR* const ABGMaterialRootName = TEXT("Material");

struct FABGMatNode
{
	FString Id;
	/** Expression class without the "MaterialExpression" prefix ("Multiply", "TextureSampleParameter2D") or "Existing". */
	FString Type;
	/** Property name -> value text. Order is kept. */
	TArray<TPair<FString, FString>> Props;
};

struct FABGMatPinRef
{
	FString NodeId;
	/** Output or input name. Empty = the first one. */
	FString PinName;
};

struct FABGMatConnection
{
	FABGMatPinRef From;
	/** NodeId == "Material" means a material output named PinName. */
	FABGMatPinRef To;
};

struct FABGMaterialSpec
{
	TArray<TPair<FString, FString>> Settings;
	TArray<FABGMatNode> Nodes;
	TArray<FABGMatConnection> Connections;
	/** Inputs (or material outputs) whose wire is cut: "node.Input" or "Material.BaseColor". */
	TArray<FABGMatPinRef> Disconnect;
	/** Ids ("e_...") of existing expressions to delete. */
	TArray<FString> Remove;
	TArray<FString> Unsupported;

	bool HasWork() const
	{
		return Settings.Num() > 0 || Nodes.Num() > 0 || Connections.Num() > 0 || Disconnect.Num() > 0 || Remove.Num() > 0;
	}
};
