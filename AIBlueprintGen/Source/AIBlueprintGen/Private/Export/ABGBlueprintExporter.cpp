#include "Export/ABGBlueprintExporter.h"
#include "Graph/ABGNodeId.h"
#include "Prompt/ABGBlueprintKind.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
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
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString DescribeType(const FEdGraphPinType& Type)
	{
		FString Base;
		const FName Category = Type.PinCategory;
		if (Category == UEdGraphSchema_K2::PC_Boolean) { Base = TEXT("bool"); }
		else if (Category == UEdGraphSchema_K2::PC_Int) { Base = TEXT("int"); }
		else if (Category == UEdGraphSchema_K2::PC_Int64) { Base = TEXT("int64"); }
		else if (Category == UEdGraphSchema_K2::PC_Real || Category == UEdGraphSchema_K2::PC_Float || Category == UEdGraphSchema_K2::PC_Double) { Base = TEXT("float"); }
		else if (Category == UEdGraphSchema_K2::PC_String) { Base = TEXT("string"); }
		else if (Category == UEdGraphSchema_K2::PC_Name) { Base = TEXT("name"); }
		else if (Category == UEdGraphSchema_K2::PC_Text) { Base = TEXT("text"); }
		else if (Category == UEdGraphSchema_K2::PC_Byte) { Base = Type.PinSubCategoryObject.IsValid() ? Type.PinSubCategoryObject->GetName() : FString(TEXT("byte")); }
		else if (Category == UEdGraphSchema_K2::PC_Struct || Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_SoftObject || Category == UEdGraphSchema_K2::PC_SoftClass || Category == UEdGraphSchema_K2::PC_Interface
			|| Category == UEdGraphSchema_K2::PC_Enum)
		{
			Base = Type.PinSubCategoryObject.IsValid() ? Type.PinSubCategoryObject->GetName() : Category.ToString();
			if (Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass)
			{
				Base = FString::Printf(TEXT("class of %s"), *Base);
			}
		}
		else { Base = Category.ToString(); }

		switch (Type.ContainerType)
		{
		case EPinContainerType::Array: return FString::Printf(TEXT("array of %s"), *Base);
		case EPinContainerType::Set: return FString::Printf(TEXT("set of %s"), *Base);
		case EPinContainerType::Map: return FString::Printf(TEXT("map of %s"), *Base);
		default: return Base;
		}
	}

	FString ToCondensedJson(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Object, Writer);
		return Out;
	}

	bool IsVisiblePin(const UEdGraphPin* Pin)
	{
		return Pin && !Pin->bHidden && !Pin->bOrphanedPin;
	}

	FString PinDefaultText(const UEdGraphPin* Pin)
	{
		if (Pin->DefaultObject)
		{
			return Pin->DefaultObject->GetPathName();
		}
		if (!Pin->DefaultValue.IsEmpty())
		{
			return Pin->DefaultValue;
		}
		if (!Pin->DefaultTextValue.IsEmpty())
		{
			return Pin->DefaultTextValue.ToString();
		}
		return FString();
	}

	FString GraphKind(const UBlueprint* Blueprint, const UEdGraph* Graph)
	{
		if (Blueprint->UbergraphPages.Contains(Graph)) { return TEXT("event graph"); }
		if (Blueprint->FunctionGraphs.Contains(Graph)) { return TEXT("function"); }
		if (Blueprint->MacroGraphs.Contains(Graph)) { return TEXT("macro"); }
		return TEXT("graph");
	}

	FString ExportGraph(const UBlueprint* Blueprint, const UEdGraph* Graph, bool bFocused)
	{
		TSet<const UEdGraphNode*> Exported;
		TArray<FString> NodeLines;

		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Node->IsA<UEdGraphNode_Comment>())
			{
				continue;
			}

			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), ABGNodeId::Make(Node));

			if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
			{
				if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Node))
				{
					Json->SetStringField(TEXT("type"), TEXT("CustomEvent"));
					Json->SetStringField(TEXT("name"), Custom->CustomFunctionName.ToString());
				}
				else
				{
					Json->SetStringField(TEXT("type"), TEXT("Event"));
					Json->SetStringField(TEXT("event"), Event->EventReference.GetMemberName().ToString());
				}
			}
			else if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				Json->SetStringField(TEXT("type"), TEXT("CallFunction"));
				FString ClassName = TEXT("self");
				if (!Call->FunctionReference.IsSelfContext())
				{
					if (const UFunction* Function = Call->GetTargetFunction())
					{
						ClassName = Function->GetOwnerClass() ? Function->GetOwnerClass()->GetName() : FString(TEXT("?"));
					}
					else if (const UClass* Member = Call->FunctionReference.GetMemberParentClass())
					{
						ClassName = Member->GetName();
					}
				}
				Json->SetStringField(TEXT("function"), ClassName + TEXT(".") + Call->FunctionReference.GetMemberName().ToString());
			}
			else if (Node->IsA<UK2Node_IfThenElse>())
			{
				Json->SetStringField(TEXT("type"), TEXT("Branch"));
			}
			else if (Node->IsA<UK2Node_SpawnActorFromClass>())
			{
				Json->SetStringField(TEXT("type"), TEXT("SpawnActor"));
			}
			else if (const UK2Node_Variable* Var = Cast<UK2Node_Variable>(Node))
			{
				Json->SetStringField(TEXT("type"), Node->IsA<UK2Node_VariableSet>() ? TEXT("SetVariable") : TEXT("GetVariable"));
				Json->SetStringField(TEXT("variable"), Var->GetVarName().ToString());
			}
			else if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
			{
				Json->SetStringField(TEXT("type"), TEXT("Cast"));
				Json->SetStringField(TEXT("class"), CastNode->TargetType ? CastNode->TargetType->GetName() : FString());
			}
			else if (Node->IsA<UK2Node_Self>())
			{
				Json->SetStringField(TEXT("type"), TEXT("Self"));
			}
			else if (Node->IsA<UK2Node_FunctionEntry>())
			{
				Json->SetStringField(TEXT("type"), TEXT("FunctionEntry"));
			}
			else if (Node->IsA<UK2Node_FunctionResult>())
			{
				Json->SetStringField(TEXT("type"), TEXT("FunctionResult"));
			}
			else
			{
				Json->SetStringField(TEXT("type"), TEXT("Other"));
				Json->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Replace(TEXT("\n"), TEXT(" ")));
			}

			TArray<TSharedPtr<FJsonValue>> InNames, OutNames;
			TSharedRef<FJsonObject> Defaults = MakeShared<FJsonObject>();
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!IsVisiblePin(Pin))
				{
					continue;
				}
				const FString PinName = Pin->PinName.ToString();
				if (Pin->Direction == EGPD_Input)
				{
					InNames.Add(MakeShared<FJsonValueString>(PinName));
					if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && Pin->LinkedTo.Num() == 0)
					{
						const FString Default = PinDefaultText(Pin);
						if (!Default.IsEmpty())
						{
							Defaults->SetStringField(PinName, Default);
						}
					}
				}
				else
				{
					OutNames.Add(MakeShared<FJsonValueString>(PinName));
				}
			}
			Json->SetArrayField(TEXT("in"), InNames);
			Json->SetArrayField(TEXT("out"), OutNames);
			if (Defaults->Values.Num() > 0)
			{
				Json->SetObjectField(TEXT("pins"), Defaults);
			}

			Exported.Add(Node);
			NodeLines.Add(TEXT("    ") + ToCondensedJson(Json));
		}

		TArray<FString> ConnectionLines;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !Exported.Contains(Node))
			{
				continue;
			}
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!IsVisiblePin(Pin) || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					const UEdGraphNode* Other = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
					if (!Other || !Exported.Contains(Other))
					{
						continue;
					}
					ConnectionLines.Add(FString::Printf(TEXT("    {\"from\":\"%s.%s\",\"to\":\"%s.%s\"}"),
						*ABGNodeId::Make(Node), *Pin->PinName.ToString(), *ABGNodeId::Make(Other), *Linked->PinName.ToString()));
				}
			}
		}

		FString Out = FString::Printf(TEXT("--- Graph \"%s\" (%s)%s ---\n"), *Graph->GetName(), *GraphKind(Blueprint, Graph),
			bFocused ? TEXT(" - THE GRAPH THE USER HAS OPEN; new nodes without a \"graph\" name go here") : TEXT(""));
		Out += TEXT("{\n  \"graph\": \"") + Graph->GetName() + TEXT("\",\n  \"nodes\": [\n");
		Out += FString::Join(NodeLines, TEXT(",\n"));
		Out += TEXT("\n  ],\n  \"connections\": [\n");
		Out += FString::Join(ConnectionLines, TEXT(",\n"));
		Out += TEXT("\n  ]\n}\n");
		return Out;
	}

	FString DescribeFunction(const UEdGraph* Graph)
	{
		FString Inputs, Outputs;
		if (const UK2Node_EditablePinBase* Entry = FBlueprintEditorUtils::GetEntryNode(Graph))
		{
			for (const TSharedPtr<FUserPinInfo>& Info : Entry->UserDefinedPins)
			{
				if (Info.IsValid())
				{
					Inputs += (Inputs.IsEmpty() ? TEXT("") : TEXT(", ")) + FString::Printf(TEXT("%s: %s"), *Info->PinName.ToString(), *DescribeType(Info->PinType));
				}
			}
		}
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (const UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
			{
				for (const TSharedPtr<FUserPinInfo>& Info : ResultNode->UserDefinedPins)
				{
					if (Info.IsValid())
					{
						Outputs += (Outputs.IsEmpty() ? TEXT("") : TEXT(", ")) + FString::Printf(TEXT("%s: %s"), *Info->PinName.ToString(), *DescribeType(Info->PinType));
					}
				}
				break;
			}
		}
		return FString::Printf(TEXT("- %s(%s)%s"), *Graph->GetName(), *Inputs, Outputs.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" -> (%s)"), *Outputs));
	}
}

