#include "AIBlueprintGen.h"
#include "Core/AIBlueprintGenLog.h"
#include "Core/AIBlueprintGenStyle.h"
#include "Create/ABGCreateKind.h"
#include "Core/AIBlueprintGenCommands.h"
#include "UI/ABGBlueprintTabFactory.h"
#include "UI/SAIBlueprintGenPanel.h"

#include "BlueprintEditor.h"
#include "Editor.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "BlueprintEditorModule.h"
#include "IMaterialEditor.h"
#include "MaterialEditorModule.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

#include "ToolMenus.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY(LogAIBlueprintGen);

static const FName AIBlueprintGenTabName("AIBlueprintGenPanel");

#define LOCTEXT_NAMESPACE "FAIBlueprintGenModule"

void FAIBlueprintGenModule::StartupModule()
{
	FAIBlueprintGenStyle::Initialize();
	FAIBlueprintGenCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	PluginCommands->MapAction(
		FAIBlueprintGenCommands::Get().OpenPanel,
		FExecuteAction::CreateRaw(this, &FAIBlueprintGenModule::OpenPanel),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAIBlueprintGenModule::RegisterMenus));

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
			AIBlueprintGenTabName,
			FOnSpawnTab::CreateRaw(this, &FAIBlueprintGenModule::OnSpawnPanelTab))
		.SetDisplayName(LOCTEXT("PanelTabTitle", "AI Blueprint Generator"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	// The Blueprint / Material editor modules assert on GEditor in their startup: hook into them only after the engine is up.
	if (GEditor && GIsEditor && FApp::CanEverRender())
	{
		RegisterEditorHooks();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FAIBlueprintGenModule::RegisterEditorHooks);
	}

	UE_LOG(LogAIBlueprintGen, Log, TEXT("AIBlueprintGen module started."));
}

void FAIBlueprintGenModule::RegisterEditorHooks()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (!GEditor)
	{
		return;
	}

	// Every Blueprint editor (actor, component, level, widget, animation, libraries...) gets its own bound tab.
	FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
	BlueprintTabsHandle = BlueprintEditorModule.OnRegisterTabsForEditor().AddRaw(this, &FAIBlueprintGenModule::RegisterBlueprintEditorTabs);

	// Material and Material Function editors get the same kind of bound tab.
	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>("MaterialEditor");
	MaterialOpenedHandle = MaterialEditorModule.OnMaterialEditorOpened().AddRaw(this, &FAIBlueprintGenModule::OnMaterialEditorOpened);
	MaterialFunctionOpenedHandle = MaterialEditorModule.OnMaterialFunctionEditorOpened().AddRaw(this, &FAIBlueprintGenModule::OnMaterialEditorOpened);
}

void FAIBlueprintGenModule::ShutdownModule()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	if (BlueprintTabsHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("Kismet"))
	{
		FBlueprintEditorModule& BlueprintEditorModule = FModuleManager::GetModuleChecked<FBlueprintEditorModule>("Kismet");
		BlueprintEditorModule.OnRegisterTabsForEditor().Remove(BlueprintTabsHandle);
	}
	BlueprintTabsHandle.Reset();

	if (FModuleManager::Get().IsModuleLoaded("MaterialEditor"))
	{
		IMaterialEditorModule& MaterialEditorModule = FModuleManager::GetModuleChecked<IMaterialEditorModule>("MaterialEditor");
		MaterialEditorModule.OnMaterialEditorOpened().Remove(MaterialOpenedHandle);
		MaterialEditorModule.OnMaterialFunctionEditorOpened().Remove(MaterialFunctionOpenedHandle);
	}
	MaterialOpenedHandle.Reset();
	MaterialFunctionOpenedHandle.Reset();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AIBlueprintGenTabName);

	FAIBlueprintGenCommands::Unregister();
	FAIBlueprintGenStyle::Shutdown();
}

void FAIBlueprintGenModule::OpenPanel()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AIBlueprintGenTabName);
}

void FAIBlueprintGenModule::OpenCreateWindow(uint8 KindValue)
{
	const EABGCreateKind Kind = static_cast<EABGCreateKind>(KindValue);
	if (Kind == EABGCreateKind::None)
	{
		return;
	}

	// A separate window per click: several assets can be prepared side by side.
	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(FString::Printf(TEXT("AI Generator: %s"), *ABGCreateKind::Label(Kind))))
		.ClientSize(FVector2D(760.f, 860.f))
		.SupportsMinimize(true)
		.SupportsMaximize(true)
		[
			SNew(SAIBlueprintGenPanel).CreateKind(Kind)
		];

	const TSharedPtr<SWindow> ParentWindow = FGlobalTabmanager::Get()->GetRootWindow();
	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}
}

