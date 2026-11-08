#include "Project/ABGProjectScanner.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ModuleDescriptor.h"
#include "PluginDescriptor.h"
#include "ProjectDescriptor.h"
#include "UObject/UObjectIterator.h"

namespace
{
	enum class EAssetGroup : uint8
	{
		Textures,
		Materials,
		Blueprints,
		Widgets,
		Meshes,
		Animations,
		Sounds,
		Other,
		Count
	};

	const TCHAR* GroupTitle(EAssetGroup Group)
	{
		switch (Group)
		{
		case EAssetGroup::Textures: return TEXT("Textures");
		case EAssetGroup::Materials: return TEXT("Materials, material functions and instances");
		case EAssetGroup::Blueprints: return TEXT("Blueprints");
		case EAssetGroup::Widgets: return TEXT("Widget Blueprints");
		case EAssetGroup::Meshes: return TEXT("Meshes");
		case EAssetGroup::Animations: return TEXT("Animations and Animation Blueprints");
		case EAssetGroup::Sounds: return TEXT("Sounds");
		default: return TEXT("Other assets");
		}
	}

	bool IsEnabled(const FABGProjectScope& Scope, EAssetGroup Group)
	{
		switch (Group)
		{
		case EAssetGroup::Textures: return Scope.bTextures;
		case EAssetGroup::Materials: return Scope.bMaterials;
		case EAssetGroup::Blueprints: return Scope.bBlueprints;
		case EAssetGroup::Widgets: return Scope.bWidgets;
		case EAssetGroup::Meshes: return Scope.bMeshes;
		case EAssetGroup::Animations: return Scope.bAnimations;
		case EAssetGroup::Sounds: return Scope.bSounds;
		default: return Scope.bOther;
		}
	}

	/** Returns false for things that never belong in the list (redirectors). */
	bool Classify(const FAssetData& Asset, EAssetGroup& OutGroup)
	{
		const FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		if (ClassName == TEXT("ObjectRedirector"))
		{
			return false;
		}
		if (ClassName.StartsWith(TEXT("Texture")) || ClassName.EndsWith(TEXT("Texture")))
		{
			OutGroup = EAssetGroup::Textures;
		}
		else if (ClassName.StartsWith(TEXT("Material")))
		{
			OutGroup = EAssetGroup::Materials;
		}
		else if (ClassName == TEXT("WidgetBlueprint"))
		{
			OutGroup = EAssetGroup::Widgets;
		}
		else if (ClassName == TEXT("AnimBlueprint"))
		{
			OutGroup = EAssetGroup::Animations;
		}
		else if (ClassName.EndsWith(TEXT("Blueprint")))
		{
			OutGroup = EAssetGroup::Blueprints;
		}
		else if (ClassName == TEXT("StaticMesh") || ClassName == TEXT("SkeletalMesh"))
		{
			OutGroup = EAssetGroup::Meshes;
		}
		else if (ClassName.StartsWith(TEXT("Anim")) || ClassName.StartsWith(TEXT("BlendSpace")) || ClassName == TEXT("PoseAsset"))
		{
			OutGroup = EAssetGroup::Animations;
		}
		else if (ClassName.StartsWith(TEXT("Sound")) || ClassName.StartsWith(TEXT("MetaSound")))
		{
			OutGroup = EAssetGroup::Sounds;
		}
		else
		{
			OutGroup = EAssetGroup::Other;
		}
		return true;
	}

	/** Appends "Folder: A, B, C" lines of Folders (already sorted) while the budget lasts. Returns the number of names written. */
	int32 WriteFolders(const TMap<FString, TArray<FString>>& Folders, int32 Budget, FString& InOutText, bool& bOutCut)
	{
		TArray<FString> FolderNames;
		Folders.GetKeys(FolderNames);
		FolderNames.Sort();

		int32 Written = 0;
		int32 Used = 0;
		for (const FString& Folder : FolderNames)
		{
			TArray<FString> Names = Folders[Folder];
			Names.Sort();

			FString Line = Folder + TEXT(": ");
			Used += Line.Len();
			if (Used > Budget)
			{
				bOutCut = true;
				break;
			}
			int32 NamesInLine = 0;
			for (const FString& Name : Names)
			{
				const int32 Extra = Name.Len() + 2;
				if (Used + Extra > Budget)
				{
					bOutCut = true;
					break;
				}
				if (NamesInLine > 0)
				{
					Line += TEXT(", ");
				}
				Line += Name;
				Used += Extra;
				++NamesInLine;
				++Written;
			}
			if (NamesInLine > 0)
			{
				InOutText += Line + TEXT("\n");
			}
			if (bOutCut)
			{
				break;
			}
		}
		return Written;
	}
}