FString FABGBlueprintExporter::Export(UBlueprint* Blueprint, UEdGraph* FocusGraph, int32 MaxChars)
{
	if (!Blueprint)
	{
		return FString();
	}

	TArray<FString> Sections;

	{
		FString Head = FString::Printf(TEXT("Blueprint: %s\nKind: %s\nAsset path: %s\nParent class: %s\n"),
			*Blueprint->GetName(), *ABGBlueprintKind::Label(ABGBlueprintKind::Detect(Blueprint)), *Blueprint->GetPathName(),
			Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("?"));
		if (FocusGraph)
		{
			Head += FString::Printf(TEXT("Open graph: %s\n"), *FocusGraph->GetName());
		}
		Sections.Add(Head);
	}

	{
		FString Text = TEXT("Variables:\n");
		if (Blueprint->NewVariables.Num() == 0)
		{
			Text += TEXT("- (none)\n");
		}
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			Text += FString::Printf(TEXT("- %s: %s%s\n"), *Var.VarName.ToString(), *DescribeType(Var.VarType),
				Var.DefaultValue.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" = %s"), *Var.DefaultValue));
		}
		Sections.Add(Text);
	}

	{
		FString Text = TEXT("Components (usable as variables with GetVariable):\n");
		int32 Count = 0;
		if (Blueprint->SimpleConstructionScript)
		{
			for (const USCS_Node* ScsNode : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (!ScsNode)
				{
					continue;
				}
				++Count;
				Text += FString::Printf(TEXT("- %s: %s%s\n"), *ScsNode->GetVariableName().ToString(),
					ScsNode->ComponentClass ? *ScsNode->ComponentClass->GetName() : TEXT("?"),
					ScsNode->IsRootNode() ? TEXT(" (root)")
						: (ScsNode->ParentComponentOrVariableName.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (attached to %s)"), *ScsNode->ParentComponentOrVariableName.ToString())));
			}
		}
		if (Count == 0)
		{
			Text += TEXT("- (none)\n");
		}
		Sections.Add(Text);
	}

	{
		FString Text = TEXT("Functions:\n");
		if (Blueprint->FunctionGraphs.Num() == 0)
		{
			Text += TEXT("- (none)\n");
		}
		for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				Text += DescribeFunction(Graph) + TEXT("\n");
			}
		}
		Text += TEXT("Macros:\n");
		int32 MacroCount = 0;
		for (const UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph)
			{
				++MacroCount;
				Text += FString::Printf(TEXT("- %s\n"), *Graph->GetName());
			}
		}
		if (MacroCount == 0)
		{
			Text += TEXT("- (none)\n");
		}
		Sections.Add(Text);
	}

	// Graphs: the focused one first.
	TArray<const UEdGraph*> Graphs;
	if (FocusGraph)
	{
		Graphs.Add(FocusGraph);
	}
	auto AddAll = [&](const TArray<TObjectPtr<UEdGraph>>& Source)
	{
		for (const UEdGraph* Graph : Source)
		{
			if (Graph && !Graphs.Contains(Graph))
			{
				Graphs.Add(Graph);
			}
		}
	};
	AddAll(Blueprint->UbergraphPages);
	AddAll(Blueprint->FunctionGraphs);
	AddAll(Blueprint->MacroGraphs);

	for (const UEdGraph* Graph : Graphs)
	{
		Sections.Add(ExportGraph(Blueprint, Graph, Graph == FocusGraph));
	}

	FString Out;
	for (int32 i = 0; i < Sections.Num(); ++i)
	{
		if (MaxChars > 0 && Out.Len() + Sections[i].Len() > MaxChars && i > 3)
		{
			Out += FString::Printf(TEXT("\n[Truncated: %d more graph(s) omitted because of the context size limit. Raise it in Editor Preferences > Plugins > AI Blueprint Generator.]\n"), Sections.Num() - i);
			break;
		}
		Out += Sections[i] + TEXT("\n");
	}
	return Out;
}
