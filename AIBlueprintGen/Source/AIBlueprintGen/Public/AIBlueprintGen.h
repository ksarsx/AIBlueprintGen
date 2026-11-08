#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUICommandList;
class SDockTab;
class FSpawnTabArgs;
class FBlueprintEditor;
class FWorkflowAllowedTabSet;
class IMaterialEditor;
class UToolMenu;

class FAIBlueprintGenModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Opens (or focuses) the generator panel. */
	void OpenPanel();

	/** Opens a new "create an asset" window. Kind is an EABGCreateKind value. Any number of these windows can be open. */
	void OpenCreateWindow(uint8 Kind);

private:
	void RegisterMenus();
	/** Hooks into the Blueprint and Material editors. Runs after engine init: those modules need GEditor, which does not exist yet while this module starts. */
	void RegisterEditorHooks();
	/** Content of the top-level "AI Generator" menu of the level editor. */
	void FillAIMenu(UToolMenu* Menu);
	TSharedRef<SDockTab> OnSpawnPanelTab(const FSpawnTabArgs& SpawnTabArgs);
	/** Adds the per-Blueprint tab to every Blueprint editor that opens. */
	void RegisterBlueprintEditorTabs(FWorkflowAllowedTabSet& TabFactories, FName ModeName, TSharedPtr<FBlueprintEditor> BlueprintEditor);

	/** Adds the AI tab to the editor of a Material or Material Function that just opened. */
	void OnMaterialEditorOpened(TWeakPtr<IMaterialEditor> WeakEditor);

	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle BlueprintTabsHandle;
	FDelegateHandle MaterialOpenedHandle;
	FDelegateHandle MaterialFunctionOpenedHandle;

	TSharedPtr<FUICommandList> PluginCommands;
};
