#include "UI/SAIBlueprintGenPanel.h"
#include "ABGSettings.h"
#include "Core/AIBlueprintGenLog.h"
#include "Core/ABGHistory.h"
#include "Create/ABGAssetCreator.h"
#include "Export/ABGBlueprintExporter.h"
#include "Export/ABGMaterialExporter.h"
#include "Graph/ABGGraphBuilder.h"
#include "Material/ABGMaterialBuilder.h"
#include "Project/ABGProjectScanner.h"
#include "Prompt/ABGPromptBuilder.h"
#include "Schema/ABGCreateJson.h"
#include "Schema/ABGGraphJson.h"
#include "Schema/ABGMaterialJson.h"

#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAIBlueprintGenPanel"

namespace
{
	/** A titled group: bold header, hint line, content. */
	TSharedRef<SWidget> MakeSection(const FText& Title, const FText& Hint, const TSharedRef<SWidget>& Content,
		TAttribute<EVisibility> Visibility = EVisibility::Visible)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.f)
			.Visibility(Visibility)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Title).Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
				[
					SNew(STextBlock).Text(Hint).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					Content
				]
			];
	}
}

void SAIBlueprintGenPanel::Construct(const FArguments& InArgs)
{
	BoundAsset = InArgs._Asset;
	bBound = BoundAsset.IsValid();
	CreateKind = InArgs._CreateKind;
	const bool bCreate = CreateKind != EABGCreateKind::None;
	if (bCreate)
	{
		bBound = false;
		const UABGSettings* DefaultSettings = UABGSettings::Get();
		CreateFolder = DefaultSettings ? DefaultSettings->DefaultCreateFolder : FString(TEXT("/Game/AI"));
	}
	StatusText = bCreate ? LOCTEXT("StatusReadyCreate", "Готово. Опишите, что создать, скопируйте промпт, вставьте ответ нейронки и нажмите «Создать».")
		: bBound ? LOCTEXT("StatusReadyBound", "Готово. Окно привязано к этому ассету.")
		: LOCTEXT("StatusReady", "Готово. Откройте Blueprint или материал (в материале сначала нажмите Apply).");

	// Existing-asset header: target label + "include description" checkbox + copy button.
	const TSharedRef<SWidget> ExistingHeader =
			SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(this, &SAIBlueprintGenPanel::GetTargetBlueprintText)
			.ColorAndOpacity(this, &SAIBlueprintGenPanel::GetTargetBlueprintColor)
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([]()
				{
					const UABGSettings* Settings = UABGSettings::Get();
					return (Settings && Settings->bIncludeBlueprintContext) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([](ECheckBoxState NewState)
				{
					if (UABGSettings* Settings = UABGSettings::GetMutable())
					{
						Settings->bIncludeBlueprintContext = (NewState == ECheckBoxState::Checked);
						Settings->SaveConfig();
					}
				})
				.ToolTipText(LOCTEXT("ContextTip", "Добавлять в копируемый промпт описание Blueprint: переменные, компоненты, функции и ноды (с id). Нужно, чтобы нейронка могла менять существующее."))
				[
					SNew(STextBlock).Text(LOCTEXT("IncludeContext", "Добавлять описание Blueprint / материала в промпт"))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("CopyBpInfo", "Скопировать описание"))
				.ToolTipText(LOCTEXT("CopyBpInfoTip", "Копирует всю информацию о Blueprint (переменные, компоненты, функции, все графы и ноды) в буфер обмена."))
				.OnClicked(this, &SAIBlueprintGenPanel::OnCopyBlueprintInfoClicked)
			]
		];

	// "New asset" header: kind, name, folder, parent class.
	auto MakeField = [](const FText& Label, const FText& Hint, FString* Value) -> TSharedRef<SWidget>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox).WidthOverride(150.f)
				[
					SNew(STextBlock).Text(Label)
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([Value]() { return FText::FromString(*Value); })
				.HintText(Hint)
				.OnTextChanged_Lambda([Value](const FText& NewText) { *Value = NewText.ToString(); })
			];
	};
	const bool bBlueprintCreate = bCreate && !ABGCreateKind::IsMaterial(CreateKind);
	const TSharedRef<SWidget> CreateHeader = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(ABGCreateKind::Label(CreateKind)))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 6.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("CreateHint", "Ассет будет создан по ответу нейронки. Поля можно оставить пустыми - тогда имя, папку и родительский класс выберет нейронка (блок \"asset\" в JSON). Заполненные поля главнее JSON."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeField(LOCTEXT("FieldName", "Имя ассета"), LOCTEXT("FieldNameHint", "например BP_Door"), &CreateName)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			MakeField(LOCTEXT("FieldFolder", "Папка"), LOCTEXT("FieldFolderHint", "/Game/AI"), &CreateFolder)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SBox)
			.Visibility(bBlueprintCreate ? EVisibility::Visible : EVisibility::Collapsed)
			[
				MakeField(LOCTEXT("FieldParent", "Родительский класс"), LOCTEXT("FieldParentHint", "Actor, Pawn, Character, ActorComponent..."), &CreateParent)
			]
		];

	// ---- "project structure in the prompt": own filters for every window ----
	auto MakeCheck = [](const FText& Label, bool* Flag) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([Flag]() { return *Flag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Flag](ECheckBoxState NewState) { *Flag = (NewState == ECheckBoxState::Checked); })
			[
				SNew(STextBlock).Text(Label)
			];
	};
	const TSharedRef<SWidget> ProjectBody = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCheck(LOCTEXT("ProjEnable", "Добавлять структуру проекта в промпт"), &ProjectScope.bEnabled)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return ProjectScope.bEnabled ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
				[
					SNew(STextBlock).Text(LOCTEXT("ProjWhere", "Откуда брать:")).Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 16.f, 0.f)
					[
						SNew(SCheckBox)
						.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("RadioButton"))
						.IsChecked_Lambda([this]() { return ProjectScope.bFolderOnly ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState) { ProjectScope.bFolderOnly = false; })
						[
							SNew(STextBlock).Text(LOCTEXT("ProjWhole", "Весь проект (/Game)"))
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SCheckBox)
						.Style(&FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>("RadioButton"))
						.IsChecked_Lambda([this]() { return ProjectScope.bFolderOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
						.OnCheckStateChanged_Lambda([this](ECheckBoxState) { ProjectScope.bFolderOnly = true; })
						[
							SNew(STextBlock).Text(LOCTEXT("ProjFolderOnly", "Только папка"))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return ProjectScope.bFolderOnly ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeField(LOCTEXT("ProjFolder", "Папка"), LOCTEXT("ProjFolderHint", "/Game/Textures"), &ProjectScope.Folder)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)
						[
							MakeCheck(LOCTEXT("ProjRecursive", "Включая подпапки"), &ProjectScope.bRecursive)
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
				[
					SNew(STextBlock).Text(LOCTEXT("ProjTypes", "Какие типы показывать:")).Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SUniformGridPanel).SlotPadding(FMargin(0.f, 1.f, 8.f, 1.f))
					+ SUniformGridPanel::Slot(0, 0)[ MakeCheck(LOCTEXT("ProjTex", "Текстуры"), &ProjectScope.bTextures) ]
					+ SUniformGridPanel::Slot(1, 0)[ MakeCheck(LOCTEXT("ProjMat", "Материалы"), &ProjectScope.bMaterials) ]
					+ SUniformGridPanel::Slot(2, 0)[ MakeCheck(LOCTEXT("ProjBp", "Blueprint"), &ProjectScope.bBlueprints) ]
					+ SUniformGridPanel::Slot(0, 1)[ MakeCheck(LOCTEXT("ProjMesh", "Меши"), &ProjectScope.bMeshes) ]
					+ SUniformGridPanel::Slot(1, 1)[ MakeCheck(LOCTEXT("ProjAnim", "Анимации"), &ProjectScope.bAnimations) ]
					+ SUniformGridPanel::Slot(2, 1)[ MakeCheck(LOCTEXT("ProjSound", "Звуки"), &ProjectScope.bSounds) ]
					+ SUniformGridPanel::Slot(0, 2)[ MakeCheck(LOCTEXT("ProjWidget", "Виджеты"), &ProjectScope.bWidgets) ]
					+ SUniformGridPanel::Slot(1, 2)[ MakeCheck(LOCTEXT("ProjOther", "Прочее"), &ProjectScope.bOther) ]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)
				[
					SNew(STextBlock).Text(LOCTEXT("ProjExtra", "Дополнительно:")).Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeCheck(LOCTEXT("ProjCpp", "C++ классы проекта (можно брать родителем для Blueprint)"), &ProjectScope.bCppClasses)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeCheck(LOCTEXT("ProjPlugins", "Содержимое и классы плагинов проекта (при режиме «весь проект»)"), &ProjectScope.bPluginContent)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("ProjLimit", "Лимит, символов:"))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SBox).WidthOverride(110.f)
						[
							SNew(SSpinBox<int32>)
							.MinValue(1000)
							.MaxValue(200000)
							.MinSliderValue(1000)
							.MaxSliderValue(60000)
							.Value_Lambda([this]() { return ProjectScope.MaxChars; })
							.OnValueChanged_Lambda([this](int32 NewValue) { ProjectScope.MaxChars = NewValue; })
						]
					]
				]
			]
		];
	const TSharedRef<SWidget> ProjectSection = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(true)
			.AreaTitle(LOCTEXT("ProjTitle", "Структура проекта в промпте (необязательно)"))
			.BodyContent()
			[
				ProjectBody
			]
		];

	ChildSlot
	[
		SNew(SScrollBox)

		// ---- target Blueprint + its description, or the "new asset" fields ----
		+ SScrollBox::Slot().Padding(8.f, 8.f, 8.f, 4.f)
		[
			bCreate ? CreateHeader : ExistingHeader
		]

		// ---- project structure for the prompt ----
		+ SScrollBox::Slot().Padding(8.f, 4.f)
		[
			ProjectSection
		]

		// ---- 1. prompt ----
		+ SScrollBox::Slot().Padding(8.f, 4.f)
		[
			MakeSection(
				LOCTEXT("PromptTitle", "1. Запрос"),
				bCreate ? LOCTEXT("PromptHintCreate", "Опишите, что должно быть в новом ассете (и пути к файлам текстур, если нужны). Кнопка копирует полный промпт (формат + запрос) - вставьте его в любую нейронку.")
					: LOCTEXT("PromptHint", "Опишите, что должен делать Blueprint. Кнопка копирует полный промпт (формат + запрос + описание Blueprint) - вставьте его в любую нейронку."),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(110.f)
					[
						SAssignNew(PromptBox, SMultiLineEditableTextBox)
						.AutoWrapText(true)
						.HintText(LOCTEXT("PromptHintText", "Например: при старте игры заспавни куб и медленно вращай его"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(LOCTEXT("CopyPrompt", "Скопировать промпт для нейронки"))
						.OnClicked(this, &SAIBlueprintGenPanel::OnCopyPromptClicked)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ArrangeNodes", "Разложить ноды"))
						.ToolTipText(LOCTEXT("ArrangeNodesTip", "Раскладывает ВСЕ ноды материала по колонкам вдоль проводов (меняются только позиции). Для случая, когда ноды слиплись в одну кучу."))
						.Visibility_Lambda([this]()
						{
							const UObject* AssetObject = BoundAsset.Get();
							return (CreateKind == EABGCreateKind::None && (Cast<UMaterial>(AssetObject) || Cast<UMaterialFunction>(AssetObject)))
								? EVisibility::Visible : EVisibility::Collapsed;
						})
						.OnClicked(this, &SAIBlueprintGenPanel::OnArrangeClicked)
					]
				])
		]

		// ---- 2. answer ----
		+ SScrollBox::Slot().Padding(8.f, 4.f)
		[
			MakeSection(
				LOCTEXT("AnswerTitle", "2. Ответ нейронки"),
				bCreate ? LOCTEXT("AnswerHintCreate", "Вставьте ответ целиком (с пояснением): плагин сам найдёт JSON. Затем нажмите кнопку - ассет будет создан, текстуры импортированы, редактор откроется.")
					: LOCTEXT("AnswerHint", "Вставьте ответ целиком (с пояснением): плагин сам найдёт JSON. Затем нажмите кнопку - ноды появятся в открытом Blueprint."),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(200.f)
					[
						SAssignNew(AnswerBox, SMultiLineEditableTextBox)
						.AutoWrapText(false)
						.HintText(LOCTEXT("AnswerHintText", "Сюда - ответ нейронки"))
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.Text(bCreate ? LOCTEXT("InsertAnswerCreate", "Создать ассет") : LOCTEXT("InsertAnswer", "Вставить в Blueprint"))
						.OnClicked(this, &SAIBlueprintGenPanel::OnInsertAnswerClicked)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearAnswer", "Очистить"))
						.OnClicked(this, &SAIBlueprintGenPanel::OnClearAnswerClicked)
					]
				])
		]

		// ---- 3. errors ----
		+ SScrollBox::Slot().Padding(8.f, 4.f)
		[
			MakeSection(
				LOCTEXT("ErrorTitle", "3. Ошибки"),
				LOCTEXT("ErrorHint", "Причины, почему не получилось, и готовый промпт для исправления. Кнопка копирует только промпт."),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(220.f)
					[
						SAssignNew(ErrorBox, SMultiLineEditableTextBox)
						.IsReadOnly(true)
						.AutoWrapText(true)
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("CopyFixPrompt", "Скопировать промпт с ошибками"))
					.OnClicked(this, &SAIBlueprintGenPanel::OnCopyFixPromptClicked)
				],
				TAttribute<EVisibility>::CreateLambda([this]() { return bHasErrors ? EVisibility::Visible : EVisibility::Collapsed; }))
		]

		// ---- status ----
		+ SScrollBox::Slot().Padding(8.f, 4.f, 8.f, 8.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]() { return StatusText; })
			.AutoWrapText(true)
		]
	];
}

