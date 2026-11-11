#include "Schema/ABGMaterialJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool ValueToString(const TSharedPtr<FJsonValue>& Value, FString& Out)
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

	bool IsExistingId(const FString& Id)
	{
		return Id.StartsWith(TEXT("e_"), ESearchCase::CaseSensitive);
	}

	/** "node", "node.Pin", "Material.BaseColor". Pin may be empty. */
	bool ParseMatPinRef(const FString& In, FABGMatPinRef& Out)
	{
		const FString Text = In.TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			return false;
		}
		int32 DotIdx = INDEX_NONE;
		if (Text.FindChar(TEXT('.'), DotIdx))
		{
			Out.NodeId = Text.Left(DotIdx);
			Out.PinName = Text.Mid(DotIdx + 1);
		}
		else
		{
			Out.NodeId = Text;
			Out.PinName.Empty();
		}
		return !Out.NodeId.IsEmpty();
	}

	void ReadMatStringList(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<FString>& Out)
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
				if (ValueToString(Item, Text) && !Text.TrimStartAndEnd().IsEmpty())
				{
					Out.Add(Text.TrimStartAndEnd());
				}
			}
		}
	}

	void ReadPairs(const TSharedPtr<FJsonObject>& Obj, TArray<TPair<FString, FString>>& Out, const FString& Where, TArray<FString>& Errors)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Obj->Values)
		{
			FString Text;
			if (!ValueToString(Pair.Value, Text))
			{
				Errors.Add(FString::Printf(TEXT("%s: value of \"%s\" must be a string, number or bool."), *Where, *Pair.Key));
				continue;
			}
			Out.Emplace(Pair.Key, Text);
		}
	}
}

