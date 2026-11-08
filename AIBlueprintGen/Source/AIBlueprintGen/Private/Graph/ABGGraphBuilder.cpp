#include "Graph/ABGGraphBuilder.h"
#include "Core/AIBlueprintGenLog.h"

#include "BlueprintEditorModule.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Graph/ABGNodeId.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "ABGGraphBuilder"

namespace
{
	/** Spacing of the simple auto-layout used for newly created nodes. */
	constexpr int32 TempNodeSpacingX = 420;
	constexpr int32 TempNodeSpacingY = 240;
	/** Gap kept between the existing nodes and the block of new nodes. */
	constexpr int32 NewBlockGapY = 300;

	/** Lower-case letters and digits only: "Manual Attachment" == "bManualAttachment" minus the b. */
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

	/** Exact name first, then forgiving matches (display name, spaces, a missing bool "b" prefix). */
	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->PinName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}

		const FString Wanted = Normalize(Name);
		if (Wanted.IsEmpty())
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != Direction || Pin->bHidden)
			{
				continue;
			}
			const FString Raw = Normalize(Pin->PinName.ToString());
			if (Raw == Wanted || Raw == TEXT("b") + Wanted || Normalize(Pin->GetDisplayName().ToString()) == Wanted)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** FindFunctionByName plus the usual Blueprint naming gaps: K2_ prefix and display names ("Add Local Rotation"). */
	UFunction* ResolveFunction(UClass* Class, const FString& Name, FString& OutSuggestions)
	{
		auto IsCallable = [](const UFunction* F) { return F && F->HasAnyFunctionFlags(FUNC_BlueprintCallable); };

		if (UFunction* F = Class->FindFunctionByName(FName(*Name)); IsCallable(F))
		{
			return F;
		}
		if (UFunction* F = Class->FindFunctionByName(FName(*(TEXT("K2_") + Name))); IsCallable(F))
		{
			return F;
		}

		const FString Wanted = Normalize(Name);
		TArray<FString> Similar;
		for (TFieldIterator<UFunction> It(Class); It; ++It)
		{
			UFunction* F = *It;
			if (!IsCallable(F))
			{
				continue;
			}
			FString Raw = F->GetName();
			const FString Display = F->GetMetaData(TEXT("DisplayName"));
			if (Raw.StartsWith(TEXT("K2_")))
			{
				Raw.RightChopInline(3);
			}
			if (Normalize(Raw) == Wanted || (!Display.IsEmpty() && Normalize(Display) == Wanted))
			{
				return F;
			}
			if (!Wanted.IsEmpty() && Similar.Num() < 6 && (Normalize(Raw).Contains(Wanted) || Wanted.Contains(Normalize(Raw))))
			{
				Similar.AddUnique(F->GetName());
			}
		}
		OutSuggestions = FString::Join(Similar, TEXT(", "));
		return nullptr;
	}

	FString ListPinNames(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		TArray<FString> Names;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && !Pin->bHidden)
			{
				Names.Add(Pin->PinName.ToString());
			}
		}
		return FString::Join(Names, TEXT(", "));
	}


	// ------------------------------------------------------------------ classes / assets / types

	/** "StaticMeshActor", "AStaticMeshActor", "/Script/Engine.StaticMeshActor" or "/Game/BP/BP_Cube.BP_Cube_C". */
	UClass* ResolveClass(const FString& InName)
	{
		const FString Name = InName.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			return nullptr;
		}
		if (Name.StartsWith(TEXT("/")))
		{
			if (UClass* Loaded = LoadObject<UClass>(nullptr, *Name))
			{
				return Loaded;
			}
			return Name.EndsWith(TEXT("_C")) ? nullptr : LoadObject<UClass>(nullptr, *(Name + TEXT("_C")));
		}
		if (UClass* Found = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst))
		{
			return Found;
		}
		if (UClass* Found = FindFirstObject<UClass>(*(Name + TEXT("_C")), EFindFirstObjectOptions::NativeFirst))
		{
			return Found;
		}
		if (Name.Len() > 1 && (Name[0] == TEXT('U') || Name[0] == TEXT('A')))
		{
			return FindFirstObject<UClass>(*Name.Mid(1), EFindFirstObjectOptions::NativeFirst);
		}
		return nullptr;
	}

	/** Asset by full path ("/Game/Meshes/Rock") or one of the engine's basic shapes. */
	UObject* ResolveAsset(const FString& InValue)
	{
		const FString Value = InValue.TrimStartAndEnd();
		if (Value.IsEmpty() || Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			return nullptr;
		}
		if (Value.StartsWith(TEXT("/")))
		{
			FString Path = Value;
			if (!Path.Contains(TEXT(".")))
			{
				Path += TEXT(".") + FPaths::GetCleanFilename(Value);
			}
			return StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
		}
		static const TCHAR* const Shapes[] = { TEXT("Cube"), TEXT("Sphere"), TEXT("Cylinder"), TEXT("Cone"), TEXT("Plane") };
		for (const TCHAR* Shape : Shapes)
		{
			if (Value.Equals(Shape, ESearchCase::IgnoreCase))
			{
				return StaticLoadObject(UObject::StaticClass(), nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape));
			}
		}
		return nullptr;
	}

	/** Sets a pin default; class/asset pins need real objects, everything else is text. */
	bool ApplyPinDefault(const UEdGraphSchema_K2* Schema, UEdGraphPin* Pin, const FString& Value, FString& OutError)
	{
		const FName Category = Pin->PinType.PinCategory;

		if (Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass)
		{
			UClass* Class = ResolveClass(Value);
			if (!Class)
			{
				OutError = FString::Printf(TEXT("class \"%s\" not found (use the class name, or a full path like /Game/Folder/BP_Name.BP_Name_C)."), *Value);
				return false;
			}
			if (Category == UEdGraphSchema_K2::PC_SoftClass)
			{
				Schema->TrySetDefaultValue(*Pin, Class->GetPathName());
				return true;
			}
			Schema->TrySetDefaultObject(*Pin, Class);
			if (Pin->DefaultObject != Class)
			{
				OutError = FString::Printf(TEXT("class \"%s\" is not allowed for this pin."), *Value);
				return false;
			}
			return true;
		}

		if (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_SoftObject)
		{
			UObject* Asset = ResolveAsset(Value);
			if (!Asset)
			{
				OutError = FString::Printf(TEXT("asset \"%s\" not found (use a full path like /Game/Folder/Asset, or Cube, Sphere, Cylinder, Cone, Plane)."), *Value);
				return false;
			}
			if (Category == UEdGraphSchema_K2::PC_SoftObject)
			{
				Schema->TrySetDefaultValue(*Pin, Asset->GetPathName());
				return true;
			}
			Schema->TrySetDefaultObject(*Pin, Asset);
			if (Pin->DefaultObject != Asset)
			{
				OutError = FString::Printf(TEXT("asset \"%s\" has the wrong type for this pin."), *Value);
				return false;
			}
			return true;
		}

		Schema->TrySetDefaultValue(*Pin, Value);
		return true;
	}

	bool MakeVariableType(const FString& TypeName, FEdGraphPinType& OutType, FString& OutError)
	{
		const FString T = Normalize(TypeName);
		OutType = FEdGraphPinType();
		if (T == TEXT("bool") || T == TEXT("boolean"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (T == TEXT("int") || T == TEXT("integer") || T == TEXT("int32"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (T == TEXT("float") || T == TEXT("double") || T == TEXT("real"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (T == TEXT("string"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (T == TEXT("name"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (T == TEXT("text"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Text;
		}
		else if (T == TEXT("vector"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (T == TEXT("rotator"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else if (T == TEXT("transform"))
		{
			OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
		}
		else
		{
			UClass* Class = ResolveClass(TypeName);
			if (!Class)
			{
				OutError = FString::Printf(TEXT("unknown type \"%s\" (use bool, int, float, string, name, text, vector, rotator, transform or a class name like Actor)."), *TypeName);
				return false;
			}
			OutType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutType.PinSubCategoryObject = Class;
		}
		return true;
	}

	FString DescribePinType(const UEdGraphPin* Pin)
	{
		const FEdGraphPinType& Type = Pin->PinType;
		FString Result = Type.PinCategory.ToString();
		if (const UObject* Object = Type.PinSubCategoryObject.Get())
		{
			Result += TEXT(" ") + Object->GetName();
		}
		else if (!Type.PinSubCategory.IsNone())
		{
			Result += TEXT(" ") + Type.PinSubCategory.ToString();
		}
		return Result;
	}

	/** Exact/forgiving name match, plus "value"/"result" aliases for variable, cast and spawn nodes. */
	UEdGraphPin* FindPinWithAliases(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction)
	{
		if (UEdGraphPin* Pin = FindPinByName(Node, Name, Direction))
		{
			return Pin;
		}
		const FString Wanted = Normalize(Name);
		const bool bAlias = Wanted == TEXT("returnvalue") || Wanted == TEXT("value") || Wanted == TEXT("result") || Wanted == TEXT("output") || Wanted == TEXT("out");
		if (bAlias && (Cast<UK2Node_Variable>(Node) || Cast<UK2Node_DynamicCast>(Node)))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == Direction && !Pin->bHidden && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
				{
					return Pin;
				}
			}
		}
		return nullptr;
	}

	// ------------------------------------------------------------------ variables

	bool VariableExists(const UBlueprint* Blueprint, const FName Name, FString& OutAvailable)
	{
		TArray<FString> Names;
		bool bFound = false;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			Names.AddUnique(Var.VarName.ToString());
			bFound |= (Var.VarName == Name);
		}
		if (const UClass* Skeleton = Blueprint->SkeletonGeneratedClass)
		{
			bFound |= (Skeleton->FindPropertyByName(Name) != nullptr);
			for (TFieldIterator<FObjectProperty> It(Skeleton); It; ++It)
			{
				if (It->GetOwnerStruct() == Skeleton) // components made in the Components panel
				{
					Names.AddUnique(It->GetName());
				}
			}
		}
		OutAvailable = FString::Join(Names, TEXT(", "));
		return bFound;
	}

	UEdGraphNode* SpawnVariableNode(UBlueprint* Blueprint, UEdGraph* Graph, const FABGNodeSpec& Spec, bool bSet, TArray<FString>& Errors)
	{
		const FName VarName(*Spec.Variable);
		FString Available;
		if (!VariableExists(Blueprint, VarName, Available))
		{
			Errors.Add(FString::Printf(TEXT("Node \"%s\": variable \"%s\" does not exist%s"), *Spec.Id, *Spec.Variable,
				Available.IsEmpty() ? TEXT(". Declare it in \"variables\".") : *FString::Printf(TEXT(" (available: %s). Declare it in \"variables\" if it is new."), *Available)));
			return nullptr;
		}
		if (bSet)
		{
			FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
			UK2Node_VariableSet* Node = Creator.CreateNode();
			Node->VariableReference.SetSelfMember(VarName);
			Creator.Finalize();
			return Node;
		}
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode();
		Node->VariableReference.SetSelfMember(VarName);
		Creator.Finalize();
		return Node;
	}

	// ------------------------------------------------------------------ other node kinds

	UEdGraphNode* SpawnSpawnActorNode(UEdGraph* Graph, const FABGNodeSpec& Spec, TArray<FString>& Errors)
	{
		UClass* Class = nullptr;
		if (!Spec.ClassName.IsEmpty())
		{
			Class = ResolveClass(Spec.ClassName);
			if (!Class || !Class->IsChildOf(AActor::StaticClass()))
			{
				Errors.Add(FString::Printf(TEXT("Node \"%s\": \"%s\" is not an Actor class."), *Spec.Id, *Spec.ClassName));
				return nullptr;
			}
		}

		FGraphNodeCreator<UK2Node_SpawnActorFromClass> Creator(*Graph);
		UK2Node_SpawnActorFromClass* Node = Creator.CreateNode();
		// This node's PostPlacedNewNode() (run by Finalize) reads its pins, so they must exist first.
		Node->AllocateDefaultPins();
		Creator.Finalize();

		if (Class)
		{
			if (UEdGraphPin* ClassPin = FindPinByName(Node, TEXT("Class"), EGPD_Input))
			{
				GetDefault<UEdGraphSchema_K2>()->TrySetDefaultObject(*ClassPin, Class);
				Node->PinDefaultValueChanged(ClassPin); // retypes ReturnValue to the chosen class
			}
		}
		return Node;
	}

	UEdGraphNode* SpawnCastNode(UEdGraph* Graph, const FABGNodeSpec& Spec, TArray<FString>& Errors)
	{
		UClass* Class = ResolveClass(Spec.ClassName);
		if (!Class)
		{
			Errors.Add(FString::Printf(TEXT("Node \"%s\": cast target class \"%s\" not found."), *Spec.Id, *Spec.ClassName));
			return nullptr;
		}

		FGraphNodeCreator<UK2Node_DynamicCast> Creator(*Graph);
		UK2Node_DynamicCast* Node = Creator.CreateNode();
		Node->TargetType = Class;
		Creator.Finalize();
		if (!FindPinByName(Node, TEXT("execute"), EGPD_Input))
		{
			Node->SetPurity(false); // always use the exec version so "then"/"CastFailed" exist
		}
		return Node;
	}

	UEdGraphNode* SpawnSelfNode(UEdGraph* Graph)
	{
		FGraphNodeCreator<UK2Node_Self> Creator(*Graph);
		UK2Node_Self* Node = Creator.CreateNode();
		Creator.Finalize();
		return Node;
	}

	UEdGraphNode* SpawnEventNode(UBlueprint* Blueprint, UEdGraph* Graph, const FABGNodeSpec& Spec, TArray<FString>& Errors)
	{
		if (!Blueprint->ParentClass)
		{
			Errors.Add(FString::Printf(TEXT("Node \"%s\": Blueprint has no parent class."), *Spec.Id));
			return nullptr;
		}

		const FName EventName(*Spec.Event);
		UFunction* EventFunction = Blueprint->ParentClass->FindFunctionByName(EventName);
		if (!EventFunction || !EventFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
		{
			Errors.Add(FString::Printf(TEXT("Node \"%s\": \"%s\" is not an overridable event of %s."),
				*Spec.Id, *Spec.Event, *Blueprint->ParentClass->GetName()));
			return nullptr;
		}
		UClass* OwnerClass = EventFunction->GetOwnerClass();

		// New Blueprints already contain "ghost" (disabled) BeginPlay/Tick/... nodes: reuse them,
		// creating a second override of the same event would fail to compile.
		if (UK2Node_Event* Existing = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, OwnerClass, EventName))
		{
			if (Existing->GetGraph() != Graph)
			{
				Errors.Add(FString::Printf(TEXT("Node \"%s\": event \"%s\" already exists in another graph."), *Spec.Id, *Spec.Event));
				return nullptr;
			}
			Existing->Modify();
			if (Existing->IsAutomaticallyPlacedGhostNode())
			{
				Existing->SetEnabledState(ENodeEnabledState::Enabled, /*bUserAction*/ false);
				Existing->NodeComment.Empty();
				Existing->bCommentBubbleVisible = false;
			}
			return Existing;
		}

		FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
		UK2Node_Event* Node = Creator.CreateNode();
		Node->EventReference.SetExternalMember(EventName, OwnerClass);
		Node->bOverrideFunction = true;
		Creator.Finalize();
		return Node;
	}

	UEdGraphNode* SpawnCallFunctionNode(UBlueprint* Blueprint, UEdGraph* Graph, const FABGNodeSpec& Spec, TArray<FString>& Errors)
	{
		FString ClassName, FunctionName;
		if (!Spec.Function.Split(TEXT("."), &ClassName, &FunctionName) || ClassName.IsEmpty() || FunctionName.IsEmpty())
		{
			Errors.Add(FString::Printf(TEXT("Node \"%s\": \"function\" must look like \"Class.Function\"."), *Spec.Id));
			return nullptr;
		}

		// "self.MyFunction": a function (or event dispatcher call) of this very Blueprint.
		if (ClassName.Equals(TEXT("self"), ESearchCase::IgnoreCase) || ClassName.Equals(TEXT("this"), ESearchCase::IgnoreCase))
		{
			const FName FunctionFName(*FunctionName);
			const UClass* Skeleton = Blueprint->SkeletonGeneratedClass;
			if (!Skeleton || !Skeleton->FindFunctionByName(FunctionFName))
			{
				TArray<FString> Names;
				for (const UEdGraph* FunctionGraph : Blueprint->FunctionGraphs)
				{
					if (FunctionGraph)
					{
						Names.Add(FunctionGraph->GetName());
					}
				}
				Errors.Add(FString::Printf(TEXT("Node \"%s\": this Blueprint has no function \"%s\" (its functions: %s). Declare it in \"functions\" first."),
					*Spec.Id, *FunctionName, Names.Num() > 0 ? *FString::Join(Names, TEXT(", ")) : TEXT("none")));
				return nullptr;
			}
			FGraphNodeCreator<UK2Node_CallFunction> SelfCreator(*Graph);
			UK2Node_CallFunction* SelfNode = SelfCreator.CreateNode();
			SelfNode->FunctionReference.SetSelfMember(FunctionFName);
			SelfCreator.Finalize();
			return SelfNode;
		}

		UClass* FunctionClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
		if (!FunctionClass && ClassName.Len() > 1 && (ClassName[0] == TEXT('U') || ClassName[0] == TEXT('A')))
		{
			// Tolerate C++-style prefixes (UKismetSystemLibrary -> KismetSystemLibrary).
			FunctionClass = FindFirstObject<UClass>(*ClassName.Mid(1), EFindFirstObjectOptions::NativeFirst);
		}
		if (!FunctionClass)
		{
			Errors.Add(FString::Printf(TEXT("Node \"%s\": class \"%s\" not found."), *Spec.Id, *ClassName));
			return nullptr;
		}

		FString Suggestions;
		UFunction* Function = ResolveFunction(FunctionClass, FunctionName, Suggestions);
		if (!Function)
		{
			// "StaticMeshActor.GetStaticMeshComponent" is no function but the getter of a Blueprint-visible property:
			// create a variable-get node on the other object (input "self", output named like the property).
			const FString Wanted = Normalize(FunctionName.StartsWith(TEXT("Get")) ? FunctionName.Mid(3) : FunctionName);
			for (TFieldIterator<FProperty> It(FunctionClass); It && !Wanted.IsEmpty(); ++It)
			{
				const FProperty* Property = *It;
				if (Property->HasAnyPropertyFlags(CPF_BlueprintVisible) && Normalize(Property->GetName()) == Wanted)
				{
					FGraphNodeCreator<UK2Node_VariableGet> PropCreator(*Graph);
					UK2Node_VariableGet* PropNode = PropCreator.CreateNode();
					PropNode->VariableReference.SetExternalMember(Property->GetFName(), FunctionClass);
					PropCreator.Finalize();
					return PropNode;
				}
			}
			Errors.Add(FString::Printf(TEXT("Node \"%s\": %s is not a BlueprintCallable function of %s.%s"),
				*Spec.Id, *FunctionName, *FunctionClass->GetName(),
				Suggestions.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" Similar: %s."), *Suggestions)));
			return nullptr;
		}

		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode();
		Node->SetFromFunction(Function);
		Creator.Finalize();
		return Node;
	}

	UEdGraphNode* SpawnBranchNode(UEdGraph* Graph)
	{
		FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
		UK2Node_IfThenElse* Node = Creator.CreateNode();
		Creator.Finalize();
		return Node;
	}
	// ------------------------------------------------------------------ graphs, existing nodes, functions

	UEdGraph* FindGraphByName(const UBlueprint* Blueprint, const FString& Name, FString& OutAvailable)
	{
		TArray<FString> Names;
		UEdGraph* Found = nullptr;
		auto Scan = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs)
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}
				Names.Add(Graph->GetName());
				if (!Found && Graph->GetName().Equals(Name, ESearchCase::IgnoreCase))
				{
					Found = Graph;
				}
			}
		};
		Scan(Blueprint->UbergraphPages);
		Scan(Blueprint->FunctionGraphs);
		Scan(Blueprint->MacroGraphs);
		OutAvailable = FString::Join(Names, TEXT(", "));
		return Found;
	}

	UEdGraphNode* FindNodeByRef(const UEdGraph* Graph, const FString& Id)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && ABGNodeId::Make(Node) == Id)
			{
				return Node;
			}
		}
		return nullptr;
	}

	/** Creates the function graph (entry/result nodes and parameters) unless a function with that name exists. */
	bool EnsureFunction(UBlueprint* Blueprint, const FABGFunctionSpec& Function, FABGBuildResult& Result)
	{
		for (const UEdGraph* Existing : Blueprint->FunctionGraphs)
		{
			if (Existing && Existing->GetName().Equals(Function.Name, ESearchCase::IgnoreCase))
			{
				return true; // reuse
			}
		}

		// Validate the parameter types before touching the Blueprint.
		TArray<FEdGraphPinType> InputTypes, OutputTypes;
		for (const FABGParamSpec& Param : Function.Inputs)
		{
			FEdGraphPinType Type;
			FString Error;
			if (!MakeVariableType(Param.Type, Type, Error))
			{
				Result.Errors.Add(FString::Printf(TEXT("Function \"%s\", input \"%s\": %s"), *Function.Name, *Param.Name, *Error));
				return false;
			}
			InputTypes.Add(Type);
		}
		for (const FABGParamSpec& Param : Function.Outputs)
		{
			FEdGraphPinType Type;
			FString Error;
			if (!MakeVariableType(Param.Type, Type, Error))
			{
				Result.Errors.Add(FString::Printf(TEXT("Function \"%s\", output \"%s\": %s"), *Function.Name, *Param.Name, *Error));
				return false;
			}
			OutputTypes.Add(Type);
		}

		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Function.Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (!NewGraph)
		{
			Result.Errors.Add(FString::Printf(TEXT("Function \"%s\" could not be created."), *Function.Name));
			return false;
		}
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated*/ true, nullptr);

		UK2Node_EditablePinBase* Entry = FBlueprintEditorUtils::GetEntryNode(NewGraph);
		if (!Entry)
		{
			Result.Errors.Add(FString::Printf(TEXT("Function \"%s\": the new graph has no entry node."), *Function.Name));
			return false;
		}
		for (int32 i = 0; i < Function.Inputs.Num(); ++i)
		{
			Entry->CreateUserDefinedPin(FName(*Function.Inputs[i].Name), InputTypes[i], EGPD_Output);
		}
		if (Function.Outputs.Num() > 0)
		{
			if (UK2Node_FunctionResult* ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry))
			{
				for (int32 i = 0; i < Function.Outputs.Num(); ++i)
				{
					ResultNode->CreateUserDefinedPin(FName(*Function.Outputs[i].Name), OutputTypes[i], EGPD_Input);
				}
			}
		}
		++Result.FunctionsCreated;
		return true;
	}

	/** Builds one graph part: removals, nodes, pin defaults, connections. Errors go to Result.Errors. */
	void BuildPart(UBlueprint* Blueprint, UEdGraph* Graph, const FString& GraphLabel, const FABGGraphPart& Part,
		const UEdGraphSchema_K2* Schema, FABGBuildResult& Result, TArray<TPair<FString, UEdGraphNode*>>& AllNodes)
	{
		Graph->Modify();
		const FString Label = GraphLabel.IsEmpty() ? FString() : FString::Printf(TEXT("[%s] "), *GraphLabel);
		const bool bIsEventGraph = Schema->GetGraphType(Graph) == GT_Ubergraph;

		TMap<FString, UEdGraphNode*> NodesById;
		auto ResolveNode = [&](const FString& Id) -> UEdGraphNode*
		{
			if (UEdGraphNode** Found = NodesById.Find(Id))
			{
				return *Found;
			}
			if (ABGNodeId::LooksLikeExistingId(Id))
			{
				if (UEdGraphNode* Existing = FindNodeByRef(Graph, Id))
				{
					NodesById.Add(Id, Existing);
					return Existing;
				}
			}
			return nullptr;
		};

		// ---- removals first ----
		for (const FString& Id : Part.Remove)
		{
			UEdGraphNode* Node = FindNodeByRef(Graph, Id);
			if (!Node)
			{
				Result.Errors.Add(FString::Printf(TEXT("%sCannot remove \"%s\": no node with this id in the graph."), *Label, *Id));
				continue;
			}
			if (Node->IsA<UK2Node_FunctionEntry>())
			{
				Result.Errors.Add(FString::Printf(TEXT("%sCannot remove \"%s\": the function's entry node must stay."), *Label, *Id));
				continue;
			}
			FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile*/ true);
			++Result.NodesRemoved;
		}

		// New nodes go into a block BELOW everything that is already in the graph, so they never land on top of old nodes.
		FIntPoint Origin(0, 0);
		{
			bool bAny = false;
			int32 MinX = 0, MaxY = 0;
			for (const UEdGraphNode* Existing : Graph->Nodes)
			{
				if (!Existing)
				{
					continue;
				}
				MinX = bAny ? FMath::Min(MinX, Existing->NodePosX) : Existing->NodePosX;
				MaxY = bAny ? FMath::Max(MaxY, Existing->NodePosY) : Existing->NodePosY;
				bAny = true;
			}
			if (bAny)
			{
				Origin = FIntPoint(MinX, MaxY + NewBlockGapY);
			}
		}
		TArray<UEdGraphNode*> NewNodes;

		// ---- spawn nodes ----
		for (const FABGNodeSpec& NodeSpec : Part.Nodes)
		{
			UEdGraphNode* Node = nullptr;
			const int32 NodesBefore = Graph->Nodes.Num();

			if (NodeSpec.Type == ABGNodeType::Existing)
			{
				Node = FindNodeByRef(Graph, NodeSpec.Id);
				if (!Node)
				{
					Result.Errors.Add(FString::Printf(TEXT("%sExisting node \"%s\" was not found in this graph."), *Label, *NodeSpec.Id));
				}
			}
			else if (NodeSpec.Type == ABGNodeType::Event)
			{
				if (!bIsEventGraph)
				{
					Result.Errors.Add(FString::Printf(TEXT("%sNode \"%s\": events can only be placed in an event graph, not in a function or macro."), *Label, *NodeSpec.Id));
				}
				else
				{
					Node = SpawnEventNode(Blueprint, Graph, NodeSpec, Result.Errors);
				}
			}
			else if (NodeSpec.Type == ABGNodeType::CallFunction)
			{
				Node = SpawnCallFunctionNode(Blueprint, Graph, NodeSpec, Result.Errors);
			}
			else if (NodeSpec.Type == ABGNodeType::Branch)
			{
				Node = SpawnBranchNode(Graph);
			}
			else if (NodeSpec.Type == ABGNodeType::SpawnActor)
			{
				Node = SpawnSpawnActorNode(Graph, NodeSpec, Result.Errors);
			}
			else if (NodeSpec.Type == ABGNodeType::GetVariable || NodeSpec.Type == ABGNodeType::SetVariable)
			{
				Node = SpawnVariableNode(Blueprint, Graph, NodeSpec, NodeSpec.Type == ABGNodeType::SetVariable, Result.Errors);
			}
			else if (NodeSpec.Type == ABGNodeType::Cast)
			{
				Node = SpawnCastNode(Graph, NodeSpec, Result.Errors);
			}
			else if (NodeSpec.Type == ABGNodeType::Self)
			{
				Node = SpawnSelfNode(Graph);
			}
			else if (NodeSpec.Type == ABGNodeType::FunctionEntry || NodeSpec.Type == ABGNodeType::FunctionResult)
			{
				UK2Node_EditablePinBase* Entry = FBlueprintEditorUtils::GetEntryNode(Graph);
				if (!Entry)
				{
					Result.Errors.Add(FString::Printf(TEXT("%sNode \"%s\": this graph has no function entry (it is an event graph?)."), *Label, *NodeSpec.Id));
				}
				else if (NodeSpec.Type == ABGNodeType::FunctionEntry)
				{
					Node = Entry;
				}
				else
				{
					Node = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
				}
			}

			if (!Node)
			{
				continue;
			}

			const bool bReused = (Graph->Nodes.Num() == NodesBefore);
			if (!bReused)
			{
				Node->NodePosX = Origin.X;
				Node->NodePosY = Origin.Y;
				NewNodes.Add(Node);
				++Result.NodesCreated;
			}
			NodesById.Add(NodeSpec.Id, Node);
			AllNodes.Add(TPair<FString, UEdGraphNode*>(Label + NodeSpec.Id, Node));

			// ---- pin defaults ----
			for (const TPair<FString, FString>& Default : NodeSpec.PinDefaults)
			{
				UEdGraphPin* Pin = FindPinByName(Node, Default.Key, EGPD_Input);
				if (!Pin)
				{
					Result.Warnings.Add(FString::Printf(TEXT("%sNode \"%s\": no input pin \"%s\" (available: %s)."),
						*Label, *NodeSpec.Id, *Default.Key, *ListPinNames(Node, EGPD_Input)));
					continue;
				}
				FString DefaultError;
				if (!ApplyPinDefault(Schema, Pin, Default.Value, DefaultError))
				{
					Result.Errors.Add(FString::Printf(TEXT("%sNode \"%s\", pin \"%s\": %s"), *Label, *NodeSpec.Id, *Default.Key, *DefaultError));
				}
			}
		}

		// ---- connections (only when everything above worked) ----
		if (Result.Errors.Num() > 0)
		{
			return;
		}
		for (const FABGConnectionSpec& Conn : Part.Connections)
		{
			UEdGraphNode* FromNode = ResolveNode(Conn.From.NodeId);
			UEdGraphNode* ToNode = ResolveNode(Conn.To.NodeId);
			if (!FromNode || !ToNode)
			{
				Result.Errors.Add(FString::Printf(TEXT("%sConnection %s.%s -> %s.%s: node %s was not found."), *Label,
					*Conn.From.NodeId, *Conn.From.PinName, *Conn.To.NodeId, *Conn.To.PinName,
					!FromNode ? *Conn.From.NodeId : *Conn.To.NodeId));
				continue;
			}

			UEdGraphPin* FromPin = FindPinWithAliases(FromNode, Conn.From.PinName, EGPD_Output);
			UEdGraphPin* ToPin = FindPinWithAliases(ToNode, Conn.To.PinName, EGPD_Input);
			if (!FromPin)
			{
				Result.Errors.Add(FString::Printf(TEXT("%sNode \"%s\" has no output pin \"%s\" (available: %s)."), *Label,
					*Conn.From.NodeId, *Conn.From.PinName, *ListPinNames(FromNode, EGPD_Output)));
				continue;
			}
			if (!ToPin)
			{
				Result.Errors.Add(FString::Printf(TEXT("%sNode \"%s\" has no input pin \"%s\" (available: %s)."), *Label,
					*Conn.To.NodeId, *Conn.To.PinName, *ListPinNames(ToNode, EGPD_Input)));
				continue;
			}

			const FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);
			if (Response.Response != CONNECT_RESPONSE_DISALLOW && Schema->TryCreateConnection(FromPin, ToPin))
			{
				++Result.ConnectionsMade;
			}
			else
			{
				Result.Errors.Add(FString::Printf(TEXT("%sCannot connect %s.%s (%s) -> %s.%s (%s): %s. If the types differ, put a Cast node in between or pick a function that returns the right type."), *Label,
					*Conn.From.NodeId, *Conn.From.PinName, *DescribePinType(FromPin),
					*Conn.To.NodeId, *Conn.To.PinName, *DescribePinType(ToPin),
					Response.Message.IsEmpty() ? TEXT("not allowed") : *Response.Message.ToString()));
			}
		}

		// ---- layout of the new nodes: columns by distance along the wires, rows inside a column ----
		TMap<const UEdGraphNode*, int32> Layer;
		for (const UEdGraphNode* NewNode : NewNodes)
		{
			Layer.Add(NewNode, 0);
		}
		for (int32 Pass = 0; Pass < NewNodes.Num(); ++Pass)
		{
			bool bChanged = false;
			for (const FABGConnectionSpec& Conn : Part.Connections)
			{
				const UEdGraphNode* From = ResolveNode(Conn.From.NodeId);
				const UEdGraphNode* To = ResolveNode(Conn.To.NodeId);
				if (!From || !To || !Layer.Contains(From) || !Layer.Contains(To))
				{
					continue;
				}
				const int32 Wanted = Layer[From] + 1;
				if (Layer[To] < Wanted)
				{
					Layer[To] = Wanted;
					bChanged = true;
				}
			}
			if (!bChanged)
			{
				break;
			}
		}
		TMap<int32, int32> RowInLayer;
		for (UEdGraphNode* NewNode : NewNodes)
		{
			const int32 NodeLayer = Layer[NewNode];
			const int32 Row = RowInLayer.FindOrAdd(NodeLayer)++;
			NewNode->NodePosX = Origin.X + NodeLayer * TempNodeSpacingX;
			NewNode->NodePosY = Origin.Y + Row * TempNodeSpacingY;
		}
	}
}