void SAIBlueprintGenPanel::SetStatus(const FString& Text)
{
	StatusText = FText::FromString(Text);
}

bool SAIBlueprintGenPanel::ResolveTarget(FTarget& OutTarget, FString& OutError) const
{
	OutTarget = FTarget();

	UObject* Asset = nullptr;
	if (bBound)
	{
		Asset = BoundAsset.Get();
		if (!Asset)
		{
			OutError = TEXT("Ассет этого окна закрыт или удалён.");
			return false;
		}
	}
	else
	{
		UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
		if (!AssetEditors)
		{
			OutError = TEXT("Asset editor subsystem is not available.");
			return false;
		}
		TArray<UObject*> Candidates;
		for (UObject* Edited : AssetEditors->GetAllEditedAssets())
		{
			// The material editor also lists its transient preview copy: skip everything that is not a saved asset.
			if (!Edited || Edited->GetOutermost() == GetTransientPackage())
			{
				continue;
			}
			if (Cast<UBlueprint>(Edited) || Cast<UMaterial>(Edited) || Cast<UMaterialFunction>(Edited))
			{
				Candidates.Add(Edited);
			}
		}
		if (Candidates.Num() == 0)
		{
			OutError = TEXT("Не открыт ни один Blueprint или материал. Откройте его и кликните в граф.");
			return false;
		}
		if (Candidates.Num() > 1)
		{
			OutError = TEXT("Открыто несколько Blueprint/материалов. Закройте лишние или откройте окно плагина из меню Window нужного редактора.");
			return false;
		}
		Asset = Candidates[0];
	}

	OutTarget.Asset = Asset;
	OutTarget.Kind = ABGBlueprintKind::DetectAsset(Asset);
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		OutTarget.Blueprint = Blueprint;
		return FABGGraphBuilder::FindTargetGraphFor(Blueprint, OutTarget.Graph, OutError);
	}
	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		OutTarget.Material = Material;
		return true;
	}
	if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
	{
		OutTarget.Function = Function;
		return true;
	}
	OutError = TEXT("Этот вид ассета плагин не поддерживает.");
	return false;
}

