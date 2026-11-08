#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"

/**
 * Stable short id for an existing node ("e_1a2b3c4d" = first 8 hex digits of its guid).
 * Shared by the exporter (writes them) and the builder (resolves them), so an AI can refer to existing nodes.
 */
namespace ABGNodeId
{
	inline FString Make(const UEdGraphNode* Node)
	{
		return TEXT("e_") + Node->NodeGuid.ToString(EGuidFormats::Digits).Left(8).ToLower();
	}

	inline bool LooksLikeExistingId(const FString& Id)
	{
		return Id.StartsWith(TEXT("e_"), ESearchCase::CaseSensitive);
	}
}