FString FABGBuildResult::ToDisplayString() const
{
	TArray<FString> Lines;
	if (bSuccess)
	{
		FString Extra;
		if (VariablesCreated > 0)
		{
			Extra += FString::Printf(TEXT(", %d variable(s) added"), VariablesCreated);
		}
		if (FunctionsCreated > 0)
		{
			Extra += FString::Printf(TEXT(", %d function(s) added"), FunctionsCreated);
		}
		if (NodesRemoved > 0)
		{
			Extra += FString::Printf(TEXT(", %d node(s) removed"), NodesRemoved);
		}
		Lines.Add(FString::Printf(TEXT("OK: %d node(s) created, %d connection(s) made%s. Blueprint compile: %s."),
			NodesCreated, ConnectionsMade, *Extra,
			bCompiled ? TEXT("succeeded") : TEXT("FAILED (see Compiler Results)")));
	}
	else
	{
		Lines.Add(TEXT("FAILED: nothing was changed."));
	}
	for (const FString& E : Errors)
	{
		Lines.Add(TEXT("Error: ") + E);
	}
	for (const FString& N : Notes)
	{
		Lines.Add(TEXT("Not done (model's note): ") + N);
	}
	for (const FString& C : CompileErrors)
	{
		Lines.Add(TEXT("Compile error: ") + C);
	}
	for (const FString& W : Warnings)
	{
		Lines.Add(TEXT("Warning: ") + W);
	}
	return FString::Join(Lines, TEXT("\n"));
}