EABGBlueprintKind SAIBlueprintGenPanel::GetTargetKind() const
{
	if (CreateKind != EABGCreateKind::None)
	{
		return ABGCreateKind::ToPromptKind(CreateKind, CreateParent);
	}
	FTarget Target;
	FString Error;
	return ResolveTarget(Target, Error) ? Target.Kind : EABGBlueprintKind::Other;
}

void SAIBlueprintGenPanel::RefreshTargetCache() const
{
	// Slate polls these attributes every frame; the lookup only needs to run a couple of times per second.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastTargetCheckTime < 0.4)
	{
		return;
	}
	LastTargetCheckTime = Now;

	FTarget Target;
	FString Error;
	if (ResolveTarget(Target, Error))
	{
		bCachedTargetOk = true;
		const FString Where = Target.Graph ? FString::Printf(TEXT("  ›  граф: %s"), *Target.Graph->GetName()) : FString();
		CachedTargetText = FText::FromString(FString::Printf(TEXT("%s: %s%s  ·  %s"),
			bBound ? TEXT("Ассет окна") : TEXT("Цель"),
			*Target.Asset->GetName(), *Where, *ABGBlueprintKind::Label(Target.Kind)));
	}
	else
	{
		bCachedTargetOk = false;
		CachedTargetText = FText::FromString(TEXT("Цель не выбрана. ") + Error);
	}
}

