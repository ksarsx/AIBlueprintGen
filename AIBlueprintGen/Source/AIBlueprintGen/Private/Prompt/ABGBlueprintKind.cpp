#include "Prompt/ABGBlueprintKind.h"
#include "ABGSettings.h"

#include "Animation/AnimBlueprint.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/LevelScriptBlueprint.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"

namespace ABGBlueprintKind
{
	EABGBlueprintKind Detect(const UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return EABGBlueprintKind::Other;
		}
		if (Cast<ULevelScriptBlueprint>(Blueprint))
		{
			return EABGBlueprintKind::Level;
		}
		// Widget Blueprints live in the UMGEditor module; matching the class name avoids a module dependency
		// (also covers Editor Utility Widget Blueprints).
		if (Blueprint->GetClass()->GetName().Contains(TEXT("WidgetBlueprint")))
		{
			return EABGBlueprintKind::Widget;
		}
		if (Cast<UAnimBlueprint>(Blueprint))
		{
			return EABGBlueprintKind::Animation;
		}
		switch (Blueprint->BlueprintType)
		{
		case BPTYPE_FunctionLibrary: return EABGBlueprintKind::FunctionLibrary;
		case BPTYPE_MacroLibrary: return EABGBlueprintKind::MacroLibrary;
		case BPTYPE_Interface: return EABGBlueprintKind::Interface;
		default: break;
		}
		if (Blueprint->ParentClass)
		{
			if (Blueprint->ParentClass->IsChildOf(UActorComponent::StaticClass()))
			{
				return EABGBlueprintKind::Component;
			}
			if (Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
			{
				return EABGBlueprintKind::Actor;
			}
		}
		return EABGBlueprintKind::Other;
	}

	EABGBlueprintKind DetectAsset(const UObject* Asset)
	{
		if (Cast<UMaterial>(Asset))
		{
			return EABGBlueprintKind::Material;
		}
		if (Cast<UMaterialFunction>(Asset))
		{
			return EABGBlueprintKind::MaterialFunction;
		}
		return Detect(Cast<UBlueprint>(Asset));
	}

	FString Label(EABGBlueprintKind Kind)
	{
		switch (Kind)
		{
		case EABGBlueprintKind::Actor: return TEXT("Actor Blueprint");
		case EABGBlueprintKind::Component: return TEXT("Component Blueprint");
		case EABGBlueprintKind::Level: return TEXT("Level Blueprint");
		case EABGBlueprintKind::Widget: return TEXT("Widget Blueprint");
		case EABGBlueprintKind::Animation: return TEXT("Animation Blueprint");
		case EABGBlueprintKind::FunctionLibrary: return TEXT("Function Library");
		case EABGBlueprintKind::MacroLibrary: return TEXT("Macro Library");
		case EABGBlueprintKind::Interface: return TEXT("Blueprint Interface");
		case EABGBlueprintKind::Material: return TEXT("Material");
		case EABGBlueprintKind::MaterialFunction: return TEXT("Material Function");
		default: return TEXT("Blueprint");
		}
	}

