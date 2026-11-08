#include "Core/AIBlueprintGenStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Interfaces/IPluginManager.h"

#define RootToContentDir Style->RootToContentDir

static const FVector2D Icon20x20(20.0f, 20.0f);

TSharedPtr<FSlateStyleSet> FAIBlueprintGenStyle::StyleInstance = nullptr;

void FAIBlueprintGenStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FAIBlueprintGenStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

FName FAIBlueprintGenStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("AIBlueprintGenStyle"));
	return StyleSetName;
}

const ISlateStyle& FAIBlueprintGenStyle::Get()
{
	return *StyleInstance;
}

TSharedRef<FSlateStyleSet> FAIBlueprintGenStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AIBlueprintGen"));
	check(Plugin.IsValid());
	Style->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));

	Style->Set("AIBlueprintGen.OpenPanel", new IMAGE_BRUSH_SVG(TEXT("AIBlueprintGenButtonIcon"), Icon20x20));

	return Style;
}

#undef RootToContentDir