FText SAIBlueprintGenPanel::GetTargetBlueprintText() const
{
	RefreshTargetCache();
	return CachedTargetText;
}

FSlateColor SAIBlueprintGenPanel::GetTargetBlueprintColor() const
{
	RefreshTargetCache();
	return bCachedTargetOk ? FSlateColor(FLinearColor(0.35f, 0.85f, 0.45f)) : FSlateColor(FLinearColor(1.f, 0.7f, 0.25f));
}

FABGCreateRequest SAIBlueprintGenPanel::MakeCreateRequest() const
{
	FABGCreateRequest Request;
	Request.Kind = CreateKind;
	Request.Name = CreateName.TrimStartAndEnd();
	Request.Folder = CreateFolder.TrimStartAndEnd();
	Request.ParentClass = CreateParent.TrimStartAndEnd();
	return Request;
}

FString SAIBlueprintGenPanel::BuildProjectStructure(FABGProjectScanResult& OutScan) const
{
	OutScan = FABGProjectScanner::Scan(ProjectScope);
	return OutScan.Text;
}

FString SAIBlueprintGenPanel::BuildBlueprintContext() const
{
	if (CreateKind != EABGCreateKind::None)
	{
		return FString(); // nothing exists yet
	}
	FTarget Target;
	FString Error;
	if (!ResolveTarget(Target, Error))
	{
		return FString();
	}
	const UABGSettings* Settings = UABGSettings::Get();
	const int32 MaxChars = Settings ? Settings->ContextMaxChars : 0;
	if (Target.IsMaterial())
	{
		return FABGMaterialExporter::Export(Target.Material, Target.Function, MaxChars);
	}
	return FABGBlueprintExporter::Export(Target.Blueprint, Target.Graph, MaxChars);
}