void FAIBlueprintGenModule::FillAIMenu(UToolMenu* Menu)
{
	FToolMenuSection& OpenSection = Menu->AddSection("AIGenOpen", LOCTEXT("AIMenuOpenSection", "Существующие ассеты"));
	OpenSection.AddMenuEntry(
		"AIGenOpenPanel",
		LOCTEXT("AIMenuOpenPanel", "Окно AI Blueprint Generator"),
		LOCTEXT("AIMenuOpenPanelTip", "Общее окно для открытого Blueprint или материала. У каждого редактора есть и своя вкладка в меню Window."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FAIBlueprintGenModule::OpenPanel)));

	FToolMenuSection& CreateSection = Menu->AddSection("AIGenCreate", LOCTEXT("AIMenuCreateSection", "Создать новый ассет"));
	const EABGCreateKind Kinds[] =
	{
		EABGCreateKind::Blueprint,
		EABGCreateKind::WidgetBlueprint,
		EABGCreateKind::Material,
		EABGCreateKind::MaterialFunction,
	};
	for (const EABGCreateKind Kind : Kinds)
	{
		CreateSection.AddMenuEntry(
			FName(*FString::Printf(TEXT("AIGenCreate_%s"), ABGCreateKind::TypeName(Kind))),
			FText::FromString(ABGCreateKind::Label(Kind)),
			LOCTEXT("AIMenuCreateTip", "Открывает окно: промпт для нейронки, затем вставка ответа создаёт ассет."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FAIBlueprintGenModule::OpenCreateWindow, static_cast<uint8>(Kind))));
	}
}

TSharedRef<SDockTab> FAIBlueprintGenModule::OnSpawnPanelTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SAIBlueprintGenPanel)
		];
}

void FAIBlueprintGenModule::RegisterBlueprintEditorTabs(FWorkflowAllowedTabSet& TabFactories, FName ModeName, TSharedPtr<FBlueprintEditor> BlueprintEditor)
{
	if (BlueprintEditor.IsValid())
	{
		TabFactories.RegisterFactory(MakeShared<FABGBlueprintTabFactory>(BlueprintEditor));
	}
}

void FAIBlueprintGenModule::OnMaterialEditorOpened(TWeakPtr<IMaterialEditor> WeakEditor)
{
	const TSharedPtr<IMaterialEditor> Editor = WeakEditor.Pin();
	if (!Editor.IsValid())
	{
		return;
	}

	// This event fires BEFORE the editor creates its tab manager and registers its tabs, so there is nothing to
	// register into yet. The editor announces every (re-)registration of its tab spawners: add our tab then.
	Editor->OnRegisterTabSpawners().AddLambda([WeakEditor](const TSharedRef<FTabManager>& InTabManager)
	{
		const TSharedPtr<IMaterialEditor> PinnedEditor = WeakEditor.Pin();
		if (!PinnedEditor.IsValid())
		{
			return;
		}
		// The first edited object is the saved asset (the others are the editor's transient preview copies).
		const TArray<UObject*>* EditedObjects = PinnedEditor->GetObjectsCurrentlyBeingEdited();
		UObject* Asset = (EditedObjects && EditedObjects->Num() > 0) ? (*EditedObjects)[0] : nullptr;
		if (!Asset)
		{
			Asset = PinnedEditor->GetMaterialInterface();
		}
		if (!Asset)
		{
			UE_LOG(LogAIBlueprintGen, Warning, TEXT("Material editor opened, but its asset is not known: the AI tab was not added."));
			return;
		}

		const TWeakObjectPtr<UObject> WeakAsset(Asset);
		static const FName MaterialTabId("AIBlueprintGenMaterialTab");
		InTabManager->UnregisterTabSpawner(MaterialTabId);
		InTabManager->RegisterTabSpawner(MaterialTabId, FOnSpawnTab::CreateLambda([WeakAsset](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.TabRole(ETabRole::PanelTab)
				.Label(LOCTEXT("MaterialTabLabel", "AI Material"))
				[
					SNew(SAIBlueprintGenPanel).Asset(WeakAsset)
				];
		}))
		.SetDisplayName(LOCTEXT("MaterialTabMenu", "AI Blueprint Generator"))
		.SetTooltipText(LOCTEXT("MaterialTabTip", "Copy-paste AI workflow bound to this material"))
		.SetGroup(InTabManager->GetLocalWorkspaceMenuRoot());
		UE_LOG(LogAIBlueprintGen, Log, TEXT("AI tab added to the editor of %s."), *Asset->GetName());
	});
}

void FAIBlueprintGenModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Window menu entry
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
		Section.AddMenuEntryWithCommandList(FAIBlueprintGenCommands::Get().OpenPanel, PluginCommands);
	}

	// Top-level "AI Generator" menu in the level editor's menu bar (placed before Help, like the engine's own menus)
	{
		UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
		FToolMenuSection& Section = MainMenu->FindOrAddSection(NAME_None);
		FToolMenuEntry& Entry = Section.AddSubMenu(
			"AIBlueprintGenMenu",
			LOCTEXT("TopMenuLabel", "AI Generator"),
			LOCTEXT("TopMenuTip", "Создание Blueprint, материалов и текстур через промпт для нейронки"),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateRaw(this, &FAIBlueprintGenModule::FillAIMenu)));
		Entry.InsertPosition = FToolMenuInsert("Help", EToolMenuInsertType::Before);
	}

	// Level editor toolbar button
	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
		FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FAIBlueprintGenCommands::Get().OpenPanel));
		Entry.SetCommandList(PluginCommands);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAIBlueprintGenModule, AIBlueprintGen)
