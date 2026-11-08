#include "Material/ABGMaterialBuilder.h"
#include "Material/ABGMaterialIds.h"
#include "Core/AIBlueprintGenLog.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ABGMaterialBuilder"

namespace
{
	constexpr int32 NodeSpacingX = 320;
	constexpr int32 NodeSpacingY = 260;
	constexpr int32 BlockGapY = 320;

	FString Normalize(const FString& In)
	{
		FString Out;
		for (const TCHAR Ch : In)
		{
			if (FChar::IsAlnum(Ch))
			{
				Out.AppendChar(FChar::ToLower(Ch));
			}
		}
		return Out;
	}

	struct FTarget
	{
		UMaterial* Material = nullptr;
		UMaterialFunction* Function = nullptr;
	};

	TArray<UMaterialExpression*> GetAllExpressions(const FTarget& T)
	{
		return T.Function ? UMaterialEditingLibrary::GetMaterialFunctionExpressions(T.Function)
			: UMaterialEditingLibrary::GetMaterialExpressions(T.Material);
	}

	// ------------------------------------------------------------------ classes

	UClass* ResolveExpressionClass(const FString& Type, FString& OutSuggestions)
	{
		FString Name = Type.TrimStartAndEnd();
		if (Name.StartsWith(TEXT("UMaterialExpression")))
		{
			Name.RightChopInline(1);
		}
		Name.RemoveFromStart(TEXT("MaterialExpression"));
		if (Name.Equals(TEXT("Lerp"), ESearchCase::IgnoreCase))
		{
			Name = TEXT("LinearInterpolate");
		}

		const FString Wanted = Normalize(Name);
		TArray<FString> Similar;
		UClass* Found = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UMaterialExpression::StaticClass())
				|| Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			FString Short = Class->GetName();
			Short.RemoveFromStart(TEXT("MaterialExpression"));
			const FString ShortNorm = Normalize(Short);
			if (ShortNorm == Wanted)
			{
				Found = Class;
				break;
			}
			if (!Wanted.IsEmpty() && Similar.Num() < 8 && (ShortNorm.Contains(Wanted) || Wanted.Contains(ShortNorm)))
			{
				Similar.AddUnique(Short);
			}
		}
		if (!Found)
		{
			OutSuggestions = FString::Join(Similar, TEXT(", "));
		}
		return Found;
	}

	// ------------------------------------------------------------------ properties

	bool IsInputStruct(const FProperty* Property)
	{
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		return Struct && Struct->Struct && Struct->Struct->GetName().EndsWith(TEXT("Input"));
	}

	FProperty* FindProp(UObject* Object, const FString& Name)
	{
		if (FProperty* Direct = Object->GetClass()->FindPropertyByName(FName(*Name)))
		{
			return Direct;
		}
		const FString Wanted = Normalize(Name);
		if (Wanted.IsEmpty())
		{
			return nullptr;
		}
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (Normalize(It->GetName()) == Wanted || Normalize(It->GetDisplayNameText().ToString()) == Wanted)
			{
				return *It;
			}
		}
		return nullptr;
	}

	FString ListEditableProps(const UClass* Class)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(Class); It && Names.Num() < 30; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Edit) && !It->HasAnyPropertyFlags(CPF_Transient) && !IsInputStruct(*It)
				&& !It->GetName().StartsWith(TEXT("MaterialExpressionEditor")))
			{
				Names.Add(It->GetName());
			}
		}
		return FString::Join(Names, TEXT(", "));
	}

	UObject* LoadAssetByPath(UClass* Class, const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		int32 QuoteIdx = INDEX_NONE;
		if (Path.FindChar(TEXT('\''), QuoteIdx)) // Texture2D'/Game/T.T'
		{
			Path = Path.Mid(QuoteIdx + 1);
			Path.RemoveFromEnd(TEXT("'"));
		}
		UObject* Object = StaticLoadObject(Class, nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Object && !Path.Contains(TEXT(".")))
		{
			Object = StaticLoadObject(Class, nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		return Object;
	}

	bool ParseFloats(const FString& Text, int32 Min, int32 Max, TArray<double>& Out)
	{
		TArray<FString> Parts;
		Text.TrimStartAndEnd().ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			const FString Trimmed = Part.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || !Trimmed.IsNumeric())
			{
				return false;
			}
			Out.Add(FCString::Atod(*Trimmed));
		}
		return Out.Num() >= Min && Out.Num() <= Max;
	}

	bool ResolveEnumValue(const UEnum* Enum, const FString& Value, int64& OutValue, FString& OutValid)
	{
		const FString Wanted = Normalize(Value);
		TArray<FString> Valid;
		int64 Found = INDEX_NONE;
		bool bFound = false;
		for (int32 Pass = 0; Pass < 2 && !bFound; ++Pass)
		{
			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
			{
				const FString Full = Enum->GetNameStringByIndex(i);
				if (Full.EndsWith(TEXT("_MAX")) || Enum->HasMetaData(TEXT("Hidden"), i))
				{
					continue;
				}
				if (Pass == 0)
				{
					Valid.Add(Full);
				}
				FString Candidate = Full;
				if (Pass == 1)
				{
					int32 UnderscoreIdx = INDEX_NONE;
					if (Full.FindChar(TEXT('_'), UnderscoreIdx))
					{
						Candidate = Full.Mid(UnderscoreIdx + 1); // BLEND_Translucent -> Translucent
					}
				}
				if (Normalize(Candidate) == Wanted || Normalize(Enum->GetDisplayNameTextByIndex(i).ToString()) == Wanted)
				{
					Found = Enum->GetValueByIndex(i);
					bFound = true;
					break;
				}
			}
		}
		OutValid = FString::Join(Valid, TEXT(", "));
		OutValue = Found;
		return bFound;
	}

	/** Sets one property from text (typed conversion for the common cases). Returns false and fills OutError on failure. */
	bool SetPropertyFromText(UObject* Object, const FString& PropName, const FString& Value, FString& OutError)
	{
		FProperty* Prop = FindProp(Object, PropName);
		if (!Prop || IsInputStruct(Prop))
		{
			OutError = FString::Printf(TEXT("\"%s\" has no property \"%s\"%s. Editable properties: %s"),
				*Object->GetClass()->GetName(), *PropName,
				(Prop && IsInputStruct(Prop)) ? TEXT(" that can be set (it is an input pin: use a connection)") : TEXT(""),
				*ListEditableProps(Object->GetClass()));
			return false;
		}

		void* Ptr = Prop->ContainerPtrToValuePtr<void>(Object);
		const FString V = Value.TrimStartAndEnd();
		Object->Modify();

		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			if (V.Equals(TEXT("true"), ESearchCase::IgnoreCase) || V == TEXT("1"))
			{
				BoolProp->SetPropertyValue(Ptr, true);
			}
			else if (V.Equals(TEXT("false"), ESearchCase::IgnoreCase) || V == TEXT("0"))
			{
				BoolProp->SetPropertyValue(Ptr, false);
			}
			else
			{
				OutError = FString::Printf(TEXT("property \"%s\" is a bool: use true or false (got \"%s\")."), *Prop->GetName(), *V);
				return false;
			}
			return true;
		}

		if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Prop))
		{
			if (V.IsEmpty() || V.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				ObjectProp->SetObjectPropertyValue(Ptr, nullptr);
				return true;
			}
			UObject* Loaded = LoadAssetByPath(ObjectProp->PropertyClass, V);
			if (!Loaded)
			{
				OutError = FString::Printf(TEXT("property \"%s\": no %s asset found at \"%s\". Use a full asset path such as /Game/Folder/Asset."),
					*Prop->GetName(), *ObjectProp->PropertyClass->GetName(), *V);
				return false;
			}
			ObjectProp->SetObjectPropertyValue(Ptr, Loaded);
			return true;
		}

		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			TArray<double> Numbers;
			if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
			{
				if (!V.StartsWith(TEXT("(")) && ParseFloats(V, 3, 4, Numbers))
				{
					*static_cast<FLinearColor*>(Ptr) = FLinearColor(Numbers[0], Numbers[1], Numbers[2], Numbers.Num() > 3 ? Numbers[3] : 1.0);
					return true;
				}
			}
			else if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				if (!V.StartsWith(TEXT("(")) && ParseFloats(V, 3, 3, Numbers))
				{
					*static_cast<FVector*>(Ptr) = FVector(Numbers[0], Numbers[1], Numbers[2]);
					return true;
				}
			}
			else if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
			{
				if (!V.StartsWith(TEXT("(")) && ParseFloats(V, 2, 2, Numbers))
				{
					*static_cast<FVector2D*>(Ptr) = FVector2D(Numbers[0], Numbers[1]);
					return true;
				}
			}
			// anything else (or the "(R=..,G=..)" form) goes through the engine's text importer below
		}

		const UEnum* Enum = nullptr;
		FNumericProperty* EnumNumeric = nullptr;
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop); ByteProp && ByteProp->Enum)
		{
			Enum = ByteProp->Enum;
			EnumNumeric = ByteProp;
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			Enum = EnumProp->GetEnum();
			EnumNumeric = EnumProp->GetUnderlyingProperty();
		}
		if (Enum && EnumNumeric)
		{
			int64 EnumValue = INDEX_NONE;
			FString Valid;
			if (!ResolveEnumValue(Enum, V, EnumValue, Valid))
			{
				OutError = FString::Printf(TEXT("property \"%s\": \"%s\" is not a valid value. Valid values: %s"), *Prop->GetName(), *V, *Valid);
				return false;
			}
			EnumNumeric->SetIntPropertyValue(Ptr, EnumValue);
			return true;
		}

		if (!Prop->ImportText_Direct(*V, Ptr, Object, PPF_None))
		{
			OutError = FString::Printf(TEXT("property \"%s\": \"%s\" is not a valid %s value."), *Prop->GetName(), *V, *Prop->GetCPPType());
			return false;
		}
		return true;
	}

	void NotifyPropertyChanged(UObject* Object, const FString& PropName)
	{
		if (FProperty* Prop = FindProp(Object, PropName))
		{
			FPropertyChangedEvent Event(Prop);
			Object->PostEditChangeProperty(Event);
		}
	}

	// ------------------------------------------------------------------ pins and outputs

	bool ResolvePin(const TArray<FString>& Names, const FString& Wanted, FString& OutExact)
	{
		OutExact.Reset();
		if (Names.Num() == 0)
		{
			return false;
		}
		const FString Trimmed = Wanted.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return true; // first
		}
		for (const FString& Name : Names)
		{
			if (Name.Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				OutExact = Name;
				return true;
			}
		}
		const FString Norm = Normalize(Trimmed);
		for (const FString& Name : Names)
		{
			if (!Name.IsEmpty() && Normalize(Name) == Norm)
			{
				OutExact = Name;
				return true;
			}
		}
		if (Norm == TEXT("output") || Norm == TEXT("out") || Norm == TEXT("result") || Norm == TEXT("returnvalue"))
		{
			return true; // the first output
		}
		if (Names.Num() == 1)
		{
			return true; // the only pin (often unnamed, e.g. the input of Sine or OneMinus): whatever the name was meant to be, there is no other choice
		}
		return false;
	}

	FString DescribeNames(const TArray<FString>& Names)
	{
		TArray<FString> Shown;
		for (const FString& Name : Names)
		{
			Shown.Add(Name.IsEmpty() ? TEXT("(unnamed)") : Name);
		}
		return Shown.Num() > 0 ? FString::Join(Shown, TEXT(", ")) : FString(TEXT("none"));
	}

	bool ParseRootProperty(const FString& Name, EMaterialProperty& Out, FString& OutValid)
	{
		struct FEntry { const TCHAR* Name; EMaterialProperty Property; };
		static const FEntry Entries[] =
		{
			{ TEXT("BaseColor"), MP_BaseColor }, { TEXT("Metallic"), MP_Metallic }, { TEXT("Specular"), MP_Specular },
			{ TEXT("Roughness"), MP_Roughness }, { TEXT("Anisotropy"), MP_Anisotropy }, { TEXT("EmissiveColor"), MP_EmissiveColor },
			{ TEXT("Opacity"), MP_Opacity }, { TEXT("OpacityMask"), MP_OpacityMask }, { TEXT("Normal"), MP_Normal },
			{ TEXT("Tangent"), MP_Tangent }, { TEXT("WorldPositionOffset"), MP_WorldPositionOffset },
			{ TEXT("SubsurfaceColor"), MP_SubsurfaceColor }, { TEXT("AmbientOcclusion"), MP_AmbientOcclusion },
			{ TEXT("Refraction"), MP_Refraction }, { TEXT("PixelDepthOffset"), MP_PixelDepthOffset },
		};
		TArray<FString> Valid;
		for (const FEntry& Entry : Entries)
		{
			Valid.Add(Entry.Name);
		}
		OutValid = FString::Join(Valid, TEXT(", "));

		FString Norm = Normalize(Name);
		if (Norm == TEXT("emissive")) { Norm = TEXT("emissivecolor"); }
		else if (Norm == TEXT("diffuse") || Norm == TEXT("albedo") || Norm == TEXT("color")) { Norm = TEXT("basecolor"); }
		else if (Norm == TEXT("ao")) { Norm = TEXT("ambientocclusion"); }
		else if (Norm == TEXT("subsurface")) { Norm = TEXT("subsurfacecolor"); }
		else if (Norm == TEXT("wpo")) { Norm = TEXT("worldpositionoffset"); }
		for (const FEntry& Entry : Entries)
		{
			if (Normalize(Entry.Name) == Norm)
			{
				Out = Entry.Property;
				return true;
			}
		}
		return false;
	}
}

