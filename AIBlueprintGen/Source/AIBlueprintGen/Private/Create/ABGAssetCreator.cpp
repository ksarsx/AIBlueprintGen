#include "Create/ABGAssetCreator.h"
#include "ABGSettings.h"
#include "Core/AIBlueprintGenLog.h"
#include "Schema/ABGMaterialSpec.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "IAssetTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprintFactory.h"

bool FABGAssetCreator::NormalizeFolder(const FString& Folder, FString& OutFolder, FString& OutError)
{
	FString Result = Folder.TrimStartAndEnd();
	Result.ReplaceInline(TEXT("\\"), TEXT("/"));
	if (Result.IsEmpty())
	{
		const UABGSettings* Settings = UABGSettings::Get();
		Result = (Settings && !Settings->DefaultCreateFolder.TrimStartAndEnd().IsEmpty()) ? Settings->DefaultCreateFolder.TrimStartAndEnd() : FString(TEXT("/Game/AI"));
		Result.ReplaceInline(TEXT("\\"), TEXT("/"));
	}
	if (!Result.StartsWith(TEXT("/")))
	{
		Result = TEXT("/Game/") + Result;
	}
	while (Result.Len() > 1 && Result.EndsWith(TEXT("/")))
	{
		Result.LeftChopInline(1);
	}
	if (Result != TEXT("/Game") && !Result.StartsWith(TEXT("/Game/")))
	{
		OutError = FString::Printf(TEXT("Folder \"%s\" is outside the project: it must start with /Game."), *Result);
		return false;
	}
	if (Result.Contains(TEXT("..")) || Result.Contains(TEXT("//")))
	{
		OutError = FString::Printf(TEXT("Folder \"%s\" is not a valid content folder."), *Result);
		return false;
	}
	OutFolder = Result;
	return true;
}

FString FABGAssetCreator::MakePackageName(const FString& Folder, const FString& Name)
{
	return Folder + TEXT("/") + Name;
}

bool FABGAssetCreator::AssetExists(const FString& PackageName)
{
	// An asset deleted a moment ago may leave its (empty) package in memory until the next GC, so look for the asset object itself.
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	return FPackageName::DoesPackageExist(PackageName)
		|| StaticFindObject(UObject::StaticClass(), nullptr, *(PackageName + TEXT(".") + AssetName)) != nullptr;
}

UClass* FABGAssetCreator::ResolveParentClass(EABGCreateKind Kind, const FString& Text, FString& OutError)
{
	const bool bWidget = Kind == EABGCreateKind::WidgetBlueprint;
	const FString Name = Text.TrimStartAndEnd();

	UClass* Class = nullptr;
	if (Name.IsEmpty())
	{
		Class = bWidget ? UUserWidget::StaticClass() : AActor::StaticClass();
	}
	else if (Name.StartsWith(TEXT("/")))
	{
		Class = LoadObject<UClass>(nullptr, *Name);
		if (!Class && !Name.EndsWith(TEXT("_C")))
		{
			Class = LoadObject<UClass>(nullptr, *(Name + TEXT("_C")));
		}
	}
	else
	{
		Class = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
		if (!Class)
		{
			Class = FindFirstObject<UClass>(*(Name + TEXT("_C")), EFindFirstObjectOptions::NativeFirst);
		}
		if (!Class && Name.Len() > 1 && (Name[0] == TEXT('U') || Name[0] == TEXT('A')))
		{
			Class = FindFirstObject<UClass>(*Name.Mid(1), EFindFirstObjectOptions::NativeFirst);
		}
	}

	if (!Class)
	{
		OutError = FString::Printf(TEXT("Parent class \"%s\" was not found. Examples: %s."), *Name,
			bWidget ? TEXT("UserWidget") : TEXT("Actor, Pawn, Character, ActorComponent, SceneComponent, GameModeBase, PlayerController, or a full class path"));
		return nullptr;
	}
	if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(Class))
	{
		OutError = FString::Printf(TEXT("A Blueprint cannot be based on class \"%s\" (it is not Blueprintable)."), *Class->GetName());
		return nullptr;
	}
	if (bWidget && !Class->IsChildOf(UUserWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("A Widget Blueprint needs a UserWidget-based parent class, \"%s\" is not one."), *Class->GetName());
		return nullptr;
	}
	if (!bWidget && Class->IsChildOf(UUserWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("\"%s\" is a widget class: create a Widget Blueprint instead."), *Class->GetName());
		return nullptr;
	}
	return Class;
}