FReply SAIBlueprintGenPanel::OnCopyBlueprintInfoClicked()
{
	const FString Info = BuildBlueprintContext();
	if (Info.IsEmpty())
	{
		FTarget Target;
		FString Error;
		ResolveTarget(Target, Error);
		SetStatus(TEXT("Нечего копировать. ") + Error);
		return FReply::Handled();
	}
	FPlatformApplicationMisc::ClipboardCopy(*Info);
	SetStatus(FString::Printf(TEXT("Описание скопировано (%d символов).%s"), Info.Len(),
		ABGBlueprintKind::IsMaterialKind(GetTargetKind()) ? TEXT(" Это сохранённое состояние материала: нажмите Apply в редакторе, чтобы в описание попали последние правки.") : TEXT("")));
	return FReply::Handled();
}

FString SAIBlueprintGenPanel::HistoryTarget() const
{
	if (CreateKind != EABGCreateKind::None)
	{
		return FString::Printf(TEXT("new_%s"), *CreateName);
	}
	const UObject* AssetObject = BoundAsset.Get();
	return AssetObject ? AssetObject->GetName() : FString();
}

FReply SAIBlueprintGenPanel::OnCopyPromptClicked()
{
	const FString Request = PromptBox.IsValid() ? PromptBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (Request.IsEmpty())
	{
		SetStatus(TEXT("Сначала напишите запрос в поле 1."));
		return FReply::Handled();
	}

	const UABGSettings* Settings = UABGSettings::Get();
	const bool bCreateMode = CreateKind != EABGCreateKind::None;
	const bool bWithContext = !bCreateMode && Settings && Settings->bIncludeBlueprintContext;
	const FString Context = bWithContext ? BuildBlueprintContext() : FString();

	const FABGCreateRequest CreateRequest = MakeCreateRequest();
	FABGProjectScanResult Scan;
	const FString ProjectStructure = BuildProjectStructure(Scan);
	const FString Prompt = FABGPromptBuilder::BuildExternalPrompt(Request, Context, GetTargetKind(), bCreateMode ? &CreateRequest : nullptr, ProjectStructure);
	FPlatformApplicationMisc::ClipboardCopy(*Prompt);
	FABGHistory::Add(TEXT("prompt"), HistoryTarget(), Prompt);
	FString ProjectInfo;
	if (ProjectScope.bEnabled)
	{
		ProjectInfo = FString::Printf(TEXT("; структура проекта: %d из %d"), Scan.Shown, Scan.Total);
	}
	SetStatus(FString::Printf(TEXT("Промпт скопирован (%d символов%s%s). Вставьте его в чат с ИИ, а ответ - в поле 2."),
		Prompt.Len(),
		bWithContext ? (Context.IsEmpty() ? TEXT(", БЕЗ описания Blueprint - он не выбран") : TEXT(", с описанием Blueprint")) : TEXT(""),
		*ProjectInfo));
	return FReply::Handled();
}

FReply SAIBlueprintGenPanel::OnArrangeClicked()
{
	FTarget Target;
	FString TargetError;
	if (!ResolveTarget(Target, TargetError) || !Target.IsMaterial())
	{
		SetStatus(TargetError.IsEmpty() ? FString(TEXT("Разложить можно только материал или Material Function.")) : TargetError);
		return FReply::Handled();
	}
	const int32 Moved = FABGMaterialBuilder::Arrange(Target.Material, Target.Function);
	SetStatus(FString::Printf(TEXT("Разложено нод: %d. Не сохранено: Ctrl+S / Apply в редакторе материала. Отмена: Ctrl+Z."), Moved));
	return FReply::Handled();
}

FReply SAIBlueprintGenPanel::OnClearAnswerClicked()
{
	if (AnswerBox.IsValid())
	{
		AnswerBox->SetText(FText::GetEmpty());
	}
	return FReply::Handled();
}

