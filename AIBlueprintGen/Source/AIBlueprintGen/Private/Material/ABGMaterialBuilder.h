#pragma once

#include "CoreMinimal.h"
#include "Graph/ABGGraphBuilder.h" // FABGBuildResult
#include "Schema/ABGMaterialSpec.h"

class UMaterial;
class UMaterialFunction;

/** Applies a FABGMaterialSpec to a Material or a Material Function using the engine's MaterialEditingLibrary. */
class FABGMaterialBuilder
{
public:
	/**
	 * Exactly one of Material / Function must be set.
	 * All-or-nothing: on any error the transaction is cancelled and nothing is deleted.
	 * Note: the Material Editor works on a copy of the asset; the editor is refreshed from the asset afterwards,
	 * so changes not yet applied (Apply button) in that editor are replaced.
	 */
	static FABGBuildResult Build(UMaterial* Material, UMaterialFunction* Function, const FABGMaterialSpec& Spec);

	/**
	 * Lays out ALL expressions of the asset in columns by distance along the wires (sources on the left, the material
	 * outputs on the right). Fixes a graph where nodes were piled up on one spot. Only positions change.
	 * Returns the number of moved expressions.
	 */
	static int32 Arrange(UMaterial* Material, UMaterialFunction* Function);
};