UObject* FABGAssetCreator::CreateAsset(EABGCreateKind Kind, const FString& Name, const FString& Folder, UClass* ParentClass, FString& OutError)
{
	UFactory* Factory = nullptr;
	switch (Kind)
	{
	case EABGCreateKind::Blueprint:
	{
		UBlueprintFactory* BlueprintFactory = NewObject<UBlueprintFactory>();
		BlueprintFactory->ParentClass = ParentClass;
		BlueprintFactory->bSkipClassPicker = true;
		Factory = BlueprintFactory;
		break;
	}
	case EABGCreateKind::WidgetBlueprint:
	{
		UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
		WidgetFactory->ParentClass = ParentClass ? ParentClass : UUserWidget::StaticClass();
		Factory = WidgetFactory;
		break;
	}
	case EABGCreateKind::Material:
		Factory = NewObject<UMaterialFactoryNew>();
		break;
	case EABGCreateKind::MaterialFunction:
		Factory = NewObject<UMaterialFunctionFactoryNew>();
		break;
	default:
		OutError = TEXT("Unknown asset kind.");
		return nullptr;
	}

	// Keep the factory alive for the duration of the call.
	TStrongObjectPtr<UFactory> FactoryHolder(Factory);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, Folder, Factory->GetSupportedClass(), Factory, FName("AIBlueprintGen"));
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("The engine could not create the asset %s."), *MakePackageName(Folder, Name));
		return nullptr;
	}
	return Asset;
}

bool FABGAssetCreator::DeleteAsset(UObject* Asset)
{
	if (!Asset)
	{
		return false;
	}
	if (GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditors->CloseAllEditorsForAsset(Asset);
		}
	}
	TArray<UObject*> ToDelete;
	ToDelete.Add(Asset);
	return ObjectTools::DeleteObjects(ToDelete, /*bShowConfirmation*/ false) > 0;
}

void FABGAssetCreator::OpenEditor(UObject* Asset)
{
	if (Asset && GEditor)
	{
		if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditors->OpenEditorForAsset(Asset);
		}
	}
}

void FABGAssetCreator::ImportTextures(const TArray<FABGTextureSpec>& Textures, const FString& Folder,
	TArray<FString>& OutErrors, TArray<FString>& OutWarnings, int32& OutImported)
{
	if (Textures.Num() == 0)
	{
		return;
	}
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	for (const FABGTextureSpec& Tex : Textures)
	{
		FString File = Tex.File;
		File.ReplaceInline(TEXT("\\"), TEXT("/"));
		const FString PackageName = MakePackageName(Folder, Tex.Name);

		if (AssetExists(PackageName))
		{
			OutWarnings.Add(FString::Printf(TEXT("Texture %s already exists: it was reused, the file \"%s\" was not imported again."), *PackageName, *Tex.File));
			continue;
		}
		if (!FPaths::FileExists(File))
		{
			OutErrors.Add(FString::Printf(TEXT("Texture \"%s\": the file \"%s\" does not exist on this computer."), *Tex.Name, *Tex.File));
			continue;
		}

		TStrongObjectPtr<UAssetImportTask> Task(NewObject<UAssetImportTask>());
		Task->Filename = File;
		Task->DestinationPath = Folder;
		Task->DestinationName = Tex.Name;
		Task->bAutomated = true;
		Task->bReplaceExisting = false;
		Task->bSave = false;
		Task->bAsync = false;

		TArray<UAssetImportTask*> Tasks;
		Tasks.Add(Task.Get());
		AssetTools.ImportAssetTasks(Tasks);

		UTexture* Texture = nullptr;
		for (UObject* Imported : Task->GetObjects())
		{
			Texture = Cast<UTexture>(Imported);
			if (Texture)
			{
				break;
			}
		}
		if (!Texture)
		{
			OutErrors.Add(FString::Printf(TEXT("Texture \"%s\": the file \"%s\" could not be imported (unsupported or damaged image)."), *Tex.Name, *Tex.File));
			continue;
		}

		// Usage: how the pixels are interpreted.
		const bool bNormal = Tex.Usage.Equals(TEXT("Normal"), ESearchCase::IgnoreCase);
		const bool bLinear = bNormal || Tex.Usage.Equals(TEXT("Mask"), ESearchCase::IgnoreCase) || Tex.Usage.Equals(TEXT("Data"), ESearchCase::IgnoreCase);
		if (bLinear)
		{
			Texture->PreEditChange(static_cast<FProperty*>(nullptr));
			Texture->CompressionSettings = bNormal ? TC_Normalmap : TC_Masks;
			Texture->SRGB = false;
			Texture->PostEditChange();
		}
		Texture->MarkPackageDirty();
		++OutImported;
		UE_LOG(LogAIBlueprintGen, Log, TEXT("Imported texture %s from %s"), *PackageName, *File);
	}
}

void FABGAssetCreator::ApplyTextureAliases(FABGMaterialSpec& Spec, const TArray<FABGTextureSpec>& Textures, const FString& Folder)
{
	if (Textures.Num() == 0)
	{
		return;
	}
	for (FABGMatNode& Node : Spec.Nodes)
	{
		for (TPair<FString, FString>& Prop : Node.Props)
		{
			for (const FABGTextureSpec& Tex : Textures)
			{
				if (Prop.Value.Equals(Tex.Name, ESearchCase::IgnoreCase))
				{
					Prop.Value = MakePackageName(Folder, Tex.Name);
					break;
				}
			}
		}
	}
}