namespace
{
	/** Settings of the material itself (BlendMode, TwoSided, ShadingModel...). Runs on a throw-away probe to validate, or on the asset. */
	void ApplySettings(UObject* Object, const TArray<TPair<FString, FString>>& Settings, bool bNotify, TArray<FString>& Errors)
	{
		if (bNotify)
		{
			Object->PreEditChange(static_cast<FProperty*>(nullptr));
		}
		for (const TPair<FString, FString>& Setting : Settings)
		{
			FString Error;
			UMaterial* AsMaterial = Cast<UMaterial>(Object);
			if (AsMaterial && Normalize(Setting.Key) == TEXT("shadingmodel"))
			{
				int64 ModelValue = INDEX_NONE;
				FString Valid;
				if (ResolveEnumValue(StaticEnum<EMaterialShadingModel>(), Setting.Value, ModelValue, Valid))
				{
					AsMaterial->SetShadingModel(static_cast<EMaterialShadingModel>(ModelValue));
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("Setting \"ShadingModel\": \"%s\" is not valid. Valid values: %s"), *Setting.Value, *Valid));
				}
			}
			else if (!SetPropertyFromText(Object, Setting.Key, Setting.Value, Error))
			{
				Errors.Add(TEXT("Setting ") + Error);
			}
		}
		if (bNotify)
		{
			Object->PostEditChange();
		}
	}

	FString SamplerTypeText(UMaterialExpression* Expression)
	{
		FString Text;
		if (FProperty* Prop = Expression->GetClass()->FindPropertyByName(TEXT("SamplerType")))
		{
			const void* Ptr = Prop->ContainerPtrToValuePtr<void>(Expression);
			Prop->ExportText_Direct(Text, Ptr, Ptr, Expression, PPF_None);
		}
		return Text;
	}

	/**
	 * A texture sampler whose type does not match its texture (Normal for an sRGB color map, LinearColor for a Color
	 * texture, ...) makes the material fail to compile. The type is taken from the texture, like the editor does when
	 * a texture is assigned; a sampler without a texture falls back to Color (the default texture is a color map).
	 */
	void FixSamplerType(UMaterialExpression* Expression, const FString& NodeId, TArray<FString>& Warnings)
	{
		UMaterialExpressionTextureBase* TextureNode = Cast<UMaterialExpressionTextureBase>(Expression);
		if (!TextureNode)
		{
			return;
		}
		const FString Before = SamplerTypeText(Expression);
		if (TextureNode->Texture)
		{
			TextureNode->AutoSetSampleType();
		}
		else
		{
			FString Ignored;
			SetPropertyFromText(Expression, TEXT("SamplerType"), TEXT("Color"), Ignored);
		}
		const FString After = SamplerTypeText(Expression);
		if (Before != After)
		{
			NotifyPropertyChanged(Expression, TEXT("SamplerType"));
			Warnings.Add(FString::Printf(TEXT("Node \"%s\": SamplerType was changed from %s to %s to match %s."),
				*NodeId, *Before, *After, TextureNode->Texture ? TEXT("its texture") : TEXT("a sampler without a texture")));
		}
	}
}

