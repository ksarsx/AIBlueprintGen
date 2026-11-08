#pragma once

#include "CoreMinimal.h"

/**
 * Extra top-level keys used when a NEW asset is created (and "textures" also when editing a material):
 * {
 *   "asset": { "type": "Blueprint", "name": "BP_Door", "path": "/Game/AI", "parentClass": "Actor" },
 *   "textures": [ { "name": "T_Brick_D", "file": "C:/Textures/brick.png", "usage": "Color" } ],
 *   ... the usual Blueprint or material graph keys ...
 * }
 */

struct FABGAssetSpec
{
	bool bPresent = false;
	/** Blueprint, WidgetBlueprint, Material or MaterialFunction (empty = not stated). */
	FString Type;
	FString Name;
	/** Package folder such as /Game/AI (empty = not stated). */
	FString Path;
	/** Blueprint kinds only: Actor, Pawn, ActorComponent, ... or a full class path. */
	FString ParentClass;
};

struct FABGTextureSpec
{
	/** Name of the Texture2D asset to create. Material nodes can use this name instead of a full path. */
	FString Name;
	/** Image file on disk (png, jpg, tga, bmp, exr, hdr, ...). */
	FString File;
	/** Color (sRGB, default), Normal (normal map), Mask (linear data: roughness, metallic, AO). */
	FString Usage;
};

struct FABGCreateSpec
{
	FABGAssetSpec Asset;
	TArray<FABGTextureSpec> Textures;
};
