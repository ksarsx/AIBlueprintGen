#include "Schema/ABGGraphJson.h"
#include "Graph/ABGNodeId.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ParsePinRef(const FString& In, FABGPinRef& Out)
	{
		int32 DotIdx = INDEX_NONE;
		if (!In.FindChar(TEXT('.'), DotIdx) || DotIdx <= 0 || DotIdx >= In.Len() - 1)
		{
			return false;
		}
		Out.NodeId = In.Left(DotIdx);
		Out.PinName = In.Mid(DotIdx + 1);
		return true;
	}

	bool CanonicalNodeType(const FString& In, FString& Out)
	{
		const TCHAR* Known[] =
		{
			ABGNodeType::Event, ABGNodeType::CallFunction, ABGNodeType::Branch, ABGNodeType::SpawnActor,
			ABGNodeType::GetVariable, ABGNodeType::SetVariable, ABGNodeType::Cast, ABGNodeType::Self,
			ABGNodeType::Existing, ABGNodeType::FunctionEntry, ABGNodeType::FunctionResult
		};
		for (const TCHAR* K : Known)
		{
			if (In.Equals(K, ESearchCase::IgnoreCase))
			{
				Out = K;
				return true;
			}
		}
		return false;
	}

	bool JsonValueToString(const TSharedPtr<FJsonValue>& Value, FString& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		bool bBool = false;
		double Number = 0.0;
		if (Value->TryGetString(Out))
		{
			return true;
		}
		if (Value->TryGetBool(bBool))
		{
			Out = bBool ? TEXT("true") : TEXT("false");
			return true;
		}
		if (Value->TryGetNumber(Number))
		{
			Out = FString::SanitizeFloat(Number);
			return true;
		}
		return false;
	}

	void ReadStringList(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<FString>& Out)
	{
		FString Single;
		const TArray<TSharedPtr<FJsonValue>>* Many = nullptr;
		if (Obj->TryGetStringField(Field, Single))
		{
			if (!Single.TrimStartAndEnd().IsEmpty())
			{
				Out.Add(Single.TrimStartAndEnd());
			}
		}
		else if (Obj->TryGetArrayField(Field, Many) && Many)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Many)
			{
				FString Text;
				if (Item.IsValid() && Item->TryGetString(Text) && !Text.TrimStartAndEnd().IsEmpty())
				{
					Out.Add(Text.TrimStartAndEnd());
				}
			}
		}
	}

	/** [ { "name": "...", "type": "..." } ] */
	void ReadParams(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FString& Owner, TArray<FABGParamSpec>& Out, TArray<FString>& Errors)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Obj->TryGetArrayField(Field, Array) || !Array)
		{
			return;
		}
		for (int32 i = 0; i < Array->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* ParamObj = nullptr;
			FABGParamSpec Param;
			if ((*Array)[i].IsValid() && (*Array)[i]->TryGetObject(ParamObj) && ParamObj)
			{
				(*ParamObj)->TryGetStringField(TEXT("name"), Param.Name);
				(*ParamObj)->TryGetStringField(TEXT("type"), Param.Type);
			}
			if (Param.Name.IsEmpty() || Param.Type.IsEmpty())
			{
				Errors.Add(FString::Printf(TEXT("Function \"%s\": %s[%d] needs \"name\" and \"type\"."), *Owner, Field, i));
				continue;
			}
			Out.Add(MoveTemp(Param));
		}
	}

	/** nodes / connections / remove of one graph. */
	void ParsePart(const TSharedPtr<FJsonObject>& Obj, const FString& Label, FABGGraphPart& Part, TArray<FString>& OutErrors)
	{
		TSet<FString> Ids;

		const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
		if (Obj->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray)
		{
			for (int32 i = 0; i < NodesArray->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!(*NodesArray)[i].IsValid() || !(*NodesArray)[i]->TryGetObject(ObjPtr) || !ObjPtr)
				{
					OutErrors.Add(FString::Printf(TEXT("%snodes[%d] is not an object."), *Label, i));
					continue;
				}
				const TSharedPtr<FJsonObject>& NodeObj = *ObjPtr;

				FABGNodeSpec Node;
				FString RawType;
				NodeObj->TryGetStringField(TEXT("id"), Node.Id);
				NodeObj->TryGetStringField(TEXT("type"), RawType);
				NodeObj->TryGetStringField(TEXT("event"), Node.Event);
				NodeObj->TryGetStringField(TEXT("function"), Node.Function);
				NodeObj->TryGetStringField(TEXT("variable"), Node.Variable);
				NodeObj->TryGetStringField(TEXT("class"), Node.ClassName);

				if (Node.Id.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("%snodes[%d]: missing \"id\"."), *Label, i));
					continue;
				}
				if (Node.Id.Contains(TEXT(".")))
				{
					OutErrors.Add(FString::Printf(TEXT("%snodes[%d]: id \"%s\" must not contain '.'."), *Label, i, *Node.Id));
					continue;
				}
				if (Ids.Contains(Node.Id))
				{
					OutErrors.Add(FString::Printf(TEXT("%sDuplicate node id \"%s\"."), *Label, *Node.Id));
					continue;
				}
				if (RawType.IsEmpty() && ABGNodeId::LooksLikeExistingId(Node.Id))
				{
					RawType = ABGNodeType::Existing;
				}
				if (!CanonicalNodeType(RawType, Node.Type))
				{
					OutErrors.Add(FString::Printf(TEXT("%sNode \"%s\": unknown type \"%s\" (expected Event, CallFunction, Branch, SpawnActor, GetVariable, SetVariable, Cast, Self, Existing, FunctionEntry or FunctionResult)."), *Label, *Node.Id, *RawType));
					continue;
				}
				if (Node.Type == ABGNodeType::Existing)
				{
					if (!ABGNodeId::LooksLikeExistingId(Node.Id))
					{
						OutErrors.Add(FString::Printf(TEXT("%sNode \"%s\": an \"Existing\" node must use the id of an existing node (e_xxxxxxxx)."), *Label, *Node.Id));
						continue;
					}
				}
				else if (ABGNodeId::LooksLikeExistingId(Node.Id))
				{
					OutErrors.Add(FString::Printf(TEXT("%sNode \"%s\": ids starting with \"e_\" are reserved for existing nodes; pick another id."), *Label, *Node.Id));
					continue;
				}
				if (Node.Type == ABGNodeType::Event && Node.Event.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("%sEvent node \"%s\": missing \"event\"."), *Label, *Node.Id));
					continue;
				}
				if (Node.Type == ABGNodeType::CallFunction && Node.Function.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("%sCallFunction node \"%s\": missing \"function\"."), *Label, *Node.Id));
					continue;
				}
				if ((Node.Type == ABGNodeType::GetVariable || Node.Type == ABGNodeType::SetVariable) && Node.Variable.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("%s%s node \"%s\": missing \"variable\"."), *Label, *Node.Type, *Node.Id));
					continue;
				}
				if (Node.Type == ABGNodeType::Cast && Node.ClassName.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("%sCast node \"%s\": missing \"class\"."), *Label, *Node.Id));
					continue;
				}

				const TSharedPtr<FJsonObject>* PinsObj = nullptr;
				if (NodeObj->TryGetObjectField(TEXT("pins"), PinsObj) && PinsObj && PinsObj->IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PinsObj)->Values)
					{
						FString ValueText;
						if (JsonValueToString(Pair.Value, ValueText))
						{
							Node.PinDefaults.Add(Pair.Key, ValueText);
						}
						else
						{
							OutErrors.Add(FString::Printf(TEXT("%sNode \"%s\": pin \"%s\" default must be a string, number or bool."), *Label, *Node.Id, *Pair.Key));
						}
					}
				}

				Ids.Add(Node.Id);
				Part.Nodes.Add(MoveTemp(Node));
			}
		}

		// ---- connections (optional) ----
		const TArray<TSharedPtr<FJsonValue>>* ConnArray = nullptr;
		if (Obj->TryGetArrayField(TEXT("connections"), ConnArray) && ConnArray)
		{
			for (int32 i = 0; i < ConnArray->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!(*ConnArray)[i].IsValid() || !(*ConnArray)[i]->TryGetObject(ObjPtr) || !ObjPtr)
				{
					OutErrors.Add(FString::Printf(TEXT("%sconnections[%d] is not an object."), *Label, i));
					continue;
				}

				FString FromText, ToText;
				(*ObjPtr)->TryGetStringField(TEXT("from"), FromText);
				(*ObjPtr)->TryGetStringField(TEXT("to"), ToText);

				FABGConnectionSpec Conn;
				if (!ParsePinRef(FromText, Conn.From) || !ParsePinRef(ToText, Conn.To))
				{
					OutErrors.Add(FString::Printf(TEXT("%sconnections[%d]: \"from\"/\"to\" must look like \"nodeId.pinName\"."), *Label, i));
					continue;
				}
				// Existing nodes (e_xxxxxxxx) may be used without declaring them.
				if (!Ids.Contains(Conn.From.NodeId) && !ABGNodeId::LooksLikeExistingId(Conn.From.NodeId))
				{
					OutErrors.Add(FString::Printf(TEXT("%sconnections[%d]: unknown node \"%s\"."), *Label, i, *Conn.From.NodeId));
					continue;
				}
				if (!Ids.Contains(Conn.To.NodeId) && !ABGNodeId::LooksLikeExistingId(Conn.To.NodeId))
				{
					OutErrors.Add(FString::Printf(TEXT("%sconnections[%d]: unknown node \"%s\"."), *Label, i, *Conn.To.NodeId));
					continue;
				}
				Part.Connections.Add(MoveTemp(Conn));
			}
		}

		// ---- remove (optional) ----
		TArray<FString> Removals;
		ReadStringList(Obj, TEXT("remove"), Removals);
		for (const FString& Id : Removals)
		{
			if (ABGNodeId::LooksLikeExistingId(Id))
			{
				Part.Remove.Add(Id);
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s\"remove\": \"%s\" is not an existing-node id (e_xxxxxxxx)."), *Label, *Id));
			}
		}
	}
}