FABGBuildResult FABGMaterialBuilder::Build(UMaterial* Material, UMaterialFunction* Function, const FABGMaterialSpec& Spec)
{
	FABGBuildResult Result;
	FTarget Target;
	Target.Material = Material;
	Target.Function = Material ? nullptr : Function;
	if (!Target.Material && !Target.Function)
	{
		Result.Errors.Add(TEXT("No target material."));
		return Result;
	}
	UObject* Asset = Target.Function ? static_cast<UObject*>(Target.Function) : static_cast<UObject*>(Target.Material);

	Result.Notes = Spec.Unsupported;
	if (!Spec.HasWork())
	{
		Result.Errors.Add(Spec.Unsupported.Num() > 0
			? TEXT("The AI says this cannot be done with the available expressions. Its explanation is listed below.")
			: TEXT("The reply contains nothing to build."));
		return Result;
	}

	const TArray<UMaterialExpression*> ExistingBefore = GetAllExpressions(Target);
	auto FindExisting = [&](const FString& Id) -> UMaterialExpression*
	{
		for (UMaterialExpression* Expression : ExistingBefore)
		{
			if (Expression && ABGMaterialId::Make(Expression) == Id)
			{
				return Expression;
			}
		}
		return nullptr;
	};

	// =====================================================================================================
	// Phase 1: check everything on throw-away copies. Nothing in the asset is touched, so a bad answer
	// leaves the material exactly as it was (no half-built graph, no nodes piled up at the origin).
	// =====================================================================================================
	{
		TMap<FString, UMaterialExpression*> Probe; // id -> existing node, or a transient stand-in for a new one
		auto ProbeResolve = [&](const FString& Id) -> UMaterialExpression*
		{
			if (UMaterialExpression** Found = Probe.Find(Id))
			{
				return *Found;
			}
			if (Id.StartsWith(TEXT("e_"), ESearchCase::CaseSensitive))
			{
				if (UMaterialExpression* Existing = FindExisting(Id))
				{
					Probe.Add(Id, Existing);
					return Existing;
				}
			}
			return nullptr;
		};

		if (Spec.Settings.Num() > 0)
		{
			UObject* SettingsProbe = NewObject<UObject>(GetTransientPackage(), Asset->GetClass(), NAME_None, RF_Transient);
			ApplySettings(SettingsProbe, Spec.Settings, /*bNotify*/ false, Result.Errors);
		}

		for (const FABGMatNode& NodeSpec : Spec.Nodes)
		{
			UMaterialExpression* ForPins = nullptr;
			UMaterialExpression* ForProps = nullptr;
			if (NodeSpec.Type == ABGMaterialNodeType::Existing)
			{
				ForPins = FindExisting(NodeSpec.Id);
				if (!ForPins)
				{
					Result.Errors.Add(FString::Printf(TEXT("Existing node \"%s\" was not found in this material."), *NodeSpec.Id));
					continue;
				}
				if (NodeSpec.Props.Num() > 0)
				{
					ForProps = DuplicateObject<UMaterialExpression>(ForPins, GetTransientPackage());
				}
			}
			else
			{
				FString Suggestions;
				UClass* Class = ResolveExpressionClass(NodeSpec.Type, Suggestions);
				if (!Class)
				{
					Result.Errors.Add(FString::Printf(TEXT("Node \"%s\": there is no material expression \"%s\".%s"), *NodeSpec.Id, *NodeSpec.Type,
						Suggestions.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Similar: %s."), *Suggestions)));
					continue;
				}
				ForProps = NewObject<UMaterialExpression>(GetTransientPackage(), Class, NAME_None, RF_Transient);
				ForPins = ForProps;
			}
			Probe.Add(NodeSpec.Id, ForPins);

			if (ForProps)
			{
				for (const TPair<FString, FString>& Prop : NodeSpec.Props)
				{
					FString Error;
					if (!SetPropertyFromText(ForProps, Prop.Key, Prop.Value, Error))
					{
						Result.Errors.Add(FString::Printf(TEXT("Node \"%s\": %s"), *NodeSpec.Id, *Error));
					}
				}
			}
		}

		for (const FABGMatConnection& Conn : Spec.Connections)
		{
			UMaterialExpression* From = ProbeResolve(Conn.From.NodeId);
			if (!From)
			{
				Result.Errors.Add(FString::Printf(TEXT("Connection: node \"%s\" was not found."), *Conn.From.NodeId));
				continue;
			}
			const TArray<FString> OutputNames = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(From);
			FString OutputExact;
			if (!ResolvePin(OutputNames, Conn.From.PinName, OutputExact))
			{
				Result.Errors.Add(FString::Printf(TEXT("Node \"%s\" has no output \"%s\" (outputs: %s)."), *Conn.From.NodeId, *Conn.From.PinName, *DescribeNames(OutputNames)));
				continue;
			}

			if (Conn.To.NodeId.Equals(ABGMaterialRootName, ESearchCase::IgnoreCase))
			{
				EMaterialProperty Property = MP_BaseColor;
				FString Valid;
				if (!Target.Material)
				{
					Result.Errors.Add(TEXT("A Material Function has no material outputs: connect to a FunctionOutput expression instead."));
				}
				else if (!ParseRootProperty(Conn.To.PinName, Property, Valid))
				{
					Result.Errors.Add(FString::Printf(TEXT("The material has no output \"%s\" (outputs: %s)."), *Conn.To.PinName, *Valid));
				}
				continue;
			}

			UMaterialExpression* To = ProbeResolve(Conn.To.NodeId);
			if (!To)
			{
				Result.Errors.Add(FString::Printf(TEXT("Connection: node \"%s\" was not found."), *Conn.To.NodeId));
				continue;
			}
			const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(To);
			FString InputExact;
			if (!ResolvePin(InputNames, Conn.To.PinName, InputExact))
			{
				Result.Errors.Add(FString::Printf(TEXT("Node \"%s\" has no input \"%s\" (inputs: %s)."), *Conn.To.NodeId, *Conn.To.PinName, *DescribeNames(InputNames)));
			}
		}

		for (const FABGMatPinRef& Ref : Spec.Disconnect)
		{
			if (Ref.NodeId.Equals(ABGMaterialRootName, ESearchCase::IgnoreCase))
			{
				EMaterialProperty Property = MP_BaseColor;
				FString Valid;
				if (!Target.Material || !ParseRootProperty(Ref.PinName, Property, Valid))
				{
					Result.Errors.Add(FString::Printf(TEXT("Cannot disconnect Material.%s (outputs: %s)."), *Ref.PinName, *Valid));
				}
				continue;
			}
			UMaterialExpression* Node = ProbeResolve(Ref.NodeId);
			const TArray<FString> InputNames = Node ? UMaterialEditingLibrary::GetMaterialExpressionInputNames(Node) : TArray<FString>();
			FString InputExact;
			if (!Node || !ResolvePin(InputNames, Ref.PinName, InputExact))
			{
				Result.Errors.Add(FString::Printf(TEXT("Cannot disconnect %s.%s: %s."), *Ref.NodeId, *Ref.PinName,
					Node ? *FString::Printf(TEXT("inputs are: %s"), *DescribeNames(InputNames)) : TEXT("node not found")));
			}
		}

		for (const FString& Id : Spec.Remove)
		{
			if (!FindExisting(Id))
			{
				Result.Errors.Add(FString::Printf(TEXT("Cannot remove \"%s\": no expression with this id in the material."), *Id));
			}
		}

		if (Result.Errors.Num() > 0)
		{
			Result.bSuccess = false;
			return Result; // the asset was not touched
		}
	}

	// =====================================================================================================
	// Phase 2: apply. The names were checked above, so errors here are unexpected; if one happens anyway
	// the new expressions are deleted again.
	// =====================================================================================================
	FScopedTransaction Transaction(LOCTEXT("MaterialTransaction", "AI Blueprint: Apply material graph"));
	Asset->Modify();

	TMap<FString, UMaterialExpression*> NodesById;
	TArray<UMaterialExpression*> NewNodes;
	auto Resolve = [&](const FString& Id) -> UMaterialExpression*
	{
		if (UMaterialExpression** Found = NodesById.Find(Id))
		{
			return *Found;
		}
		if (Id.StartsWith(TEXT("e_"), ESearchCase::CaseSensitive))
		{
			if (UMaterialExpression* Existing = FindExisting(Id))
			{
				NodesById.Add(Id, Existing);
				return Existing;
			}
		}
		return nullptr;
	};

	if (Spec.Settings.Num() > 0)
	{
		ApplySettings(Asset, Spec.Settings, /*bNotify*/ true, Result.Errors);
	}

	for (const FABGMatNode& NodeSpec : Spec.Nodes)
	{
		UMaterialExpression* Expression = nullptr;
		if (NodeSpec.Type == ABGMaterialNodeType::Existing)
		{
			Expression = FindExisting(NodeSpec.Id);
			if (!Expression)
			{
				Result.Errors.Add(FString::Printf(TEXT("Existing node \"%s\" was not found in this material."), *NodeSpec.Id));
				continue;
			}
		}
		else
		{
			FString Suggestions;
			UClass* Class = ResolveExpressionClass(NodeSpec.Type, Suggestions);
			if (!Class)
			{
				Result.Errors.Add(FString::Printf(TEXT("Node \"%s\": there is no material expression \"%s\"."), *NodeSpec.Id, *NodeSpec.Type));
				continue;
			}
			Expression = UMaterialEditingLibrary::CreateMaterialExpressionEx(Target.Material, Target.Function, Class, nullptr, 0, 0, true);
			if (!Expression)
			{
				Result.Errors.Add(FString::Printf(TEXT("Node \"%s\": the expression %s could not be created."), *NodeSpec.Id, *NodeSpec.Type));
				continue;
			}
			NewNodes.Add(Expression);
			++Result.NodesCreated;
		}
		NodesById.Add(NodeSpec.Id, Expression);

		for (const TPair<FString, FString>& Prop : NodeSpec.Props)
		{
			FString Error;
			if (SetPropertyFromText(Expression, Prop.Key, Prop.Value, Error))
			{
				NotifyPropertyChanged(Expression, Prop.Key);
			}
			else
			{
				Result.Errors.Add(FString::Printf(TEXT("Node \"%s\": %s"), *NodeSpec.Id, *Error));
			}
		}
		if (NodeSpec.Props.Num() > 0 || NodeSpec.Type != ABGMaterialNodeType::Existing)
		{
			FixSamplerType(Expression, NodeSpec.Id, Result.Warnings);
		}
	}

	if (Result.Errors.Num() == 0)
	{
		for (const FABGMatConnection& Conn : Spec.Connections)
		{
			UMaterialExpression* From = Resolve(Conn.From.NodeId);
			if (!From)
			{
				Result.Errors.Add(FString::Printf(TEXT("Connection: node \"%s\" was not found."), *Conn.From.NodeId));
				continue;
			}
			const TArray<FString> OutputNames = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(From);
			FString OutputExact;
			if (!ResolvePin(OutputNames, Conn.From.PinName, OutputExact))
			{
				Result.Errors.Add(FString::Printf(TEXT("Node \"%s\" has no output \"%s\" (outputs: %s)."), *Conn.From.NodeId, *Conn.From.PinName, *DescribeNames(OutputNames)));
				continue;
			}

			if (Conn.To.NodeId.Equals(ABGMaterialRootName, ESearchCase::IgnoreCase))
			{
				EMaterialProperty Property = MP_BaseColor;
				FString Valid;
				if (!Target.Material || !ParseRootProperty(Conn.To.PinName, Property, Valid))
				{
					Result.Errors.Add(FString::Printf(TEXT("The material has no output \"%s\" (outputs: %s)."), *Conn.To.PinName, *Valid));
					continue;
				}
				if (UMaterialEditingLibrary::ConnectMaterialProperty(From, OutputExact, Property))
				{
					++Result.ConnectionsMade;
				}
				else
				{
					Result.Errors.Add(FString::Printf(TEXT("Cannot connect %s.%s to Material.%s."), *Conn.From.NodeId, *Conn.From.PinName, *Conn.To.PinName));
				}
				continue;
			}

			UMaterialExpression* To = Resolve(Conn.To.NodeId);
			if (!To)
			{
				Result.Errors.Add(FString::Printf(TEXT("Connection: node \"%s\" was not found."), *Conn.To.NodeId));
				continue;
			}
			const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(To);
			FString InputExact;
			if (!ResolvePin(InputNames, Conn.To.PinName, InputExact))
			{
				Result.Errors.Add(FString::Printf(TEXT("Node \"%s\" has no input \"%s\" (inputs: %s)."), *Conn.To.NodeId, *Conn.To.PinName, *DescribeNames(InputNames)));
				continue;
			}
			if (UMaterialEditingLibrary::ConnectMaterialExpressions(From, OutputExact, To, InputExact))
			{
				++Result.ConnectionsMade;
			}
			else
			{
				Result.Errors.Add(FString::Printf(TEXT("Cannot connect %s.%s -> %s.%s."), *Conn.From.NodeId, *Conn.From.PinName, *Conn.To.NodeId, *Conn.To.PinName));
			}
		}

		for (const FABGMatPinRef& Ref : Spec.Disconnect)
		{
			if (Ref.NodeId.Equals(ABGMaterialRootName, ESearchCase::IgnoreCase))
			{
				EMaterialProperty Property = MP_BaseColor;
				FString Valid;
				if (Target.Material && ParseRootProperty(Ref.PinName, Property, Valid)
					&& !UMaterialEditingLibrary::DisconnectMaterialProperty(Target.Material, Property))
				{
					Result.Warnings.Add(FString::Printf(TEXT("Material.%s had no connection to remove."), *Ref.PinName));
				}
				continue;
			}
			UMaterialExpression* Node = Resolve(Ref.NodeId);
			const TArray<FString> InputNames = Node ? UMaterialEditingLibrary::GetMaterialExpressionInputNames(Node) : TArray<FString>();
			FString InputExact;
			if (Node && ResolvePin(InputNames, Ref.PinName, InputExact)
				&& !UMaterialEditingLibrary::DisconnectMaterialExpressions(Node, InputExact))
			{
				Result.Warnings.Add(FString::Printf(TEXT("%s.%s had no connection to remove."), *Ref.NodeId, *Ref.PinName));
			}
		}
	}

	// ---- unexpected error while applying: take the new expressions out again ----
	if (Result.Errors.Num() > 0)
	{
		for (UMaterialExpression* Node : NewNodes)
		{
			if (Target.Function)
			{
				UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Target.Function, Node);
			}
			else
			{
				UMaterialEditingLibrary::DeleteMaterialExpression(Target.Material, Node);
			}
		}
		Transaction.Cancel();
		if (Target.Function)
		{
			UMaterialEditingLibrary::RefreshMaterialFunctionEditor(Target.Function);
		}
		else
		{
			UMaterialEditingLibrary::RefreshMaterialEditor(Target.Material);
		}
		Result.Errors.Add(TEXT("The new expressions were removed again. Properties or wires of existing expressions may have been changed before the error: press Ctrl+Z in the material editor if the graph looks wrong."));
		Result.NodesCreated = 0;
		Result.ConnectionsMade = 0;
		Result.bSuccess = false;
		return Result;
	}

	// ---- removals ----
	for (const FString& Id : Spec.Remove)
	{
		if (UMaterialExpression* Doomed = FindExisting(Id))
		{
			if (Target.Function)
			{
				UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Target.Function, Doomed);
			}
			else
			{
				UMaterialEditingLibrary::DeleteMaterialExpression(Target.Material, Doomed);
			}
			++Result.NodesRemoved;
		}
	}

	// ---- layout of the new nodes: columns by distance along the wires, placed clear of the existing nodes ----
	if (NewNodes.Num() > 0)
	{
		TMap<const UMaterialExpression*, int32> Layer;
		for (const UMaterialExpression* Node : NewNodes)
		{
			Layer.Add(Node, 0);
		}
		for (int32 Pass = 0; Pass < NewNodes.Num(); ++Pass)
		{
			bool bChanged = false;
			for (const FABGMatConnection& Conn : Spec.Connections)
			{
				const UMaterialExpression* From = Resolve(Conn.From.NodeId);
				const UMaterialExpression* To = Resolve(Conn.To.NodeId);
				if (From && To && Layer.Contains(From) && Layer.Contains(To) && Layer[To] < Layer[From] + 1)
				{
					Layer[To] = Layer[From] + 1;
					bChanged = true;
				}
			}
			if (!bChanged)
			{
				break;
			}
		}
		int32 MaxLayer = 0;
		for (const TPair<const UMaterialExpression*, int32>& Pair : Layer)
		{
			MaxLayer = FMath::Max(MaxLayer, Pair.Value);
		}

		FIntPoint Origin(0, 0);
		bool bAny = false;
		int32 MinX = 0, MaxY = 0;
		for (const UMaterialExpression* Existing : ExistingBefore)
		{
			if (Existing)
			{
				MinX = bAny ? FMath::Min(MinX, Existing->MaterialExpressionEditorX) : Existing->MaterialExpressionEditorX;
				MaxY = bAny ? FMath::Max(MaxY, Existing->MaterialExpressionEditorY) : Existing->MaterialExpressionEditorY;
				bAny = true;
			}
		}
		if (bAny)
		{
			Origin = FIntPoint(MinX, MaxY + BlockGapY);
		}
		else if (Target.Material)
		{
			Origin = FIntPoint(-400 - MaxLayer * NodeSpacingX, 0); // the material's own output node sits near (0,0)
		}

		TMap<int32, int32> RowInLayer;
		for (UMaterialExpression* Node : NewNodes)
		{
			const int32 NodeLayer = Layer[Node];
			const int32 Row = RowInLayer.FindOrAdd(NodeLayer)++;
			Node->MaterialExpressionEditorX = Origin.X + NodeLayer * NodeSpacingX;
			Node->MaterialExpressionEditorY = Origin.Y + Row * NodeSpacingY;
		}
	}

	// ---- compile and refresh ----
	if (Target.Function)
	{
		UMaterialEditingLibrary::UpdateMaterialFunction(Target.Function, nullptr);
		UMaterialEditingLibrary::RefreshMaterialFunctionEditor(Target.Function);
		Result.bCompiled = true;
	}
	else
	{
		const TArray<FString> CompileMessages = UMaterialEditingLibrary::RecompileMaterial(Target.Material);
		Result.CompileErrors = CompileMessages;
		Result.bCompiled = (CompileMessages.Num() == 0);
		UMaterialEditingLibrary::RefreshMaterialEditor(Target.Material);
	}
	Asset->MarkPackageDirty();

	Result.bSuccess = true;
	UE_LOG(LogAIBlueprintGen, Log, TEXT("Material applied: %d expression(s) created, %d connection(s), %d removed, compiled=%d"),
		Result.NodesCreated, Result.ConnectionsMade, Result.NodesRemoved, Result.bCompiled ? 1 : 0);
	return Result;
}