bool FABGGraphBuilder::FindTargetGraph(UBlueprint*& OutBlueprint, UEdGraph*& OutGraph, FString& OutError)
{
	OutBlueprint = nullptr;
	OutGraph = nullptr;

	UAssetEditorSubsystem* AssetEditors = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!AssetEditors)
	{
		OutError = TEXT("Asset editor subsystem is not available.");
		return false;
	}

	TArray<UBlueprint*> OpenBlueprints;
	for (UObject* Asset : AssetEditors->GetAllEditedAssets())
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			OpenBlueprints.Add(Blueprint);
		}
	}

	if (OpenBlueprints.Num() == 0)
	{
		OutError = TEXT("No Blueprint editor is open. Open a Blueprint and click into its graph first.");
		return false;
	}
	if (OpenBlueprints.Num() > 1)
	{
		OutError = TEXT("More than one Blueprint editor is open. Close all but the target one (temporary limitation).");
		return false;
	}

	UBlueprint* Blueprint = OpenBlueprints[0];
	UEdGraph* Graph = nullptr;
	if (!FindTargetGraphFor(Blueprint, Graph, OutError))
	{
		return false;
	}

	OutBlueprint = Blueprint;
	OutGraph = Graph;
	return true;
}

bool FABGGraphBuilder::FindTargetGraphFor(UBlueprint* Blueprint, UEdGraph*& OutGraph, FString& OutError)
{
	OutGraph = nullptr;
	if (!Blueprint)
	{
		OutError = TEXT("The Blueprint of this window no longer exists.");
		return false;
	}

	TSharedPtr<IBlueprintEditor> Editor = FKismetEditorUtilities::GetIBlueprintEditorForObject(Blueprint, /*bOpenEditor*/ false);
	UEdGraph* Graph = Editor.IsValid() ? Editor->GetFocusedGraph() : nullptr;
	if (!Graph && Blueprint->UbergraphPages.Num() > 0)
	{
		Graph = Blueprint->UbergraphPages[0];
	}
	if (!Graph)
	{
		OutError = TEXT("Could not find a graph in this Blueprint.");
		return false;
	}
	OutGraph = Graph;
	return true;
}

