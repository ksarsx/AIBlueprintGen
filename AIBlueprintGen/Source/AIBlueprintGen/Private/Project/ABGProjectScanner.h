#pragma once

#include "CoreMinimal.h"

/** What a window shows to the AI about the project. Every window has its own copy. */
struct FABGProjectScope
{
	/** Master switch: add the project structure to the prompt. */
	bool bEnabled = false;
	/** false = the whole project (/Game); true = only Folder. */
	bool bFolderOnly = false;
	FString Folder = TEXT("/Game");
	bool bRecursive = true;

	bool bTextures = true;
	bool bMaterials = true;
	bool bBlueprints = true;
	bool bMeshes = true;
	bool bAnimations = false;
	bool bSounds = false;
	bool bWidgets = true;
	bool bOther = false;

	/** Native classes of the project's modules that a Blueprint can be based on. */
	bool bCppClasses = false;
	/** Content (and C++ classes) of the project's own plugins, not only /Game. */
	bool bPluginContent = false;

	/** Upper limit for the whole block (characters). */
	int32 MaxChars = 12000;
};

struct FABGProjectScanResult
{
	/** The text for the prompt (empty when nothing is enabled or found). */
	FString Text;
	/** Assets (and classes) written into the text / matching the filters. */
	int32 Shown = 0;
	int32 Total = 0;
};

/** Lists asset names (grouped by folder and type) through the Asset Registry. Names only: no asset is loaded. */
class FABGProjectScanner
{
public:
	static FABGProjectScanResult Scan(const FABGProjectScope& Scope);
};