int32 FABGMaterialBuilder::Arrange(UMaterial* Material, UMaterialFunction* Function)
{
	FTarget Target;
	Target.Material = Material;
	Target.Function = Material ? nullptr : Function;
	if (!Target.Material && !Target.Function)
	{
		return 0;
	}
	UObject* Asset = Target.Function ? static_cast<UObject*>(Target.Function) : static_cast<UObject*>(Target.Material);

	TArray<UMaterialExpression*> Nodes;
	for (UMaterialExpression* Expression : GetAllExpressions(Target))
	{
		if (Expression && !Expression->IsA<UMaterialExpressionComment>())
		{
			Nodes.Add(Expression);
		}
	}
	if (Nodes.Num() == 0)
	{
		return 0;
	}
	const TSet<UMaterialExpression*> NodeSet(Nodes);

	TMap<UMaterialExpression*, TArray<UMaterialExpression*>> Sources;
	for (UMaterialExpression* Node : Nodes)
	{
		TArray<UMaterialExpression*>& List = Sources.Add(Node);
		for (FExpressionInputIterator It{ Node }; It; ++It)
		{
			if (It->Expression && NodeSet.Contains(It->Expression))
			{
				List.AddUnique(It->Expression);
			}
		}
	}

	// longest path from the sources (a pass limit protects against loops)
	TMap<UMaterialExpression*, int32> Layer;
	for (UMaterialExpression* Node : Nodes)
	{
		Layer.Add(Node, 0);
	}
	for (int32 Pass = 0; Pass <= Nodes.Num(); ++Pass)
	{
		bool bChanged = false;
		for (UMaterialExpression* Node : Nodes)
		{
			for (UMaterialExpression* Source : Sources[Node])
			{
				if (Layer[Node] < Layer[Source] + 1 && Pass < Nodes.Num())
				{
					Layer[Node] = Layer[Source] + 1;
					bChanged = true;
				}
			}
		}
		if (!bChanged)
		{
			break;
		}
	}

	int32 MaxLayer = 0;
	for (const TPair<UMaterialExpression*, int32>& Pair : Layer)
	{
		MaxLayer = FMath::Max(MaxLayer, Pair.Value);
	}

	constexpr int32 MaxRowsPerColumn = 8;
	struct FColumn { TArray<UMaterialExpression*> Items; };
	TArray<FColumn> Columns;
	for (int32 L = 0; L <= MaxLayer; ++L)
	{
		TArray<UMaterialExpression*> InLayer;
		for (UMaterialExpression* Node : Nodes)
		{
			if (Layer[Node] == L)
			{
				InLayer.Add(Node);
			}
		}
		// stable and deterministic: by the old position, then by name
		InLayer.Sort([](const UMaterialExpression& A, const UMaterialExpression& B)
		{
			if (A.MaterialExpressionEditorY != B.MaterialExpressionEditorY)
			{
				return A.MaterialExpressionEditorY < B.MaterialExpressionEditorY;
			}
			if (A.MaterialExpressionEditorX != B.MaterialExpressionEditorX)
			{
				return A.MaterialExpressionEditorX < B.MaterialExpressionEditorX;
			}
			return A.GetName() < B.GetName();
		});
		for (int32 Start = 0; Start < InLayer.Num(); Start += MaxRowsPerColumn)
		{
			FColumn& Column = Columns.AddDefaulted_GetRef();
			for (int32 i = Start; i < FMath::Min(Start + MaxRowsPerColumn, InLayer.Num()); ++i)
			{
				Column.Items.Add(InLayer[i]);
			}
		}
	}

	FScopedTransaction Transaction(LOCTEXT("ArrangeTransaction", "AI Blueprint: Arrange material nodes"));
	Asset->Modify();
	const int32 ColumnCount = Columns.Num();
	int32 Moved = 0;
	for (int32 C = 0; C < ColumnCount; ++C)
	{
		// the last column ends next to the material's own output node (a material) or at X = 0 (a function)
		const int32 X = Target.Material ? (C - ColumnCount) * NodeSpacingX - 100 : (C - (ColumnCount - 1)) * NodeSpacingX;
		const int32 Rows = Columns[C].Items.Num();
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			UMaterialExpression* Node = Columns[C].Items[Row];
			Node->Modify();
			Node->MaterialExpressionEditorX = X;
			Node->MaterialExpressionEditorY = FMath::RoundToInt((Row - (Rows - 1) * 0.5f) * NodeSpacingY);
			++Moved;
		}
	}

	Asset->MarkPackageDirty();
	if (Target.Function)
	{
		UMaterialEditingLibrary::RefreshMaterialFunctionEditor(Target.Function);
	}
	else
	{
		UMaterialEditingLibrary::RefreshMaterialEditor(Target.Material);
	}
	UE_LOG(LogAIBlueprintGen, Log, TEXT("Arranged %d expression(s) in %d column(s)."), Moved, ColumnCount);
	return Moved;
}

#undef LOCTEXT_NAMESPACE
