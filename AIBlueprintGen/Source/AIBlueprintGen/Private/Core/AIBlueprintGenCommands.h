#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "Core/AIBlueprintGenStyle.h"

class FAIBlueprintGenCommands : public TCommands<FAIBlueprintGenCommands>
{
public:
	FAIBlueprintGenCommands()
		: TCommands<FAIBlueprintGenCommands>(
			TEXT("AIBlueprintGen"),
			NSLOCTEXT("Contexts", "AIBlueprintGen", "AI Blueprint Generator"),
			NAME_None,
			FAIBlueprintGenStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenPanel;
};
