#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"

namespace ABGMaterialId
{
	/** "MaterialExpressionMultiply_2" -> "e_Multiply_2". Object names are unique inside a material. */
	inline FString Make(const UMaterialExpression* Expression)
	{
		FString Name = Expression->GetName();
		Name.RemoveFromStart(TEXT("MaterialExpression"));
		return TEXT("e_") + Name;
	}

	/** Short class name: "MaterialExpressionMultiply" -> "Multiply". */
	inline FString TypeName(const UMaterialExpression* Expression)
	{
		FString Name = Expression->GetClass()->GetName();
		Name.RemoveFromStart(TEXT("MaterialExpression"));
		return Name;
	}
}
