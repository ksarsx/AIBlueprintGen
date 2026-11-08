#include "Export/ABGMaterialExporter.h"
#include "Material/ABGMaterialIds.h"
#include "Prompt/ABGBlueprintKind.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MaterialDomain.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialFunction.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace
{
	FString ToCondensedJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	bool IsSkippedProperty(const FProperty* Property)
	{
		if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient))
		{
			return true;
		}
		const FString Name = Property->GetName();
		if (Name.StartsWith(TEXT("MaterialExpressionEditor")) || Name == TEXT("Desc") || Name == TEXT("bCollapsed")
			|| Name == TEXT("bShowOutputNameOnPin") || Name == TEXT("bRealtimePreview") || Name == TEXT("bCommentBubbleVisible"))
		{
			return true;
		}
		if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
		{
			return Struct->Struct && Struct->Struct->GetName().EndsWith(TEXT("Input")); // wires are exported as connections
		}
		if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
		{
			return CastField<FStructProperty>(Array->Inner) != nullptr; // e.g. Custom node inputs contain wires
		}
		return false;
	}

	/** Properties that differ from the class defaults, as name -> text. */
	TSharedRef<FJsonObject> ChangedProperties(UMaterialExpression* Expression)
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		const UObject* Defaults = Expression->GetClass()->GetDefaultObject();
		for (TFieldIterator<FProperty> It(Expression->GetClass()); It; ++It)
		{
			const FProperty* Property = *It;
			if (IsSkippedProperty(Property) || Property->Identical_InContainer(Expression, Defaults))
			{
				continue;
			}
			FString Text;
			if (const FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Property))
			{
				const UObject* Value = ObjectProp->GetObjectPropertyValue_InContainer(Expression);
				Text = Value ? Value->GetPathName() : FString(TEXT("None"));
			}
			else
			{
				const void* Ptr = Property->ContainerPtrToValuePtr<void>(Expression);
				Property->ExportText_Direct(Text, Ptr, Ptr, Expression, PPF_None);
			}
			if (Text.Len() > 300)
			{
				Text = Text.Left(300) + TEXT("...");
			}
			Props->SetStringField(Property->GetName(), Text);
		}
		return Props;
	}

	FString OutputNameOf(UMaterialExpression* Expression, int32 OutputIndex)
	{
		const TArray<FString> Names = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression);
		return Names.IsValidIndex(OutputIndex) ? Names[OutputIndex] : FString();
	}

	FString EnumText(const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetNameStringByValue(Value) : FString::Printf(TEXT("%lld"), Value);
	}
}