bool FABGGraphJson::Parse(const FString& InJson, FABGGraphSpec& OutSpec, TArray<FString>& OutErrors)
{
	OutSpec = FABGGraphSpec();
	const int32 ErrorsBefore = OutErrors.Num();

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("Invalid JSON: %s"), *Reader->GetErrorMessage()));
		return false;
	}

	// ---- "unsupported": the model's own explanation of what it could not do ----
	ReadStringList(Root, TEXT("unsupported"), OutSpec.Unsupported);

	// ---- variables (optional) ----
	{
		const TArray<TSharedPtr<FJsonValue>>* VarArray = nullptr;
		if (Root->TryGetArrayField(TEXT("variables"), VarArray) && VarArray)
		{
			TSet<FString> VarNames;
			for (int32 i = 0; i < VarArray->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* VarObj = nullptr;
				if (!(*VarArray)[i].IsValid() || !(*VarArray)[i]->TryGetObject(VarObj) || !VarObj)
				{
					OutErrors.Add(FString::Printf(TEXT("variables[%d] is not an object."), i));
					continue;
				}
				FABGVariableSpec Var;
				(*VarObj)->TryGetStringField(TEXT("name"), Var.Name);
				(*VarObj)->TryGetStringField(TEXT("type"), Var.Type);
				JsonValueToString((*VarObj)->TryGetField(TEXT("default")), Var.Default);
				if (Var.Name.IsEmpty() || Var.Type.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("variables[%d]: \"name\" and \"type\" are required."), i));
					continue;
				}
				if (VarNames.Contains(Var.Name))
				{
					OutErrors.Add(FString::Printf(TEXT("Duplicate variable \"%s\"."), *Var.Name));
					continue;
				}
				VarNames.Add(Var.Name);
				OutSpec.Variables.Add(MoveTemp(Var));
			}
		}
	}

	// ---- functions to create (optional) ----
	{
		const TArray<TSharedPtr<FJsonValue>>* FuncArray = nullptr;
		if (Root->TryGetArrayField(TEXT("functions"), FuncArray) && FuncArray)
		{
			for (int32 i = 0; i < FuncArray->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* FuncObj = nullptr;
				if (!(*FuncArray)[i].IsValid() || !(*FuncArray)[i]->TryGetObject(FuncObj) || !FuncObj)
				{
					OutErrors.Add(FString::Printf(TEXT("functions[%d] is not an object."), i));
					continue;
				}
				FABGFunctionSpec Func;
				(*FuncObj)->TryGetStringField(TEXT("name"), Func.Name);
				if (Func.Name.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("functions[%d]: \"name\" is required."), i));
					continue;
				}
				ReadParams(*FuncObj, TEXT("inputs"), Func.Name, Func.Inputs, OutErrors);
				ReadParams(*FuncObj, TEXT("outputs"), Func.Name, Func.Outputs, OutErrors);
				OutSpec.Functions.Add(MoveTemp(Func));
			}
		}
	}

	// ---- graph parts: top-level nodes/connections/remove (focused graph or "graph") + "graphs" ----
	{
		const bool bTopLevelPart = Root->HasField(TEXT("nodes")) || Root->HasField(TEXT("connections")) || Root->HasField(TEXT("remove"));
		if (bTopLevelPart)
		{
			FABGGraphPart Part;
			Root->TryGetStringField(TEXT("graph"), Part.GraphName);
			ParsePart(Root, FString(), Part, OutErrors);
			OutSpec.Parts.Add(MoveTemp(Part));
		}

		const TArray<TSharedPtr<FJsonValue>>* GraphsArray = nullptr;
		if (Root->TryGetArrayField(TEXT("graphs"), GraphsArray) && GraphsArray)
		{
			for (int32 i = 0; i < GraphsArray->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* GraphObj = nullptr;
				if (!(*GraphsArray)[i].IsValid() || !(*GraphsArray)[i]->TryGetObject(GraphObj) || !GraphObj)
				{
					OutErrors.Add(FString::Printf(TEXT("graphs[%d] is not an object."), i));
					continue;
				}
				FABGGraphPart Part;
				if (!(*GraphObj)->TryGetStringField(TEXT("graph"), Part.GraphName))
				{
					(*GraphObj)->TryGetStringField(TEXT("name"), Part.GraphName);
				}
				if (Part.GraphName.IsEmpty())
				{
					OutErrors.Add(FString::Printf(TEXT("graphs[%d]: \"graph\" (the graph's name) is required."), i));
					continue;
				}
				ParsePart(*GraphObj, FString::Printf(TEXT("graphs[%d] \"%s\": "), i, *Part.GraphName), Part, OutErrors);
				OutSpec.Parts.Add(MoveTemp(Part));
			}
		}
	}

	if (!OutSpec.HasWork() && OutSpec.Unsupported.Num() == 0 && OutErrors.Num() == ErrorsBefore)
	{
		OutErrors.Add(TEXT("Missing \"nodes\" array (or \"graphs\")."));
	}
	return OutErrors.Num() == ErrorsBefore;
}

