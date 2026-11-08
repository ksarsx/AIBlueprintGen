#pragma once

#include "CoreMinimal.h"
#include "Create/ABGCreateKind.h"
#include "Schema/ABGCreateSpec.h"

struct FABGMaterialSpec;
class UObject;
class UClass;

/** Creates new assets (Blueprint, Widget Blueprint, Material, Material Function), imports texture files and opens editors. */
class FABGAssetCreator
{
public:
	/** "" -> the default folder from the settings; "AI/Doors" -> "/Game/AI/Doors"; trailing slashes removed. Fails for folders outside /Game. */
	static bool NormalizeFolder(const FString& Folder, FString& OutFolder, FString& OutError);

	/** Long package name, e.g. "/Game/AI/BP_Door". */
	static FString MakePackageName(const FString& Folder, const FString& Name);

	/** True if an asset (on disk or already in memory) occupies this package name. */
	static bool AssetExists(const FString& PackageName);

	/** Parent class of a new Blueprint. Empty text = Actor (UserWidget for widgets). Accepts "Actor", "AActor", "Pawn", "/Script/Engine.Pawn", "/Game/BP/BP_Base". */
	static UClass* ResolveParentClass(EABGCreateKind Kind, const FString& Text, FString& OutError);

	/** Creates the empty asset through the same factories as the Content Browser. Returns null (with OutError) on failure. */
	static UObject* CreateAsset(EABGCreateKind Kind, const FString& Name, const FString& Folder, UClass* ParentClass, FString& OutError);

	/** Closes the asset's editors and deletes it (used to roll back a failed attempt). */
	static bool DeleteAsset(UObject* Asset);

	/** Opens the asset's editor window. */
	static void OpenEditor(UObject* Asset);

	/**
	 * Imports the image files as Texture2D assets into Folder. An asset that already exists is reused (a warning is added).
	 * Problems are appended to OutErrors; nothing is thrown away when one file fails.
	 */
	static void ImportTextures(const TArray<FABGTextureSpec>& Textures, const FString& Folder,
		TArray<FString>& OutErrors, TArray<FString>& OutWarnings, int32& OutImported);

	/** Replaces property values equal to a declared texture name ("T_Brick_D") with its asset path. */
	static void ApplyTextureAliases(FABGMaterialSpec& Spec, const TArray<FABGTextureSpec>& Textures, const FString& Folder);
};
