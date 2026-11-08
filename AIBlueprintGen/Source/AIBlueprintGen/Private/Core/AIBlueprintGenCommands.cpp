#include "Core/AIBlueprintGenCommands.h"

#define LOCTEXT_NAMESPACE "FAIBlueprintGenModule"

void FAIBlueprintGenCommands::RegisterCommands()
{
	UI_COMMAND(OpenPanel, "AI Blueprint", "Open the AI Blueprint Generator panel", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