FString FABGMaterialExporter::Export(UMaterial* Material, UMaterialFunction* Function, int32 MaxChars)
{
	if (!Material && !Function)
	{
		return FString();
	}
	const UObject* Asset = Material ? static_cast<const UObject*>(Material) : static_cast<const UObject*>(Function);

	TArray<FString> Sections;

	FString Head = FString::Printf(TEXT("Asset: %s\nKind: %s\nAsset path: %s\n"), *Asset->GetName(),
		Material ? TEXT("Material") : TEXT("Material Function"), *Asset->GetPathName());
	if (Material)
	{
		TArray<FString> Models;
		const FMaterialShadingModelField ShadingModels = Material->GetShadingModels();
		for (int32 ModelIndex = 0; ModelIndex < MSM_NUM; ++ModelIndex)
		{
			if (ShadingModels.HasShadingModel(static_cast<EMaterialShadingModel>(ModelIndex)))
			{
				Models.Add(EnumText(StaticEnum<EMaterialShadingModel>(), ModelIndex));
			}
		}
		Head += FString::Printf(TEXT("Settings: MaterialDomain=%s, BlendMode=%s, ShadingModel=%s, TwoSided=%s\n"),
			*EnumText(StaticEnum<EMaterialDomain>(), static_cast<int64>(Material->MaterialDomain.GetValue())),
			*EnumText(StaticEnum<EBlendMode>(), static_cast<int64>(Material->BlendMode.GetValue())),
			*FString::Join(Models, TEXT("+")),
			Material->IsTwoSided() ? TEXT("true") : TEXT("false"));
	}
	Head += TEXT("Note: this is the saved/applied state of the asset.\n");
	Sections.Add(Head);

	const TArray<UMaterialExpression*> Expressions = Function
		? UMaterialEditingLibrary::GetMaterialFunctionExpressions(Function)
		: UMaterialEditingLibrary::GetMaterialExpressions(Material);

	TSet<const UMaterialExpression*> Exported;
	TArray<FString> NodeLines;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression || Expression->IsA<UMaterialExpressionComment>())
		{
			continue;
		}
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("id"), ABGMaterialId::Make(Expression));
		Json->SetStringField(TEXT("type"), ABGMaterialId::TypeName(Expression));
		const TSharedRef<FJsonObject> Props = ChangedProperties(Expression);
		if (Props->Values.Num() > 0)
		{
			Json->SetObjectField(TEXT("props"), Props);
		}
		TArray<TSharedPtr<FJsonValue>> In, Out;
		for (const FString& Name : UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression))
		{
			if (!Name.Equals(TEXT("None"), ESearchCase::IgnoreCase)) // an unnamed input: "from" alone connects to it
			{
				In.Add(MakeShared<FJsonValueString>(Name));
			}
		}
		for (const FString& Name : UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression))
		{
			Out.Add(MakeShared<FJsonValueString>(Name));
		}
		Json->SetArrayField(TEXT("in"), In);
		Json->SetArrayField(TEXT("out"), Out);
		Exported.Add(Expression);
		NodeLines.Add(TEXT("    ") + ToCondensedJson(Json));
	}

	TArray<FString> ConnectionLines;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression || !Exported.Contains(Expression))
		{
			continue;
		}
		const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression);
		int32 InputIndex = 0;
		for (FExpressionInputIterator It{ Expression }; It; ++It, ++InputIndex)
		{
			UMaterialExpression* Source = It->Expression;
			if (!Source || !Exported.Contains(Source))
			{
				continue;
			}
			const FString OutName = OutputNameOf(Source, It->OutputIndex);
			FString InName = InputNames.IsValidIndex(InputIndex) ? InputNames[InputIndex] : FString();
			if (InName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				InName.Reset();
			}
			ConnectionLines.Add(FString::Printf(TEXT("    {\"from\":\"%s%s\",\"to\":\"%s%s\"}"),
				*ABGMaterialId::Make(Source), OutName.IsEmpty() ? TEXT("") : *(TEXT(".") + OutName),
				*ABGMaterialId::Make(Expression), InName.IsEmpty() ? TEXT("") : *(TEXT(".") + InName)));
		}
	}
	if (Material)
	{
		struct FRoot { const TCHAR* Name; EMaterialProperty Property; };
		static const FRoot Roots[] =
		{
			{ TEXT("BaseColor"), MP_BaseColor }, { TEXT("Metallic"), MP_Metallic }, { TEXT("Specular"), MP_Specular },
			{ TEXT("Roughness"), MP_Roughness }, { TEXT("Anisotropy"), MP_Anisotropy }, { TEXT("EmissiveColor"), MP_EmissiveColor },
			{ TEXT("Opacity"), MP_Opacity }, { TEXT("OpacityMask"), MP_OpacityMask }, { TEXT("Normal"), MP_Normal },
			{ TEXT("Tangent"), MP_Tangent }, { TEXT("WorldPositionOffset"), MP_WorldPositionOffset },
			{ TEXT("SubsurfaceColor"), MP_SubsurfaceColor }, { TEXT("AmbientOcclusion"), MP_AmbientOcclusion },
			{ TEXT("Refraction"), MP_Refraction }, { TEXT("PixelDepthOffset"), MP_PixelDepthOffset },
		};
		for (const FRoot& Root : Roots)
		{
			UMaterialExpression* Source = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, Root.Property);
			if (Source && Exported.Contains(Source))
			{
				const FString OutName = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, Root.Property);
				ConnectionLines.Add(FString::Printf(TEXT("    {\"from\":\"%s%s\",\"to\":\"Material.%s\"}"),
					*ABGMaterialId::Make(Source), OutName.IsEmpty() ? TEXT("") : *(TEXT(".") + OutName), Root.Name));
			}
		}
	}

	FString Graph = TEXT("--- Graph ---\n{\n  \"nodes\": [\n");
	Graph += FString::Join(NodeLines, TEXT(",\n"));
	Graph += TEXT("\n  ],\n  \"connections\": [\n");
	Graph += FString::Join(ConnectionLines, TEXT(",\n"));
	Graph += TEXT("\n  ]\n}\n");
	Sections.Add(Graph);

	FString Out;
	for (int32 i = 0; i < Sections.Num(); ++i)
	{
		if (MaxChars > 0 && Out.Len() + Sections[i].Len() > MaxChars && i > 0)
		{
			Out += TEXT("\n[Truncated: the material graph was omitted because of the context size limit. Raise it in Editor Preferences > Plugins > AI Blueprint Generator.]\n");
			break;
		}
		Out += Sections[i] + TEXT("\n");
	}
	return Out;
}
