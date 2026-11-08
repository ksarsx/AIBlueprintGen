#pragma once

#include "CoreMinimal.h"
#include "Prompt/ABGBlueprintKind.h"

/** What a "new asset" window creates. None = the panel works on an existing asset. */
enum class EABGCreateKind : uint8
{
	None,
	Blueprint,
	WidgetBlueprint,
	Material,
	MaterialFunction,
};

/** What the user typed into the "new asset" window; goes into the prompt so the AI repeats it in the JSON "asset" block. */
struct FABGCreateRequest
{
	EABGCreateKind Kind = EABGCreateKind::None;
	FString Name;
	FString Folder;
	/** Blueprint kinds only. */
	FString ParentClass;
};

namespace ABGCreateKind
{
	inline bool IsMaterial(EABGCreateKind Kind)
	{
		return Kind == EABGCreateKind::Material || Kind == EABGCreateKind::MaterialFunction;
	}

	/** The JSON "asset.type" value. */
	inline const TCHAR* TypeName(EABGCreateKind Kind)
	{
		switch (Kind)
		{
		case EABGCreateKind::Blueprint: return TEXT("Blueprint");
		case EABGCreateKind::WidgetBlueprint: return TEXT("WidgetBlueprint");
		case EABGCreateKind::Material: return TEXT("Material");
		case EABGCreateKind::MaterialFunction: return TEXT("MaterialFunction");
		default: return TEXT("");
		}
	}

	/** Window title / menu label (Russian UI text). */
	inline FString Label(EABGCreateKind Kind)
	{
		switch (Kind)
		{
		case EABGCreateKind::Blueprint: return TEXT("Новый Blueprint");
		case EABGCreateKind::WidgetBlueprint: return TEXT("Новый Widget Blueprint");
		case EABGCreateKind::Material: return TEXT("Новый материал");
		case EABGCreateKind::MaterialFunction: return TEXT("Новая Material Function");
		default: return FString();
		}
	}

	/** Which prompt profile (notes + settings instructions) a new asset of this kind uses. */
	inline EABGBlueprintKind ToPromptKind(EABGCreateKind Kind, const FString& ParentClassText)
	{
		switch (Kind)
		{
		case EABGCreateKind::WidgetBlueprint: return EABGBlueprintKind::Widget;
		case EABGCreateKind::Material: return EABGBlueprintKind::Material;
		case EABGCreateKind::MaterialFunction: return EABGBlueprintKind::MaterialFunction;
		case EABGCreateKind::Blueprint:
			return ParentClassText.Contains(TEXT("Component")) ? EABGBlueprintKind::Component : EABGBlueprintKind::Actor;
		default: return EABGBlueprintKind::Other;
		}
	}
}