void SAIBlueprintGenPanel::UpdateErrorPanel(const FString& Json, const FABGBuildResult& Result)
{
	TArray<FString> Errors = Result.Errors;
	Errors.Append(Result.CompileErrors);
	if (Result.bSuccess && !Result.bCompiled)
	{
		Errors.Insert(ABGBlueprintKind::IsMaterialKind(GetTargetKind())
			? TEXT("The expressions were created, but the material does not compile. Typical causes: a value type mismatch between connected pins, a required input left empty, or a texture sampler type that does not match the texture.")
			: TEXT("The nodes were created, but the Blueprint does not compile. Typical causes: a function that needs a Target object is not connected to one, a required pin is empty, or a pin type mismatch."), 0);
	}

	bHasErrors = !Result.bSuccess || !Result.bCompiled || Result.Notes.Num() > 0;
	if (!ErrorBox.IsValid())
	{
		return;
	}
	if (!bHasErrors)
	{
		FixPromptText.Empty();
		ErrorBox->SetText(FText::GetEmpty());
		return;
	}

	// What the model said it could not do goes to the AI as a problem to solve as well.
	TArray<FString> PromptErrors = Errors;
	for (const FString& Note : Result.Notes)
	{
		PromptErrors.Add(TEXT("Not done (you reported this yourself): ") + Note);
	}

	const FString Request = PromptBox.IsValid() ? PromptBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FABGCreateRequest CreateRequest = MakeCreateRequest();
	FABGProjectScanResult Scan;
	const FString ProjectStructure = BuildProjectStructure(Scan);
	FixPromptText = FABGPromptBuilder::BuildFixPrompt(Request, Json, PromptErrors, Result.Warnings, BuildBlueprintContext(), GetTargetKind(),
		CreateKind != EABGCreateKind::None ? &CreateRequest : nullptr, ProjectStructure);

	FABGHistory::Add(TEXT("fixprompt"), HistoryTarget(), FixPromptText);

	// Human-readable reasons first, then the prompt.
	FString Reasons;
	if (Errors.Num() > 0)
	{
		Reasons += TEXT("ПОЧЕМУ НЕ ПОЛУЧИЛОСЬ:\n");
		for (const FString& Error : Errors)
		{
			Reasons += TEXT("- ") + Error + TEXT("\n");
		}
	}
	if (Result.Notes.Num() > 0)
	{
		Reasons += TEXT("МОДЕЛЬ СООБЩИЛА, ЧТО ЭТО НЕВОЗМОЖНО СДЕЛАТЬ:\n");
		for (const FString& Note : Result.Notes)
		{
			Reasons += TEXT("- ") + Note + TEXT("\n");
		}
	}
	if (Result.Warnings.Num() > 0)
	{
		Reasons += TEXT("ПРЕДУПРЕЖДЕНИЯ:\n");
		for (const FString& Warning : Result.Warnings)
		{
			Reasons += TEXT("- ") + Warning + TEXT("\n");
		}
	}
	Reasons += TEXT("\n----- ПРОМПТ ДЛЯ НЕЙРОНКИ (копируется кнопкой ниже) -----\n");
	ErrorBox->SetText(FText::FromString(Reasons + FixPromptText));
}

FReply SAIBlueprintGenPanel::OnCopyFixPromptClicked()
{
	if (!FixPromptText.IsEmpty())
	{
		const FString Text = FixPromptText;
		FPlatformApplicationMisc::ClipboardCopy(*Text);
		SetStatus(FString::Printf(TEXT("Промпт с ошибками скопирован (%d символов). Отправьте его нейронке, исправленный ответ вставьте в поле 2."), Text.Len()));
	}
	return FReply::Handled();
}

