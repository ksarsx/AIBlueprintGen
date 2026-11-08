#include "UI/ABGBlueprintTabFactory.h"
#include "UI/SAIBlueprintGenPanel.h"

#include "BlueprintEditor.h"
#include "Engine/Blueprint.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ABGBlueprintTabFactory"

const FName FABGBlueprintTabFactory::TabId(TEXT("AIBlueprintGenBlueprintTab"));

FABGBlueprintTabFactory::FABGBlueprintTabFactory(TSharedPtr<FBlueprintEditor> InBlueprintEditor)
	: FWorkflowTabFactory(TabId, InBlueprintEditor)
	, BlueprintEditor(InBlueprintEditor)
{
	TabLabel = LOCTEXT("TabLabel", "AI Blueprint");
	bIsSingleton = true;
	ViewMenuDescription = LOCTEXT("ViewMenu", "AI Blueprint Generator");
	ViewMenuTooltip = LOCTEXT("ViewMenuTip", "Copy-paste AI workflow bound to this Blueprint");
}

TSharedRef<SWidget> FABGBlueprintTabFactory::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FBlueprintEditor> Editor = BlueprintEditor.Pin();
	UBlueprint* Blueprint = Editor.IsValid() ? Editor->GetBlueprintObj() : nullptr;
	if (!Blueprint)
	{
		return SNew(STextBlock).Text(LOCTEXT("NoBlueprint", "No Blueprint."));
	}
	return SNew(SAIBlueprintGenPanel).Asset(Blueprint);
}

FText FABGBlueprintTabFactory::GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const
{
	return ViewMenuTooltip;
}

#undef LOCTEXT_NAMESPACE