	FString BuiltInNotes(EABGBlueprintKind Kind)
	{
		switch (Kind)
		{
		case EABGBlueprintKind::Actor:
			return TEXT(
				"This is an Actor Blueprint. Typical events: ReceiveBeginPlay, ReceiveTick (pin DeltaSeconds), ReceiveEndPlay, ReceiveActorBeginOverlap. "
				"Its components can be used as variables with GetVariable.");
		case EABGBlueprintKind::Component:
			return TEXT(
				"This is an Actor Component Blueprint. \"Self\" is the component, not an actor: use ActorComponent.GetOwner to reach the owning actor. "
				"Typical events: ReceiveBeginPlay, ReceiveTick (pin DeltaSeconds), ReceiveEndPlay.");
		case EABGBlueprintKind::Level:
			return TEXT(
				"This is the LEVEL Blueprint. It has no components. Actors placed in the level cannot be referenced by this format "
				"(references to selected level actors cannot be created): find them at runtime with GameplayStatics.GetActorOfClass, "
				"GameplayStatics.GetAllActorsOfClass or GameplayStatics.GetPlayerPawn, or report it in \"unsupported\". "
				"Typical events: ReceiveBeginPlay, ReceiveTick, ReceiveEndPlay.");
		case EABGBlueprintKind::Widget:
			return TEXT(
				"This is a Widget (UMG) Blueprint. Only its graphs can be edited: the widget layout of the Designer cannot be changed "
				"(widgets cannot be added, moved or restyled). Widgets that exist and are marked \"Is Variable\" can be used with GetVariable. "
				"Events of child widgets (Button OnClicked, ...) cannot be created by this format: report that in \"unsupported\". "
				"Events: Construct, Destruct, Tick (pins MyGeometry, InDeltaTime), PreConstruct, OnInitialized. "
				"Useful classes: UserWidget, Widget (SetVisibility), TextBlock (SetText), WidgetBlueprintLibrary, GameplayStatics.");
		case EABGBlueprintKind::Animation:
			return TEXT(
				"This is an Animation Blueprint. Only the EventGraph can be edited. The AnimGraph, state machines, transitions and "
				"animation nodes are NOT supported: report any such request in \"unsupported\". "
				"Events: BlueprintInitializeAnimation, BlueprintUpdateAnimation (pin DeltaTimeX), BlueprintPostEvaluateAnimation, BlueprintBeginPlay. "
				"Useful: AnimInstance.TryGetPawnOwner, Pawn/Character functions after a Cast.");
		case EABGBlueprintKind::FunctionLibrary:
			return TEXT(
				"This is a Blueprint Function Library. It has no event graph, no events, no variables and no \"Self\". "
				"All work is done in functions: declare each one in \"functions\" and describe its body as a graph with the same name, "
				"starting with a FunctionEntry node (and a FunctionResult node when it returns values). Do not declare \"variables\".");
		case EABGBlueprintKind::MacroLibrary:
			return TEXT(
				"This is a Macro Library. Macros cannot be created by this format and macro graphs can only be changed through \"Existing\" nodes. "
				"If a new macro is requested, say so in \"unsupported\".");
		case EABGBlueprintKind::Interface:
			return TEXT(
				"This is a Blueprint Interface. It only declares function signatures: you may declare functions (name, inputs, outputs) in \"functions\", "
				"but they cannot have a body, and there are no events, variables or event graph. Anything else goes to \"unsupported\".");
		case EABGBlueprintKind::Material:
			return TEXT(
				"This is a MATERIAL (not a Blueprint). It is built from material expressions wired into the material's outputs "
				"(BaseColor, Metallic, Specular, Roughness, Anisotropy, EmissiveColor, Opacity, OpacityMask, Normal, Tangent, WorldPositionOffset, "
				"SubsurfaceColor, AmbientOcclusion, Refraction, PixelDepthOffset). Use \"settings\" for blend mode, shading model, two-sided and similar. "
				"Material functions, material instances, custom HLSL beyond the Custom expression, and creating texture assets are not handled here; "
				"use existing textures by their asset path.");
		case EABGBlueprintKind::MaterialFunction:
			return TEXT(
				"This is a MATERIAL FUNCTION. It has no material outputs: take values in with FunctionInput expressions and return them with FunctionOutput expressions. "
				"Do not connect to \"Material.<output>\".");
		default:
			return TEXT("This is a generic Blueprint (not an Actor). Do not assume actor features such as components or ReceiveBeginPlay exist unless the Blueprint description shows them.");
		}
	}

	FString UserInstructions(EABGBlueprintKind Kind)
	{
		const UABGSettings* Settings = UABGSettings::Get();
		if (!Settings)
		{
			return FString();
		}
		switch (Kind)
		{
		case EABGBlueprintKind::Actor: return Settings->ActorInstructions;
		case EABGBlueprintKind::Component: return Settings->ComponentInstructions;
		case EABGBlueprintKind::Level: return Settings->LevelInstructions;
		case EABGBlueprintKind::Widget: return Settings->WidgetInstructions;
		case EABGBlueprintKind::Animation: return Settings->AnimationInstructions;
		case EABGBlueprintKind::FunctionLibrary:
		case EABGBlueprintKind::MacroLibrary: return Settings->LibraryInstructions;
		case EABGBlueprintKind::Interface: return Settings->InterfaceInstructions;
		case EABGBlueprintKind::Material:
		case EABGBlueprintKind::MaterialFunction: return Settings->MaterialInstructions;
		default: return Settings->OtherInstructions;
		}
	}
}