FABGProjectScanResult FABGProjectScanner::Scan(const FABGProjectScope& Scope)
{
	FABGProjectScanResult Result;
	if (!Scope.bEnabled)
	{
		return Result;
	}

	// ---- where to look ----
	TArray<FName> Roots;
	bool bRecursive = true;
	FString ScopeText;
	if (Scope.bFolderOnly)
	{
		FString Folder = Scope.Folder.TrimStartAndEnd();
		Folder.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Folder.IsEmpty())
		{
			Folder = TEXT("/Game");
		}
		if (!Folder.StartsWith(TEXT("/")))
		{
			Folder = TEXT("/Game/") + Folder;
		}
		while (Folder.Len() > 1 && Folder.EndsWith(TEXT("/")))
		{
			Folder.LeftChopInline(1);
		}
		Roots.Add(FName(*Folder));
		bRecursive = Scope.bRecursive;
		ScopeText = FString::Printf(TEXT("folder %s%s"), *Folder, bRecursive ? TEXT(" and its subfolders") : TEXT(" only"));
	}
	else
	{
		Roots.Add(FName(TEXT("/Game")));
		ScopeText = TEXT("the whole project (/Game)");
		if (Scope.bPluginContent)
		{
			for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPluginsWithContent())
			{
				if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project)
				{
					Roots.AddUnique(FName(*FString::Printf(TEXT("/%s"), *Plugin->GetName())));
				}
			}
			ScopeText += TEXT(" and the content of the project's plugins");
		}
	}

	// ---- assets ----
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FARFilter Filter;
	Filter.PackagePaths = Roots;
	Filter.bRecursivePaths = bRecursive;
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	TMap<FString, TArray<FString>> ByGroup[static_cast<int32>(EAssetGroup::Count)];
	int32 GroupCount[static_cast<int32>(EAssetGroup::Count)] = {};
	for (const FAssetData& Asset : Assets)
	{
		EAssetGroup Group;
		if (!Classify(Asset, Group) || !IsEnabled(Scope, Group))
		{
			continue;
		}
		ByGroup[static_cast<int32>(Group)].FindOrAdd(Asset.PackagePath.ToString()).Add(Asset.AssetName.ToString());
		++GroupCount[static_cast<int32>(Group)];
		++Result.Total;
	}

	// ---- C++ classes ----
	TArray<FString> ClassLines;
	if (Scope.bCppClasses)
	{
		TSet<FName> Modules;
		if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
		{
			for (const FModuleDescriptor& Module : Project->Modules)
			{
				Modules.Add(Module.Name);
			}
		}
		if (Scope.bPluginContent)
		{
			for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
			{
				if (Plugin->GetLoadedFrom() == EPluginLoadedFrom::Project)
				{
					for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
					{
						Modules.Add(Module.Name);
					}
				}
			}
		}
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->HasAnyClassFlags(CLASS_Native) || Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			const FString Package = Class->GetOutermost()->GetName();
			if (!Package.StartsWith(TEXT("/Script/")) || !Modules.Contains(FName(*Package.RightChop(8))))
			{
				continue;
			}
			if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(Class))
			{
				continue;
			}
			ClassLines.Add(FString::Printf(TEXT("%s (%s)"), *Class->GetName(), Class->GetSuperClass() ? *Class->GetSuperClass()->GetName() : TEXT("")));
			++Result.Total;
		}
		ClassLines.Sort();
	}

	// ---- text, with a budget per section so that one huge group cannot hide the others ----
	int32 Sections = ClassLines.Num() > 0 ? 1 : 0;
	for (int32 i = 0; i < static_cast<int32>(EAssetGroup::Count); ++i)
	{
		Sections += GroupCount[i] > 0 ? 1 : 0;
	}
	if (Sections == 0)
	{
		Result.Text = FString::Printf(TEXT("(nothing found in %s with the chosen filters)\n"), *ScopeText);
		return Result;
	}
	const int32 SectionBudget = FMath::Max(300, Scope.MaxChars / Sections);

	FString Text = FString::Printf(TEXT("Scope: %s. Asset names grouped by folder: the full path of an asset is <folder>/<name>.\n"), *ScopeText);
	if (Registry.IsLoadingAssets())
	{
		Text += TEXT("Note: the editor is still scanning assets, this list may be incomplete.\n");
	}
	for (int32 i = 0; i < static_cast<int32>(EAssetGroup::Count); ++i)
	{
		if (GroupCount[i] == 0)
		{
			continue;
		}
		FString Section;
		bool bCut = false;
		const int32 Written = WriteFolders(ByGroup[i], SectionBudget, Section, bCut);
		Result.Shown += Written;
		Text += FString::Printf(TEXT("\n--- %s (%d%s) ---\n"), GroupTitle(static_cast<EAssetGroup>(i)), GroupCount[i],
			bCut ? *FString::Printf(TEXT(", showing %d"), Written) : TEXT(""));
		Text += Section;
	}
	if (ClassLines.Num() > 0)
	{
		Text += FString::Printf(TEXT("\n--- C++ classes of the project that a Blueprint can be based on (%d) ---\n"), ClassLines.Num());
		int32 Used = 0;
		int32 Written = 0;
		FString Line;
		for (const FString& ClassLine : ClassLines)
		{
			if (Used + ClassLine.Len() + 2 > SectionBudget)
			{
				Line += TEXT(", ...");
				break;
			}
			Line += (Written > 0 ? TEXT(", ") : TEXT("")) + ClassLine;
			Used += ClassLine.Len() + 2;
			++Written;
		}
		Text += Line + TEXT("\n");
		Result.Shown += Written;
	}
	Result.Text = Text;
	return Result;
}