void SAIBlueprintGenPanel::ApplyCreate(const FString& Json, FABGBuildResult& Result, FString& OutPackage, int32& OutImported)
{
	FABGCreateSpec CreateSpec;
	if (!FABGCreateJson::Parse(Json, CreateSpec, Result.Errors))
	{
		return;
	}
	const FABGAssetSpec& AssetSpec = CreateSpec.Asset;
	const bool bMaterial = ABGCreateKind::IsMaterial(CreateKind);

	// Window fields win over the JSON "asset" block.
	const FString Name = !CreateName.TrimStartAndEnd().IsEmpty() ? CreateName.TrimStartAndEnd() : AssetSpec.Name;
	if (Name.IsEmpty())
	{
		Result.Errors.Add(TEXT("The asset has no name: type one into the window or put it into \"asset\": { \"name\": ... } in the JSON."));
		return;
	}
	if (!FABGCreateJson::IsValidAssetName(Name))
	{
		Result.Errors.Add(FString::Printf(TEXT("\"%s\" is not a valid asset name: use letters, digits and underscores only, not starting with a digit."), *Name));
		return;
	}
	FString Folder, FolderError;
	if (!FABGAssetCreator::NormalizeFolder(!CreateFolder.TrimStartAndEnd().IsEmpty() ? CreateFolder : AssetSpec.Path, Folder, FolderError))
	{
		Result.Errors.Add(FolderError);
		return;
	}
	UClass* Parent = nullptr;
	if (!bMaterial)
	{
		FString ParentError;
		Parent = FABGAssetCreator::ResolveParentClass(CreateKind, !CreateParent.TrimStartAndEnd().IsEmpty() ? CreateParent : AssetSpec.ParentClass, ParentError);
		if (!Parent)
		{
			Result.Errors.Add(ParentError);
			return;
		}
	}

	// Check the content BEFORE anything is created: a bad answer must not leave assets behind.
	FABGGraphSpec GraphSpec;
	FABGMaterialSpec MaterialSpec;
	if (bMaterial ? !FABGMaterialJson::Parse(Json, MaterialSpec, Result.Errors) : !FABGGraphJson::Parse(Json, GraphSpec, Result.Errors))
	{
		return;
	}

	const FString PackageName = FABGAssetCreator::MakePackageName(Folder, Name);
	// The previous attempt of this window built but did not compile: a retry with the same name replaces it.
	if (LastCreatedAsset.IsValid() && LastCreatedPackage == PackageName)
	{
		if (!FABGAssetCreator::DeleteAsset(LastCreatedAsset.Get()))
		{
			Result.Errors.Add(FString::Printf(TEXT("Could not remove the previous attempt %s. Delete it in the Content Browser and try again."), *PackageName));
			return;
		}
	}
	LastCreatedAsset.Reset();
	LastCreatedPackage.Empty();
	if (FABGAssetCreator::AssetExists(PackageName))
	{
		Result.Errors.Add(FString::Printf(TEXT("An asset %s already exists. Nothing was changed. Choose another name or folder."), *PackageName));
		return;
	}

	// Textures first, so that a missing image file leaves no empty asset behind.
	TArray<FString> ImportErrors;
	TArray<FString> ImportWarnings;
	FABGAssetCreator::ImportTextures(CreateSpec.Textures, Folder, ImportErrors, ImportWarnings, OutImported);
	if (ImportErrors.Num() > 0)
	{
		Result.Errors.Append(ImportErrors);
		return;
	}
	if (bMaterial)
	{
		FABGAssetCreator::ApplyTextureAliases(MaterialSpec, CreateSpec.Textures, Folder);
	}

	FString CreateError;
	UObject* Asset = FABGAssetCreator::CreateAsset(CreateKind, Name, Folder, Parent, CreateError);
	if (!Asset)
	{
		Result.Errors.Add(CreateError);
		return;
	}

	FABGBuildResult Built;
	if (bMaterial)
	{
		Built = FABGMaterialBuilder::Build(Cast<UMaterial>(Asset), Cast<UMaterialFunction>(Asset), MaterialSpec);
	}
	else
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		UEdGraph* Graph = nullptr;
		FString GraphError;
		if (!Blueprint || !FABGGraphBuilder::FindTargetGraphFor(Blueprint, Graph, GraphError))
		{
			Built.Errors.Add(GraphError.IsEmpty() ? FString(TEXT("The new Blueprint has no graph to build in.")) : GraphError);
		}
		else
		{
			Built = FABGGraphBuilder::Build(Blueprint, Graph, GraphSpec);
		}
	}
	Built.Warnings.Append(ImportWarnings);
	if (!AssetSpec.Type.IsEmpty() && !AssetSpec.Type.Equals(ABGCreateKind::TypeName(CreateKind), ESearchCase::IgnoreCase))
	{
		Built.Warnings.Add(FString::Printf(TEXT("\"asset.type\" is \"%s\", this window creates \"%s\"; the window's kind was used."), *AssetSpec.Type, ABGCreateKind::TypeName(CreateKind)));
	}

	if (!Built.bSuccess)
	{
		// All or nothing: no half-built asset stays in the project.
		if (!FABGAssetCreator::DeleteAsset(Asset))
		{
			Built.Warnings.Add(FString::Printf(TEXT("The empty asset %s could not be removed automatically: delete it in the Content Browser."), *PackageName));
		}
	}
	else
	{
		OutPackage = PackageName;
		if (!Built.bCompiled)
		{
			LastCreatedAsset = Asset;
			LastCreatedPackage = PackageName;
		}
		FABGAssetCreator::OpenEditor(Asset);
	}
	Result = MoveTemp(Built);
}

