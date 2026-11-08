#pragma once

#include "CoreMinimal.h"
#include "Create/ABGCreateKind.h"
#include "Prompt/ABGBlueprintKind.h"

/** Builds the prompts sent to the model. (Phase 1 of the Prompt Builder: one fixed block; user-toggled blocks come later.) */
class FABGPromptBuilder
{
public:
	/**
	 * For pasting into any chat AI by hand: format description + the user's request.
	 * The AI is asked for a short explanation plus the JSON in one ```json block, which ExtractJson can pull out.
	 * BlueprintContext (optional) is the FABGBlueprintExporter dump of the target Blueprint.
	 * ProjectStructure (optional) is the FABGProjectScanner text: names of the assets that already exist in the project.
	 * Create (optional) turns the prompt into a "create a NEW asset" prompt: the AI also fills the JSON "asset" block.
	 */
	static FString BuildExternalPrompt(const FString& UserRequest, const FString& BlueprintContext = FString(),
		EABGBlueprintKind Kind = EABGBlueprintKind::Other, const FABGCreateRequest* Create = nullptr,
		const FString& ProjectStructure = FString());

	/**
	 * For pasting into a chat AI after a failed import: format + request + the rejected JSON + the plugin's errors.
	 * The AI is asked to answer in the same two-part format as BuildExternalPrompt.
	 */
	static FString BuildFixPrompt(const FString& UserRequest, const FString& RejectedJson,
		const TArray<FString>& Errors, const TArray<FString>& Warnings, const FString& BlueprintContext = FString(),
		EABGBlueprintKind Kind = EABGBlueprintKind::Other, const FABGCreateRequest* Create = nullptr,
		const FString& ProjectStructure = FString());

private:
	/** Format, node types, rules and examples (shared by both prompts). */
	static FString BuildFormatSpec(bool bIncludeExamples);

	/** Same for materials and material functions. */
	static FString BuildMaterialFormatSpec(bool bIncludeExamples);
};
