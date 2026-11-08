#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ABGSettings.generated.h"

/** Editor Preferences -> Plugins -> AI Blueprint Generator */
UCLASS(config = EditorSettings)
class UABGSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Add the description of the target Blueprint (variables, components, functions, existing nodes) to the copied prompts. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt")
	bool bIncludeBlueprintContext = true;

	/** Upper limit for the Blueprint description inside a prompt (characters). Long Blueprints are cut with a note. 0 = no limit. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt", meta = (ClampMin = "0"))
	int32 ContextMaxChars = 16000;

	/** Send the worked examples with the prompt. Turn off to shorten the prompt (the format description stays). */
	UPROPERTY(config, EditAnywhere, Category = "Prompt")
	bool bIncludeExamples = true;

	/** Your own instructions added to EVERY prompt (e.g. naming rules, preferred answer language, style). */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString GlobalInstructions;

	/** Added to prompts for Actor Blueprints. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString ActorInstructions;

	/** Added to prompts for Actor Component Blueprints. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString ComponentInstructions;

	/** Added to prompts for the Level Blueprint. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString LevelInstructions;

	/** Added to prompts for Widget (UMG) Blueprints. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString WidgetInstructions;

	/** Added to prompts for Animation Blueprints. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString AnimationInstructions;

	/** Added to prompts for Function Libraries and Macro Libraries. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString LibraryInstructions;

	/** Added to prompts for Blueprint Interfaces. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString InterfaceInstructions;

	/** Added to prompts for Materials and Material Functions. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString MaterialInstructions;

	/** Folder suggested in the "new asset" windows, e.g. /Game/AI. */
	/** How many prompts / fix prompts / replies are kept in <Project>/Saved/AIBlueprintGen/History (the oldest are deleted). */
	UPROPERTY(config, EditAnywhere, Category = "History", meta = (ClampMin = "10", ClampMax = "1000"))
	int32 HistoryMaxEntries = 100;

	UPROPERTY(config, EditAnywhere, Category = "Prompt|New assets")
	FString DefaultCreateFolder = TEXT("/Game/AI");

	/** Added to every prompt that creates a NEW asset (naming rules, folder conventions, ...). */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|New assets", meta = (MultiLine = "true"))
	FString NewAssetInstructions;

	/** Added to prompts for any other kind of Blueprint. */
	UPROPERTY(config, EditAnywhere, Category = "Prompt|Instructions", meta = (MultiLine = "true"))
	FString OtherInstructions;

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("AIBlueprintGen"); }
	virtual FText GetSectionText() const override { return NSLOCTEXT("AIBlueprintGen", "SettingsSection", "AI Blueprint Generator"); }

	static const UABGSettings* Get();
	static UABGSettings* GetMutable();
};