bool FABGMaterialJson::Parse(const FString& InJson, FABGMaterialSpec& OutSpec, TArray<FString>& OutErrors)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("Invalid JSON (could not parse). Check commas, quotes and brackets."));
		return false;
	}

	const int32 ErrorsBefore = OutErrors.Num();
	ReadMatStringList(Root, TEXT("unsupported"), OutSpec.Unsupported);

	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("settings"), SettingsObj) && SettingsObj)
	{
		ReadPairs(*SettingsObj, OutSpec.Settings, TEXT("settings"), OutErrors);
	}

	TSet<FString> Declared;
	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (Root->TryGetArrayField(TEXT("nodes"), NodesArr) && NodesArr)
	{
		for (int32 i = 0; i < NodesArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			if (!(*NodesArr)[i]->TryGetObject(NodeObj) || !NodeObj)
			{
				OutErrors.Add(FString::Printf(TEXT("nodes[%d] is not an object."), i));
				continue;
			}
			FABGMatNode Node;
			(*NodeObj)->TryGetStringField(TEXT("id"), Node.Id);
			(*NodeObj)->TryGetStringField(TEXT("type"), Node.Type);
			Node.Id = Node.Id.TrimStartAndEnd();
			Node.Type = Node.Type.TrimStartAndEnd();
			if (Node.Id.IsEmpty() || Node.Id.Contains(TEXT(".")) || Node.Id.Equals(ABGMaterialRootName, ESearchCase::IgnoreCase))
			{
				OutErrors.Add(FString::Printf(TEXT("nodes[%d]: \"id\" is missing, contains a dot, or is the reserved word \"Material\"."), i));
				continue;
			}
			if (Declared.Contains(Node.Id))
			{
				OutErrors.Add(FString::Printf(TEXT("Duplicate node id \"%s\"."), *Node.Id));
				continue;
			}
			Declared.Add(Node.Id);
			if (Node.Type.IsEmpty())
			{
				OutErrors.Add(FString::Printf(TEXT("Node \"%s\": \"type\" is missing."), *Node.Id));
				continue;
			}
			if (Node.Type.Equals(ABGMaterialNodeType::Existing, ESearchCase::IgnoreCase))
			{
				Node.Type = ABGMaterialNodeType::Existing;
				if (!IsExistingId(Node.Id))
				{
					OutErrors.Add(FString::Printf(TEXT("Node \"%s\": an Existing node needs an id from the Material description (e_...)."), *Node.Id));
					continue;
				}
			}
			else if (IsExistingId(Node.Id))
			{
				OutErrors.Add(FString::Printf(TEXT("Node \"%s\": ids starting with \"e_\" are reserved for existing nodes."), *Node.Id));
				continue;
			}
			const TSharedPtr<FJsonObject>* PropsObj = nullptr;
			if ((*NodeObj)->TryGetObjectField(TEXT("props"), PropsObj) && PropsObj)
			{
				ReadPairs(*PropsObj, Node.Props, FString::Printf(TEXT("Node \"%s\" props"), *Node.Id), OutErrors);
			}
			else if ((*NodeObj)->TryGetObjectField(TEXT("pins"), PropsObj) && PropsObj) // tolerate the Blueprint spelling
			{
				ReadPairs(*PropsObj, Node.Props, FString::Printf(TEXT("Node \"%s\" props"), *Node.Id), OutErrors);
			}
			OutSpec.Nodes.Add(MoveTemp(Node));
		}
	}

	auto CheckEndpoint = [&](const FABGMatPinRef& Ref, bool bAllowRoot, const FString& Where)
	{
		if (Ref.NodeId.Equals(ABGMaterialRootName, ESearchCase::IgnoreCase))
		{
			if (!bAllowRoot)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: \"Material\" can only be the target (\"to\") of a connection."), *Where));
			}
			else if (Ref.PinName.IsEmpty())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: name the material output, e.g. \"Material.BaseColor\"."), *Where));
			}
			return;
		}
		if (!Declared.Contains(Ref.NodeId) && !IsExistingId(Ref.NodeId))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: node \"%s\" is not declared in \"nodes\" (and is not an existing e_... id)."), *Where, *Ref.NodeId));
		}
	};

	const TArray<TSharedPtr<FJsonValue>>* ConnArr = nullptr;
	if (Root->TryGetArrayField(TEXT("connections"), ConnArr) && ConnArr)
	{
		for (int32 i = 0; i < ConnArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* ConnObj = nullptr;
			FString FromText, ToText;
			if (!(*ConnArr)[i]->TryGetObject(ConnObj) || !ConnObj
				|| !(*ConnObj)->TryGetStringField(TEXT("from"), FromText) || !(*ConnObj)->TryGetStringField(TEXT("to"), ToText))
			{
				OutErrors.Add(FString::Printf(TEXT("connections[%d] needs \"from\" and \"to\" strings."), i));
				continue;
			}
			FABGMatConnection Conn;
			if (!ParseMatPinRef(FromText, Conn.From) || !ParseMatPinRef(ToText, Conn.To))
			{
				OutErrors.Add(FString::Printf(TEXT("connections[%d]: \"from\"/\"to\" must look like \"node\", \"node.Pin\" or \"Material.BaseColor\"."), i));
				continue;
			}
			const FString Where = FString::Printf(TEXT("connections[%d]"), i);
			CheckEndpoint(Conn.From, /*bAllowRoot*/ false, Where);
			CheckEndpoint(Conn.To, /*bAllowRoot*/ true, Where);
			OutSpec.Connections.Add(MoveTemp(Conn));
		}
	}

	TArray<FString> DisconnectTexts;
	ReadMatStringList(Root, TEXT("disconnect"), DisconnectTexts);
	for (const FString& Text : DisconnectTexts)
	{
		FABGMatPinRef Ref;
		if (!ParseMatPinRef(Text, Ref) || Ref.PinName.IsEmpty())
		{
			OutErrors.Add(FString::Printf(TEXT("disconnect \"%s\": must look like \"node.Input\" or \"Material.BaseColor\"."), *Text));
			continue;
		}
		CheckEndpoint(Ref, /*bAllowRoot*/ true, FString::Printf(TEXT("disconnect \"%s\""), *Text));
		OutSpec.Disconnect.Add(Ref);
	}

	TArray<FString> RemoveIds;
	ReadMatStringList(Root, TEXT("remove"), RemoveIds);
	for (const FString& Id : RemoveIds)
	{
		if (!IsExistingId(Id))
		{
			OutErrors.Add(FString::Printf(TEXT("remove \"%s\": only existing nodes (e_... ids from the Material description) can be removed."), *Id));
			continue;
		}
		OutSpec.Remove.Add(Id);
	}

	if (!OutSpec.HasWork() && OutSpec.Unsupported.Num() == 0 && OutErrors.Num() == ErrorsBefore)
	{
		OutErrors.Add(TEXT("The reply contains nothing to build (no \"nodes\", \"settings\", \"connections\", \"disconnect\" or \"remove\")."));
	}
	return OutErrors.Num() == ErrorsBefore;
}
