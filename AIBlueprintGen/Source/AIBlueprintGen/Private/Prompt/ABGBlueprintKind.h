#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UObject;

/** The kinds of Blueprint the plugin distinguishes. Each has its own built-in notes and user-editable instructions. */
enum class EABGBlueprintKind : uint8
{
	Actor,
	Component,
	Level,
	Widget,
	Animation,
	FunctionLibrary,
	MacroLibrary,
	Interface,
	Material,
	MaterialFunction,
	Other,
};

namespace ABGBlueprintKind
{
	EABGBlueprintKind Detect(const UBlueprint* Blueprint);

	/** Blueprint kinds plus Material / Material Function. */
	EABGBlueprintKind DetectAsset(const UObject* Asset);

	inline bool IsMaterialKind(EABGBlueprintKind Kind)
	{
		return Kind == EABGBlueprintKind::Material || Kind == EABGBlueprintKind::MaterialFunction;
	}

	/** Short human-readable name, e.g. "Level Blueprint". */
	FString Label(EABGBlueprintKind Kind);

	/** What the plugin and the JSON format can and cannot do for this kind (sent to the AI). */
	FString BuiltInNotes(EABGBlueprintKind Kind);

	/** The user's own instructions from the plugin settings for this kind (may be empty). */
	FString UserInstructions(EABGBlueprintKind Kind);
}