FString FABGGraphJson::ExtractJson(const FString& RawModelText)
{
	FString Trimmed = RawModelText.TrimStartAndEnd();

	// A chat AI may write prose (even with braces) around the graph: prefer the last ``` block that holds a graph.
	{
		TArray<FString> Parts;
		Trimmed.ParseIntoArray(Parts, TEXT("```"), /*CullEmpty*/ false);
		// Parts at odd indices are inside fences.
		for (int32 i = Parts.Num() - 1; i >= 1; --i)
		{
			if ((i % 2) == 0)
			{
				continue;
			}
			FString Block = Parts[i];
			Block.TrimStartAndEndInline();
			if (Block.StartsWith(TEXT("json"), ESearchCase::IgnoreCase))
			{
				Block.RightChopInline(4);
				Block.TrimStartAndEndInline();
			}
			if (Block.StartsWith(TEXT("{")) && Block.Contains(TEXT("\"nodes\"")))
			{
				Trimmed = Block;
				break;
			}
		}
	}

	int32 First = INDEX_NONE;
	int32 Last = INDEX_NONE;
	Trimmed.FindChar(TEXT('{'), First);
	Trimmed.FindLastChar(TEXT('}'), Last);
	if (First != INDEX_NONE && Last != INDEX_NONE && Last > First)
	{
		return Trimmed.Mid(First, Last - First + 1);
	}
	return Trimmed;
}
