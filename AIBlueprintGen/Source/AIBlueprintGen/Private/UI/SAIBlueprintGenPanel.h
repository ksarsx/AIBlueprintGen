#pragma once

#include "CoreMinimal.h"
#include "Create/ABGCreateKind.h"
#include "Project/ABGProjectScanner.h"
#include "Prompt/ABGBlueprintKind.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SCompoundWidget.h"

class SMultiLineEditableTextBox;
class UBlueprint;
class UEdGraph;
class UMaterial;
class UMaterialFunction;
class UObject;
struct FABGBuildResult;

/**
 * Main dockable panel - a pure copy-paste workflow, no AI connections:
 *  1. Prompt box + "copy prompt for the AI" (format description + request + optional Blueprint description).
 *  2. Answer box: paste the whole AI reply, "Insert into Blueprint" extracts the JSON and builds the nodes.
 *  3. Errors box (shown only when something went wrong): reasons + a ready-made fix prompt with its own copy button.
 */
class SAIBlueprintGenPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAIBlueprintGenPanel)
		: _CreateKind(EABGCreateKind::None)
	{}
		/** When set, the panel is bound to this asset (Blueprint, Material or Material Function; a tab inside its editor). Otherwise it follows the single open editor. */
		SLATE_ARGUMENT(TWeakObjectPtr<UObject>, Asset)
		/** When not None, this is a "new asset" window: the answer creates a NEW asset of this kind instead of editing an open one. */
		SLATE_ARGUMENT(EABGCreateKind, CreateKind)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FReply OnCopyPromptClicked();
	FReply OnCopyBlueprintInfoClicked();
	FReply OnInsertAnswerClicked();
	FReply OnClearAnswerClicked();
	FReply OnArrangeClicked();
	FReply OnCopyFixPromptClicked();

	/** "New asset" window: parse the answer, import textures, create the asset, build inside it. Rolls the asset back on failure. */
	void ApplyCreate(const FString& Json, FABGBuildResult& Result, FString& OutPackage, int32& OutImported);
	FABGCreateRequest MakeCreateRequest() const;
	/** Project structure text for the prompt according to this window's own filters (empty when switched off). */
	FString BuildProjectStructure(FABGProjectScanResult& OutScan) const;

	/** Shows (or clears) the error box + ready-made fix prompt for the given import attempt. */
	void UpdateErrorPanel(const FString& Json, const FABGBuildResult& Result);

	// ---- which Blueprint will receive the nodes ----
	/** What the panel works on right now. */
	struct FTarget
	{
		UObject* Asset = nullptr;
		UBlueprint* Blueprint = nullptr;
		UEdGraph* Graph = nullptr;
		UMaterial* Material = nullptr;
		UMaterialFunction* Function = nullptr;
		EABGBlueprintKind Kind = EABGBlueprintKind::Other;
		bool IsMaterial() const { return Material != nullptr || Function != nullptr; }
	};

	/** The bound asset, or the single open Blueprint / Material / Material Function editor. */
	bool ResolveTarget(FTarget& OutTarget, FString& OutError) const;
	EABGBlueprintKind GetTargetKind() const;
	/** Short name of the asset (or of the asset being created) for the history file names. */
	FString HistoryTarget() const;
	void RefreshTargetCache() const;
	FText GetTargetBlueprintText() const;
	FSlateColor GetTargetBlueprintColor() const;
	/** Exports the target Blueprint or material (empty string when none is open). */
	FString BuildBlueprintContext() const;

	void SetStatus(const FString& Text);

	TSharedPtr<SMultiLineEditableTextBox> PromptBox;
	TSharedPtr<SMultiLineEditableTextBox> AnswerBox;
	TSharedPtr<SMultiLineEditableTextBox> ErrorBox;
	bool bHasErrors = false;
	/** The ready-to-send prompt (without the reasons summary shown above it in the box). */
	FString FixPromptText;

	mutable FText CachedTargetText;
	mutable bool bCachedTargetOk = false;
	mutable double LastTargetCheckTime = -1000.0;

	TWeakObjectPtr<UObject> BoundAsset;
	bool bBound = false;

	/** What this window tells the AI about the project (each window has its own). */
	FABGProjectScope ProjectScope;

	// ---- "new asset" window ----
	EABGCreateKind CreateKind = EABGCreateKind::None;
	FString CreateName;
	FString CreateFolder;
	FString CreateParent;
	/** The asset made by the last attempt that built but did not compile: a retry with the same name replaces it. */
	TWeakObjectPtr<UObject> LastCreatedAsset;
	FString LastCreatedPackage;

	FText StatusText;
};