FABGBuildResult FABGGraphBuilder::Build(UBlueprint* Blueprint, UEdGraph* FocusGraph, const FABGGraphSpec& Spec)
{
	FABGBuildResult Result;
	if (!Blueprint)
	{
		Result.Errors.Add(TEXT("No target Blueprint."));
		return Result;
	}

	Result.Notes = Spec.Unsupported;
	if (!Spec.HasWork())
	{
		if (Spec.Unsupported.Num() > 0)
		{
			Result.Errors.Add(TEXT("The AI says this cannot be done with the available node types. Its explanation is listed below."));
		}
		else
		{
			Result.Errors.Add(TEXT("The reply contains nothing to build (no nodes, variables or functions)."));
		}
		return Result;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	FScopedTransaction Transaction(LOCTEXT("GenerateNodesTransaction", "AI Blueprint: Apply graph"));
	Blueprint->Modify();

	// ---- variables first, so Get/Set nodes can refer to them ----
	for (const FABGVariableSpec& Var : Spec.Variables)
	{
		const FName VarName(*Var.Name);
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarName) != INDEX_NONE)
		{
			continue; // already there: reuse it
		}
		FEdGraphPinType VarType;
		FString VarError;
		if (!MakeVariableType(Var.Type, VarType, VarError))
		{
			Result.Errors.Add(FString::Printf(TEXT("Variable \"%s\": %s"), *Var.Name, *VarError));
			continue;
		}
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, VarType, Var.Default))
		{
			Result.Errors.Add(FString::Printf(TEXT("Variable \"%s\" could not be created (the name may already be used by a component or function)."), *Var.Name));
			continue;
		}
		++Result.VariablesCreated;
	}

	// ---- new functions (their bodies are graph parts addressed by the function's name) ----
	for (const FABGFunctionSpec& Function : Spec.Functions)
	{
		EnsureFunction(Blueprint, Function, Result);
	}
	if (Result.FunctionsCreated > 0 && Result.Errors.Num() == 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint); // refresh the skeleton so "self.Function" calls see the parameters
	}

	// ---- graph parts ----
	TArray<TPair<FString, UEdGraphNode*>> AllNodes;
	TArray<UEdGraph*> TouchedGraphs;
	if (Result.Errors.Num() == 0)
	{
		for (const FABGGraphPart& Part : Spec.Parts)
		{
			if (Part.IsEmpty())
			{
				continue;
			}

			UEdGraph* Graph = FocusGraph;
			if (!Part.GraphName.IsEmpty())
			{
				FString Available;
				Graph = FindGraphByName(Blueprint, Part.GraphName, Available);
				if (!Graph)
				{
					Result.Errors.Add(FString::Printf(TEXT("Graph \"%s\" not found in this Blueprint (graphs: %s)."), *Part.GraphName, *Available));
					continue;
				}
			}
			else if (!Graph)
			{
				Result.Errors.Add(TEXT("No target graph: open a Blueprint and click into its graph first."));
				continue;
			}

			TouchedGraphs.AddUnique(Graph);
			BuildPart(Blueprint, Graph, Part.GraphName, Part, Schema, Result, AllNodes);
		}
	}

	// ---- finish: roll back on any error, otherwise compile ----
	if (Result.Errors.Num() > 0)
	{
		Transaction.Cancel();
		for (UEdGraph* Graph : TouchedGraphs)
		{
			Graph->NotifyGraphChanged();
		}
		Result.NodesCreated = 0;
		Result.ConnectionsMade = 0;
		Result.VariablesCreated = 0;
		Result.FunctionsCreated = 0;
		Result.NodesRemoved = 0;
		Result.bSuccess = false;
		return Result;
	}

	for (UEdGraph* Graph : TouchedGraphs)
	{
		Graph->NotifyGraphChanged();
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	Result.bCompiled = (Blueprint->Status != BS_Error);
	if (!Result.bCompiled)
	{
		// Pull the compiler's per-node messages so they can be handed back to the AI.
		for (const TPair<FString, UEdGraphNode*>& Pair : AllNodes)
		{
			const UEdGraphNode* Node = Pair.Value;
			if (Node && Node->bHasCompilerMessage && Node->ErrorType <= (int32)EMessageSeverity::Error && !Node->ErrorMsg.IsEmpty())
			{
				const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Replace(TEXT("\n"), TEXT(" "));
				Result.CompileErrors.Add(FString::Printf(TEXT("Node \"%s\" (%s): %s"), *Pair.Key, *Title, *Node->ErrorMsg));
			}
		}
		if (Result.CompileErrors.Num() == 0)
		{
			Result.CompileErrors.Add(TEXT("The Blueprint does not compile (no per-node message available)."));
		}
	}
	Result.bSuccess = true;
	UE_LOG(LogAIBlueprintGen, Log, TEXT("Graph applied: %d node(s) created, %d connection(s), %d removed, compiled=%d"),
		Result.NodesCreated, Result.ConnectionsMade, Result.NodesRemoved, Result.bCompiled ? 1 : 0);
	return Result;
}

#undef LOCTEXT_NAMESPACE
