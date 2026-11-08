#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FBlueprintEditor;

/**
 * Adds an "AI Blueprint Generator" tab to a Blueprint editor (Window menu of that editor).
 * The panel inside is bound to the Blueprint that editor is showing, so every open Blueprint can have its own window.
 */
class FABGBlueprintTabFactory : public FWorkflowTabFactory
{
public:
	static const FName TabId;

	explicit FABGBlueprintTabFactory(TSharedPtr<FBlueprintEditor> InBlueprintEditor);

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override;

private:
	TWeakPtr<FBlueprintEditor> BlueprintEditor;
};