FReply SAIBlueprintGenPanel::OnInsertAnswerClicked()
{
	// The box may hold a whole chat reply (prose + a ```json block): pull the graph out.
	const FString RawAnswer = AnswerBox.IsValid() ? AnswerBox->GetText().ToString() : FString();
	const FString Json = FABGGraphJson::ExtractJson(RawAnswer);

	FABGBuildResult Result;
	FString CreatedPackage;
	int32 ImportedTextures = 0;
	bool bMaterialTarget = false;

	if (CreateKind != EABGCreateKind::None)
	{
		if (!Json.Contains(TEXT("{")))
		{
			Result.Errors.Add(FString::Printf(TEXT("No JSON found in the pasted text. If this is an AI reply, it may be explaining why the task cannot be done. The text: %s"),
				*Json.TrimStartAndEnd().Left(1500)));
		}
		else
		{
			ApplyCreate(Json, Result, CreatedPackage, ImportedTextures);
		}
	}
	else
	{
		FTarget Target;
		FString TargetError;
		const bool bHaveTarget = ResolveTarget(Target, TargetError);
		bMaterialTarget = bHaveTarget && Target.IsMaterial();

		if (!Json.Contains(TEXT("{")))
		{
			Result.Errors.Add(FString::Printf(TEXT("No JSON graph found in the pasted text. If this is an AI reply, it may be explaining why the task cannot be done. The text: %s"),
				*Json.TrimStartAndEnd().Left(1500)));
		}
		else if (!bHaveTarget)
		{
			Result.Errors.Add(TargetError);
		}
		else if (Target.IsMaterial())
		{
			FABGMaterialSpec Spec;
			FABGCreateSpec TextureSpec;
			if (FABGMaterialJson::Parse(Json, Spec, Result.Errors) && FABGCreateJson::Parse(Json, TextureSpec, Result.Errors))
			{
				// "textures": image files are imported next to the material before it is edited.
				const FString Folder = FPackageName::GetLongPackagePath(Target.Asset->GetOutermost()->GetName());
				TArray<FString> ImportErrors;
				TArray<FString> ImportWarnings;
				FABGAssetCreator::ImportTextures(TextureSpec.Textures, Folder, ImportErrors, ImportWarnings, ImportedTextures);
				if (ImportErrors.Num() > 0)
				{
					Result.Errors.Append(ImportErrors);
				}
				else
				{
					FABGAssetCreator::ApplyTextureAliases(Spec, TextureSpec.Textures, Folder);
					Result = FABGMaterialBuilder::Build(Target.Material, Target.Function, Spec);
					Result.Warnings.Append(ImportWarnings);
				}
			}
		}
		else
		{
			FABGGraphSpec Spec;
			if (FABGGraphJson::Parse(Json, Spec, Result.Errors))
			{
				Result = FABGGraphBuilder::Build(Target.Blueprint, Target.Graph, Spec);
			}
		}
	}

	UpdateErrorPanel(Json, Result);
	FString Summary = Result.ToDisplayString();
	if (!CreatedPackage.IsEmpty())
	{
		Summary = FString::Printf(TEXT("Создан ассет %s (не сохранён: Ctrl+S в его редакторе или File > Save All). "), *CreatedPackage) + Summary;
	}
	if (ImportedTextures > 0)
	{
		Summary += FString::Printf(TEXT(" Импортировано текстур: %d."), ImportedTextures);
	}
	if (bMaterialTarget && Result.bSuccess)
	{
		Summary += TEXT(" Редактор материала обновлён из ассета.");
	}
	UE_LOG(LogAIBlueprintGen, Log, TEXT("Insert answer: %s"), *Summary);
	{
		FString Entry = TEXT("=== RESULT ===\n") + Summary + TEXT("\n");
		if (Result.Errors.Num() > 0)
		{
			Entry += TEXT("\n=== ERRORS ===\n");
			for (const FString& Error : Result.Errors)
			{
				Entry += TEXT("- ") + Error + TEXT("\n");
			}
		}
		if (Result.Warnings.Num() > 0)
		{
			Entry += TEXT("\n=== WARNINGS ===\n");
			for (const FString& Warning : Result.Warnings)
			{
				Entry += TEXT("- ") + Warning + TEXT("\n");
			}
		}
		Entry += TEXT("\n=== EXTRACTED JSON ===\n") + Json + TEXT("\n\n=== RAW ANSWER ===\n") + RawAnswer;
		FABGHistory::Add(TEXT("answer"), CreatedPackage.IsEmpty() ? HistoryTarget() : FPackageName::GetShortName(CreatedPackage), Entry);
	}
	SetStatus(Summary);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
