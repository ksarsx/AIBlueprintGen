#include "Prompt/ABGPromptBuilder.h"
#include "ABGSettings.h"
#include "Misc/EngineVersion.h"

FString FABGPromptBuilder::BuildFormatSpec(bool bIncludeExamples)
{
	const TCHAR* Lines[] =
	{
		TEXT("JSON format (everything except the graph content is optional):"),
		TEXT("{"),
		TEXT("  \"variables\": [ { \"name\": \"Cube\", \"type\": \"StaticMeshActor\" } ],"),
		TEXT("  \"functions\": [ { \"name\": \"MyFunction\", \"inputs\": [ { \"name\": \"Amount\", \"type\": \"int\" } ], \"outputs\": [ { \"name\": \"Result\", \"type\": \"bool\" } ] } ],"),
		TEXT("  \"graph\": \"<graph name, optional: default is the graph the user has open>\","),
		TEXT("  \"nodes\": [ { \"id\": \"<unique id, letters/digits only>\", \"type\": \"Event\" | \"CallFunction\" | \"Branch\" | \"SpawnActor\" | \"GetVariable\" | \"SetVariable\" | \"Cast\" | \"Self\" | \"Existing\" | \"FunctionEntry\" | \"FunctionResult\", ... } ],"),
		TEXT("  \"connections\": [ { \"from\": \"<nodeId>.<outputPin>\", \"to\": \"<nodeId>.<inputPin>\" } ],"),
		TEXT("  \"remove\": [ \"<id of an existing node, e_xxxxxxxx>\" ],"),
		TEXT("  \"unsupported\": [ \"<what could not be done and why>\" ]"),
		TEXT("}"),
		TEXT("To edit several graphs in one answer put \"graphs\": [ { \"graph\": \"EventGraph\", \"nodes\": [...], \"connections\": [...], \"remove\": [...] }, { \"graph\": \"MyFunction\", ... } ] instead of the top-level graph/nodes/connections/remove."),
		TEXT("Graph names: \"EventGraph\" (and other event graphs), or the name of a function or macro. A function in \"functions\" is created first, then its body is given as a graph with the same name."),
		TEXT(""),
		TEXT("Variables: \"type\" is bool, int, float, string, name, text, vector, rotator, transform, or a class name (Actor, StaticMeshActor, StaticMeshComponent, ...) for an object reference."),
		TEXT("  Variables you declare become normal Blueprint member variables. Variables and components that already exist in the Blueprint can be used without declaring them."),
		TEXT(""),
		TEXT("Node types:"),
		TEXT("- Event: { \"id\": \"begin\", \"type\": \"Event\", \"event\": \"<event function name>\" }"),
		TEXT("  Examples of event names: ReceiveBeginPlay, ReceiveTick, ReceiveActorBeginOverlap. Its exec output pin is \"then\"."),
		TEXT("- CallFunction: { \"id\": \"x\", \"type\": \"CallFunction\", \"function\": \"<Class>.<Function>\", \"pins\": { \"<inputPin>\": \"<default value>\" } }"),
		TEXT("  Class is the Unreal class name without the C++ prefix (KismetSystemLibrary, KismetMathLibrary, GameplayStatics, ...)."),
		TEXT("  Use only real BlueprintCallable functions. Exec input pin is \"execute\", exec output pin is \"then\"."),
		TEXT("- Branch: { \"id\": \"b\", \"type\": \"Branch\" }"),
		TEXT("  Pins: input \"execute\", input \"Condition\" (bool), outputs \"then\" (true) and \"else\" (false)."),
		TEXT("- SpawnActor: { \"id\": \"s\", \"type\": \"SpawnActor\", \"class\": \"StaticMeshActor\" }"),
		TEXT("  Pins: input \"execute\", input \"SpawnTransform\", outputs \"then\" and \"ReturnValue\" (the new actor, typed as the class)."),
		TEXT("- GetVariable: { \"id\": \"g\", \"type\": \"GetVariable\", \"variable\": \"Cube\" }  Pure node, its one output pin is named like the variable."),
		TEXT("- SetVariable: { \"id\": \"w\", \"type\": \"SetVariable\", \"variable\": \"Cube\" }  Pins: input \"execute\", input named like the variable, output \"then\"."),
		TEXT("- Cast: { \"id\": \"c\", \"type\": \"Cast\", \"class\": \"StaticMeshActor\" }"),
		TEXT("  Pins: input \"execute\", input \"Object\", outputs \"then\", \"CastFailed\" and \"As<ClassName>\" (e.g. \"AsStaticMeshActor\"). Use it when a value must become a more specific type."),
		TEXT("- Self: { \"id\": \"me\", \"type\": \"Self\" }  Output pin \"self\" is this Blueprint's actor."),
		TEXT("- Existing: { \"id\": \"e_1a2b3c4d\", \"type\": \"Existing\", \"pins\": { \"InString\": \"New text\" } }"),
		TEXT("  Refers to a node that is ALREADY in the graph (ids are listed in the Blueprint description below). Use it to connect new nodes to it or to change its pin default values. Never recreate existing nodes."),
		TEXT("- FunctionEntry: { \"id\": \"entry\", \"type\": \"FunctionEntry\" }  The start node of a function graph. Exec output \"then\"; the function's inputs are output pins named like the parameters."),
		TEXT("- FunctionResult: { \"id\": \"ret\", \"type\": \"FunctionResult\" }  The return node of a function graph. Exec input \"execute\"; the function's outputs are input pins named like the parameters."),
		TEXT("- To call a function of this Blueprint use CallFunction with \"function\": \"self.MyFunction\" (declare it in \"functions\" first if it is new)."),
		TEXT(""),
		TEXT("Rules:"),
		TEXT("- Pin default values are always strings, e.g. \"Hello\", \"1.5\", \"true\"."),
		TEXT("- Execution flow connections go from an output exec pin (\"then\" / \"else\") to an input exec pin (\"execute\")."),
		TEXT("- Data connections go from an output pin (e.g. \"ReturnValue\") to an input pin of another node."),
		TEXT("- Use pin names exactly as Unreal names them (for PrintString: InString, bPrintToScreen, bPrintToLog, Duration)."),
		TEXT("- Functions that work on an object have a \"self\" Target input: connect the object there (e.g. a GetVariable output). Left unconnected, Target is this Blueprint itself."),
		TEXT("- Class pins take a class name (\"StaticMeshActor\") or a full path. Asset pins take a full path (\"/Game/Folder/Asset\") or one of Cube, Sphere, Cylinder, Cone, Plane (engine basic shapes)."),
		TEXT("- Vectors are \"X,Y,Z\". Rotators are \"Pitch,Yaw,Roll\": spinning around the vertical axis is the MIDDLE value, e.g. \"0,1,0\". Enum pins take the entry name (\"Movable\")."),
		TEXT("- Function names are as in the Blueprint editor without spaces; a missing K2_ prefix is added automatically."),
		TEXT("- Components cannot be created by this format. Only use ones that already exist in the Blueprint, or spawn an actor instead."),
		TEXT("- Event nodes only work in event graphs. Inside a function use FunctionEntry as the start. Macros, delegates and construction script content can only be edited through \"Existing\" nodes."),
		TEXT("- If a Blueprint description is given below, use its variables, components, functions and node ids. Do not recreate things that already exist. Remove nodes only when I ask for it."),
		TEXT("- New nodes are placed automatically. To add to an existing chain connect the new nodes to \"Existing\" nodes; the old connection of that pin is replaced."),
		TEXT("- The plugin applies the answer ALL OR NOTHING: one wrong function, class or pin name rejects the whole graph. Check every name before answering."),
		TEXT("- Write each function under the class that DECLARES it: SetStaticMesh and SetMaterial belong to StaticMeshComponent (not StaticMeshActor), SetMobility and SetWorldScale3D to SceneComponent, AddActorLocalRotation / SetActorLocation / GetActorLocation / Destroy Actor to Actor, PrintString to KismetSystemLibrary, MakeTransform / RandomFloat to KismetMathLibrary."),
		TEXT("- The component of another actor is read with CallFunction \"StaticMeshActor.GetStaticMeshComponent\" (a property getter; input \"self\", output \"ReturnValue\"). Other Blueprint-visible properties work the same way: \"Class.GetPropertyName\"."),
		TEXT("- If you are not sure a function exists, prefer a simpler well-known alternative or say so in \"unsupported\" instead of guessing."),
		TEXT("- Events that are already in the graph (ReceiveBeginPlay, ReceiveTick, ...) must not be duplicated: connect to the existing one by using it as an \"Existing\" node (its \"then\" pin)."),
		TEXT("- Do not specify node positions. Create only what the user asked for."),
		TEXT("- If something is impossible with these node types, do NOT invent functions, pins or node types. Write the reason in \"unsupported\": [\"what could not be done and why\"]. Build everything that is possible; if nothing is possible reply with \"nodes\": [] and only the reason."),
		TEXT(""),
	};

	const TCHAR* ExampleLines[] =
	{
		TEXT("Example. User request: print Hello when the game starts."),
		TEXT("{"),
		TEXT("  \"nodes\": ["),
		TEXT("    { \"id\": \"begin\", \"type\": \"Event\", \"event\": \"ReceiveBeginPlay\" },"),
		TEXT("    { \"id\": \"print\", \"type\": \"CallFunction\", \"function\": \"KismetSystemLibrary.PrintString\", \"pins\": { \"InString\": \"Hello\" } }"),
		TEXT("  ],"),
		TEXT("  \"connections\": [ { \"from\": \"begin.then\", \"to\": \"print.execute\" } ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Example. User request: on begin play, if true print A, otherwise print B."),
		TEXT("{"),
		TEXT("  \"nodes\": ["),
		TEXT("    { \"id\": \"begin\", \"type\": \"Event\", \"event\": \"ReceiveBeginPlay\" },"),
		TEXT("    { \"id\": \"check\", \"type\": \"Branch\", \"pins\": { \"Condition\": \"true\" } },"),
		TEXT("    { \"id\": \"printA\", \"type\": \"CallFunction\", \"function\": \"KismetSystemLibrary.PrintString\", \"pins\": { \"InString\": \"A\" } },"),
		TEXT("    { \"id\": \"printB\", \"type\": \"CallFunction\", \"function\": \"KismetSystemLibrary.PrintString\", \"pins\": { \"InString\": \"B\" } }"),
		TEXT("  ],"),
		TEXT("  \"connections\": ["),
		TEXT("    { \"from\": \"begin.then\", \"to\": \"check.execute\" },"),
		TEXT("    { \"from\": \"check.then\", \"to\": \"printA.execute\" },"),
		TEXT("    { \"from\": \"check.else\", \"to\": \"printB.execute\" }"),
		TEXT("  ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Example. User request: spawn a cube when the game starts and spin it slowly."),
		TEXT("{"),
		TEXT("  \"variables\": [ { \"name\": \"Cube\", \"type\": \"StaticMeshActor\" } ],"),
		TEXT("  \"nodes\": ["),
		TEXT("    { \"id\": \"begin\", \"type\": \"Event\", \"event\": \"ReceiveBeginPlay\" },"),
		TEXT("    { \"id\": \"spawn\", \"type\": \"SpawnActor\", \"class\": \"StaticMeshActor\" },"),
		TEXT("    { \"id\": \"store\", \"type\": \"SetVariable\", \"variable\": \"Cube\" },"),
		TEXT("    { \"id\": \"cube1\", \"type\": \"GetVariable\", \"variable\": \"Cube\" },"),
		TEXT("    { \"id\": \"mesh\", \"type\": \"CallFunction\", \"function\": \"StaticMeshActor.GetStaticMeshComponent\" },"),
		TEXT("    { \"id\": \"mobile\", \"type\": \"CallFunction\", \"function\": \"SceneComponent.SetMobility\", \"pins\": { \"NewMobility\": \"Movable\" } },"),
		TEXT("    { \"id\": \"setMesh\", \"type\": \"CallFunction\", \"function\": \"StaticMeshComponent.SetStaticMesh\", \"pins\": { \"NewMesh\": \"Cube\" } },"),
		TEXT("    { \"id\": \"tick\", \"type\": \"Event\", \"event\": \"ReceiveTick\" },"),
		TEXT("    { \"id\": \"cube2\", \"type\": \"GetVariable\", \"variable\": \"Cube\" },"),
		TEXT("    { \"id\": \"spin\", \"type\": \"CallFunction\", \"function\": \"Actor.AddActorLocalRotation\", \"pins\": { \"DeltaRotation\": \"0,1,0\" } }"),
		TEXT("  ],"),
		TEXT("  \"connections\": ["),
		TEXT("    { \"from\": \"begin.then\", \"to\": \"spawn.execute\" },"),
		TEXT("    { \"from\": \"spawn.then\", \"to\": \"store.execute\" },"),
		TEXT("    { \"from\": \"spawn.ReturnValue\", \"to\": \"store.Cube\" },"),
		TEXT("    { \"from\": \"store.then\", \"to\": \"mobile.execute\" },"),
		TEXT("    { \"from\": \"cube1.Cube\", \"to\": \"mesh.self\" },"),
		TEXT("    { \"from\": \"mesh.ReturnValue\", \"to\": \"mobile.self\" },"),
		TEXT("    { \"from\": \"mobile.then\", \"to\": \"setMesh.execute\" },"),
		TEXT("    { \"from\": \"mesh.ReturnValue\", \"to\": \"setMesh.self\" },"),
		TEXT("    { \"from\": \"tick.then\", \"to\": \"spin.execute\" },"),
		TEXT("    { \"from\": \"cube2.Cube\", \"to\": \"spin.self\" }"),
		TEXT("  ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Example. User request: create a function PrintTwice that prints a text two times, and call it on begin play."),
		TEXT("{"),
		TEXT("  \"functions\": [ { \"name\": \"PrintTwice\", \"inputs\": [ { \"name\": \"Text\", \"type\": \"string\" } ] } ],"),
		TEXT("  \"graphs\": ["),
		TEXT("    { \"graph\": \"PrintTwice\","),
		TEXT("      \"nodes\": ["),
		TEXT("        { \"id\": \"entry\", \"type\": \"FunctionEntry\" },"),
		TEXT("        { \"id\": \"p1\", \"type\": \"CallFunction\", \"function\": \"KismetSystemLibrary.PrintString\" },"),
		TEXT("        { \"id\": \"p2\", \"type\": \"CallFunction\", \"function\": \"KismetSystemLibrary.PrintString\" }"),
		TEXT("      ],"),
		TEXT("      \"connections\": ["),
		TEXT("        { \"from\": \"entry.then\", \"to\": \"p1.execute\" },"),
		TEXT("        { \"from\": \"entry.Text\", \"to\": \"p1.InString\" },"),
		TEXT("        { \"from\": \"p1.then\", \"to\": \"p2.execute\" },"),
		TEXT("        { \"from\": \"entry.Text\", \"to\": \"p2.InString\" }"),
		TEXT("      ] },"),
		TEXT("    { \"graph\": \"EventGraph\","),
		TEXT("      \"nodes\": ["),
		TEXT("        { \"id\": \"begin\", \"type\": \"Event\", \"event\": \"ReceiveBeginPlay\" },"),
		TEXT("        { \"id\": \"call\", \"type\": \"CallFunction\", \"function\": \"self.PrintTwice\", \"pins\": { \"Text\": \"Hi\" } }"),
		TEXT("      ],"),
		TEXT("      \"connections\": [ { \"from\": \"begin.then\", \"to\": \"call.execute\" } ] }"),
		TEXT("  ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Example. User request: make the player fly (this needs C++ that is not available as a node here)."),
		TEXT("{ \"nodes\": [], \"unsupported\": [ \"Flying needs changes to the character movement mode through a component that cannot be created with these node types.\" ] }"),
	};

	// Target version comes from the running editor, e.g. "5.8.4".
	FString Result = FString::Printf(
		TEXT("Target engine: Unreal Engine %s. Use only functions, events and pin names that exist in this version.\n\n"),
		*FEngineVersion::Current().ToString(EVersionComponent::Patch));
	for (const TCHAR* Line : Lines)
	{
		Result += Line;
		Result += TEXT("\n");
	}
	if (bIncludeExamples)
	{
		for (const TCHAR* Line : ExampleLines)
		{
			Result += Line;
			Result += TEXT("\n");
		}
	}
	return Result;
}

FString FABGPromptBuilder::BuildMaterialFormatSpec(bool bIncludeExamples)
{
	const TCHAR* Lines[] =
	{
		TEXT("JSON format (every key is optional, but the answer must contain something to build or an \"unsupported\" reason):"),
		TEXT("{"),
		TEXT("  \"settings\": { \"BlendMode\": \"Translucent\", \"TwoSided\": \"true\" },"),
		TEXT("  \"textures\": [ { \"name\": \"T_Brick_D\", \"file\": \"C:/Textures/brick.png\", \"usage\": \"Color\" | \"Normal\" | \"Mask\" } ],"),
		TEXT("  \"nodes\": [ { \"id\": \"<unique id, letters/digits only>\", \"type\": \"<expression name>\", \"props\": { \"<property>\": \"<value>\" } } ],"),
		TEXT("  \"connections\": [ { \"from\": \"<nodeId>[.<outputName>]\", \"to\": \"<nodeId>.<inputName>\" } ],"),
		TEXT("  \"disconnect\": [ \"<nodeId>.<inputName>\" ],"),
		TEXT("  \"remove\": [ \"<id of an existing expression, e_...>\" ],"),
		TEXT("  \"unsupported\": [ \"<what could not be done and why>\" ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Nodes:"),
		TEXT("- \"type\" is the material expression class name without the \"MaterialExpression\" prefix, as in the Unreal source: Constant, Constant2Vector, Constant3Vector, Constant4Vector, "
			"ScalarParameter, VectorParameter, TextureSample, TextureSampleParameter2D, TextureCoordinate, Panner, Time, Add, Subtract, Multiply, Divide, "
			"LinearInterpolate (Lerp), OneMinus, Saturate, Power, Fresnel, ComponentMask, AppendVector, Desaturation, FunctionInput, FunctionOutput, ..."),
		TEXT("- \"props\" sets properties of the expression; names are those of the Details panel without spaces. Values are strings:"),
		TEXT("  numbers \"0.5\", booleans \"true\"/\"false\", colors and vectors \"R,G,B\" or \"R,G,B,A\" / \"X,Y,Z\", enums by entry name (\"Normal\" or \"SAMPLERTYPE_Normal\"), "
			"assets by full path (\"/Game/Textures/T_Rock_D\")."),
		TEXT("  Constant: R (the value). Constant2Vector: R, G (not \"Constant\"). Constant3Vector / Constant4Vector: Constant (color). ScalarParameter: ParameterName, DefaultValue. VectorParameter: ParameterName, DefaultValue (color). "
			"TextureSample / TextureSampleParameter2D: Texture, ParameterName for the parameter version (do NOT set SamplerType: the plugin takes it from the texture). "
			"TextureCoordinate: CoordinateIndex, UTiling, VTiling. Panner: SpeedX, SpeedY. Multiply/Add/...: ConstA, ConstB are used while input A/B is not connected."),
		TEXT("- Existing: { \"id\": \"e_Multiply_0\", \"type\": \"Existing\", \"props\": { ... } } refers to an expression that is ALREADY in the material (ids are listed in the material description below). "
			"Use it to connect to it or to change its properties. Never recreate existing expressions."),
		TEXT(""),
		TEXT("Connections:"),
		TEXT("- \"from\" is a node, optionally with the output name: \"tex.RGB\", \"tex.R\". Leave the output out for nodes with a single output. TextureSample outputs: RGB, R, G, B, A, RGBA."),
		TEXT("- \"to\" is a node input (\"mul.A\", \"mul.B\", \"lerp.Alpha\", \"tex.UVs\") or a material output: \"Material.BaseColor\", \"Material.Metallic\", \"Material.Specular\", "
			"\"Material.Roughness\", \"Material.EmissiveColor\", \"Material.Opacity\", \"Material.OpacityMask\", \"Material.Normal\", \"Material.WorldPositionOffset\", "
			"\"Material.AmbientOcclusion\", \"Material.Refraction\"."),
		TEXT("- An input has one wire: a new connection replaces the old one. Use \"disconnect\" (\"Material.Roughness\" or \"node.Input\") to cut a wire without a replacement."),
		TEXT(""),
		TEXT("Rules:"),
		TEXT("- The plugin applies the answer ALL OR NOTHING: one wrong expression, property, pin or asset name rejects everything. Check every name before answering."),
		TEXT("- Value types must match: a color/vector output cannot feed a scalar input directly; use a ComponentMask or a scalar output (tex.R)."),
		TEXT("- Do not recreate or duplicate expressions that already exist; reuse them as \"Existing\" nodes. Remove expressions only when I ask for it."),
		TEXT("- Blend mode, shading model, two-sided and other material flags go into \"settings\" (BlendMode: Opaque, Masked, Translucent, Additive, Modulate; "
			"ShadingModel: DefaultLit, Unlit, Subsurface, ...; TwoSided: true/false). Translucent and Masked materials also need Opacity or OpacityMask connected."),
		TEXT("- Textures that already exist in the project: use their asset path only if I give it to you or it appears in the material description. Never invent asset paths."),
		TEXT("- Image files on disk: if I name a file (for example C:/Textures/brick.png), list it in \"textures\"; the plugin imports it as a Texture2D asset named \"name\" next to the material. "
			"\"usage\" is Color (default, sRGB), Normal (normal map) or Mask (linear data such as roughness, metallic or ambient occlusion). "
			"In a node, set \"Texture\" to that same \"name\" (for example \"Texture\": \"T_Brick_D\"). Use only file paths I wrote; never invent a file."),
		TEXT("- If a texture is needed but I gave neither an asset path nor a file, use a TextureSampleParameter2D without Texture, so I can assign it later, or say it in \"unsupported\". "
			"Never set SamplerType: the plugin matches it to the texture automatically."),
		TEXT("- Nodes with a single unnamed input (Sine, Cosine, OneMinus, Saturate, Abs, Floor, Frac, Sqrt, ...): connect with just the node id, e.g. { \"from\": \"a\", \"to\": \"oneMinus\" } or \"to\": \"oneMinus.Input\". Never invent other pin names."),
		TEXT("- New expressions are placed automatically; do not specify positions. Create only what the user asked for."),
		TEXT("- If something is impossible, do NOT invent expressions, properties or pins. Write the reason in \"unsupported\" and build everything that is possible."),
	};

	const TCHAR* ExampleLines[] =
	{
		TEXT(""),
		TEXT("Example. User request: make the material a rough red plastic."),
		TEXT("{"),
		TEXT("  \"nodes\": ["),
		TEXT("    { \"id\": \"red\", \"type\": \"Constant3Vector\", \"props\": { \"Constant\": \"0.8,0.02,0.02\" } },"),
		TEXT("    { \"id\": \"rough\", \"type\": \"Constant\", \"props\": { \"R\": \"0.7\" } },"),
		TEXT("    { \"id\": \"metal\", \"type\": \"Constant\", \"props\": { \"R\": \"0\" } }"),
		TEXT("  ],"),
		TEXT("  \"connections\": ["),
		TEXT("    { \"from\": \"red\", \"to\": \"Material.BaseColor\" },"),
		TEXT("    { \"from\": \"rough\", \"to\": \"Material.Roughness\" },"),
		TEXT("    { \"from\": \"metal\", \"to\": \"Material.Metallic\" }"),
		TEXT("  ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Example. User request: texture /Game/Textures/T_Brick_D as base color, tinted by a color parameter named Tint, tiled 4 times, plus its normal map /Game/Textures/T_Brick_N."),
		TEXT("{"),
		TEXT("  \"nodes\": ["),
		TEXT("    { \"id\": \"uv\", \"type\": \"TextureCoordinate\", \"props\": { \"UTiling\": \"4\", \"VTiling\": \"4\" } },"),
		TEXT("    { \"id\": \"albedo\", \"type\": \"TextureSample\", \"props\": { \"Texture\": \"/Game/Textures/T_Brick_D\" } },"),
		TEXT("    { \"id\": \"normalTex\", \"type\": \"TextureSample\", \"props\": { \"Texture\": \"/Game/Textures/T_Brick_N\" } },"),
		TEXT("    { \"id\": \"tint\", \"type\": \"VectorParameter\", \"props\": { \"ParameterName\": \"Tint\", \"DefaultValue\": \"1,1,1,1\" } },"),
		TEXT("    { \"id\": \"tinted\", \"type\": \"Multiply\" }"),
		TEXT("  ],"),
		TEXT("  \"connections\": ["),
		TEXT("    { \"from\": \"uv\", \"to\": \"albedo.UVs\" },"),
		TEXT("    { \"from\": \"uv\", \"to\": \"normalTex.UVs\" },"),
		TEXT("    { \"from\": \"albedo.RGB\", \"to\": \"tinted.A\" },"),
		TEXT("    { \"from\": \"tint\", \"to\": \"tinted.B\" },"),
		TEXT("    { \"from\": \"tinted\", \"to\": \"Material.BaseColor\" },"),
		TEXT("    { \"from\": \"normalTex.RGB\", \"to\": \"Material.Normal\" }"),
		TEXT("  ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Example. User request: make it translucent and fade it to 40% opacity."),
		TEXT("{"),
		TEXT("  \"settings\": { \"BlendMode\": \"Translucent\" },"),
		TEXT("  \"nodes\": [ { \"id\": \"alpha\", \"type\": \"Constant\", \"props\": { \"R\": \"0.4\" } } ],"),
		TEXT("  \"connections\": [ { \"from\": \"alpha\", \"to\": \"Material.Opacity\" } ]"),
		TEXT("}"),
		TEXT(""),
		TEXT("Example. User request: make the material emit light that pulses (this needs a custom post process, not possible here)."),
		TEXT("{ \"unsupported\": [ \"A post-process pulse effect is not a material expression; only an emissive color driven by Time/Sine can be built here.\" ] }"),
	};

	FString Result = FString::Printf(
		TEXT("Target engine: Unreal Engine %s. Use only expressions, properties and pin names that exist in this version.\n\n"),
		*FEngineVersion::Current().ToString(EVersionComponent::Patch));
	for (const TCHAR* Line : Lines)
	{
		Result += Line;
		Result += TEXT("\n");
	}
	if (bIncludeExamples)
	{
		for (const TCHAR* Line : ExampleLines)
		{
			Result += Line;
			Result += TEXT("\n");
		}
	}
	return Result;
}

FString FABGPromptBuilder::BuildExternalPrompt(const FString& UserRequest, const FString& BlueprintContext, EABGBlueprintKind Kind, const FABGCreateRequest* Create, const FString& ProjectStructure)
{
	const bool bMaterial = ABGBlueprintKind::IsMaterialKind(Kind);
	FString Header = TEXT(
		"You generate Unreal Engine Blueprint node graphs. An editor plugin will import your answer into a Blueprint.\n"
		"Reply in two parts:\n"
		"1. A short plain-text explanation (2-6 sentences, in the language of my request) of what the graph does. "
		"If the request, or a part of it, cannot be done with the node types below, say so plainly here and give the reason, "
		"and also list it in the \"unsupported\" array of the JSON. Never invent functions or pins to work around it.\n"
		"2. The graph as ONE JSON object inside a single ```json code block. This must be the last code block of your reply; "
		"the plugin reads only that block, so put nothing but valid JSON inside it.\n\n");
	if (bMaterial)
	{
		Header = Header.Replace(TEXT("Blueprint node graphs. An editor plugin will import your answer into a Blueprint."),
			TEXT("material graphs. An editor plugin will import your answer into a material."));
		Header = Header.Replace(TEXT("node types below"), TEXT("expressions below"));
	}
	if (Create && Create->Kind != EABGCreateKind::None)
	{
		Header = Header.Replace(TEXT("will import your answer into a Blueprint."), TEXT("will create a NEW Blueprint asset and import your answer into it."));
		Header = Header.Replace(TEXT("will import your answer into a material."),
			Create->Kind == EABGCreateKind::MaterialFunction
				? TEXT("will create a NEW material function asset and import your answer into it.")
				: TEXT("will create a NEW material asset and import your answer into it."));
	}


	const UABGSettings* Settings = UABGSettings::Get();
	const bool bExamples = !Settings || Settings->bIncludeExamples;

	// Notes for this kind of Blueprint + the user's own instructions from the settings.
	FString Notes = FString::Printf(TEXT("\n=== NOTES FOR THIS BLUEPRINT (%s) ===\n"), *ABGBlueprintKind::Label(Kind));
	Notes += ABGBlueprintKind::BuiltInNotes(Kind) + TEXT("\n");
	const FString KindInstructions = ABGBlueprintKind::UserInstructions(Kind).TrimStartAndEnd();
	if (!KindInstructions.IsEmpty())
	{
		Notes += KindInstructions + TEXT("\n");
	}
	if (Settings && !Settings->GlobalInstructions.TrimStartAndEnd().IsEmpty())
	{
		Notes += TEXT("\n=== ADDITIONAL INSTRUCTIONS ===\n") + Settings->GlobalInstructions.TrimStartAndEnd() + TEXT("\n");
	}

	// "Create a new asset" block: the AI fills the "asset" key, the user's choices are repeated here.
	FString CreateBlock;
	if (Create && Create->Kind != EABGCreateKind::None)
	{
		const bool bBlueprintKind = !ABGCreateKind::IsMaterial(Create->Kind);
		CreateBlock += TEXT("\n=== NEW ASSET ===\n");
		CreateBlock += TEXT("The asset does not exist yet: the plugin creates it, then builds your answer inside it. ");
		CreateBlock += TEXT("Add the top-level key \"asset\" to the JSON:\n");
		CreateBlock += FString::Printf(TEXT("\"asset\": { \"type\": \"%s\", \"name\": \"<asset name>\", \"path\": \"/Game/<folder>\"%s }\n"),
			ABGCreateKind::TypeName(Create->Kind), bBlueprintKind ? TEXT(", \"parentClass\": \"<parent class>\"") : TEXT(""));
		CreateBlock += TEXT("- name: letters, digits and underscores only (for example ");
		CreateBlock += Create->Kind == EABGCreateKind::Material ? TEXT("M_Brick") : Create->Kind == EABGCreateKind::MaterialFunction ? TEXT("MF_Fresnel") : Create->Kind == EABGCreateKind::WidgetBlueprint ? TEXT("WBP_MainMenu") : TEXT("BP_Door");
		CreateBlock += TEXT("). path: a project folder starting with /Game.\n");
		if (bBlueprintKind)
		{
			CreateBlock += Create->Kind == EABGCreateKind::WidgetBlueprint
				? TEXT("- parentClass: UserWidget, or another UserWidget-based class.\n")
				: TEXT("- parentClass: the native or Blueprint class the new Blueprint derives from: Actor, Pawn, Character, ActorComponent, SceneComponent, GameModeBase, PlayerController, ... Pick the one that fits the request.\n");
			CreateBlock += TEXT("- The new Blueprint is EMPTY: it has no variables, no components of its own and no logic. Declare every variable you use in \"variables\" and create every event you need with an Event node.\n");
		}
		else
		{
			CreateBlock += TEXT("- The new material is EMPTY. Build everything: all expressions, connections and settings.\n");
		}
		const FString Name = Create->Name.TrimStartAndEnd();
		const FString Folder = Create->Folder.TrimStartAndEnd();
		const FString Parent = Create->ParentClass.TrimStartAndEnd();
		CreateBlock += TEXT("The user has already chosen: ");
		CreateBlock += Name.IsEmpty() ? TEXT("name = (not chosen: pick a good one)") : *FString::Printf(TEXT("name = \"%s\""), *Name);
		CreateBlock += Folder.IsEmpty() ? TEXT(", path = (not chosen: use /Game/AI)") : *FString::Printf(TEXT(", path = \"%s\""), *Folder);
		if (bBlueprintKind)
		{
			CreateBlock += Parent.IsEmpty() ? TEXT(", parentClass = (not chosen: pick a fitting one)") : *FString::Printf(TEXT(", parentClass = \"%s\""), *Parent);
		}
		CreateBlock += TEXT(". Repeat chosen values exactly.\n");

		if (Settings && !Settings->NewAssetInstructions.TrimStartAndEnd().IsEmpty())
		{
			CreateBlock += TEXT("\n") + Settings->NewAssetInstructions.TrimStartAndEnd() + TEXT("\n");
		}
	}

	FString Context;
	if (!BlueprintContext.TrimStartAndEnd().IsEmpty())
	{
		Context = (bMaterial
			? TEXT("\n=== MY MATERIAL (current state; expression ids like e_Multiply_0 can be used as \"Existing\" nodes) ===\n")
			: TEXT("\n=== MY BLUEPRINT (current state; node ids like e_xxxxxxxx can be used as \"Existing\" nodes) ===\n"))
			+ BlueprintContext.TrimStartAndEnd() + TEXT("\n");
	}

	FString Project;
	if (!ProjectStructure.TrimStartAndEnd().IsEmpty())
	{
		Project = TEXT("\n=== PROJECT CONTENT (assets that already exist in my project) ===\n")
			TEXT("Use these exact paths when you refer to existing assets (textures, meshes, materials, Blueprints, classes). ")
			TEXT("The list can be filtered or shortened (the header says so): do not invent assets that are not listed, ")
			TEXT("and never give a NEW asset a name that is already listed in the same folder.\n")
			+ ProjectStructure.TrimStartAndEnd() + TEXT("\n");
	}

	return Header + (bMaterial ? BuildMaterialFormatSpec(bExamples) : BuildFormatSpec(bExamples)) + Notes + CreateBlock + Context + Project
		+ TEXT("\n=== MY REQUEST ===\n") + UserRequest.TrimStartAndEnd() + TEXT("\n");
}

FString FABGPromptBuilder::BuildFixPrompt(const FString& UserRequest, const FString& RejectedJson,
	const TArray<FString>& Errors, const TArray<FString>& Warnings, const FString& BlueprintContext, EABGBlueprintKind Kind,
	const FABGCreateRequest* Create, const FString& ProjectStructure)
{
	FString Result = BuildExternalPrompt(UserRequest.IsEmpty() ? FString(TEXT("(not provided)")) : UserRequest, BlueprintContext, Kind, Create, ProjectStructure);

	Result += TEXT("\n=== YOUR PREVIOUS ANSWER (the plugin could not use it) ===\n```json\n");
	Result += RejectedJson.TrimStartAndEnd();
	Result += TEXT("\n```\n");

	Result += TEXT("\n=== ERRORS REPORTED BY THE PLUGIN ===\n");
	for (const FString& Error : Errors)
	{
		Result += TEXT("- ") + Error + TEXT("\n");
	}
	if (Warnings.Num() > 0)
	{
		Result += TEXT("\n=== WARNINGS ===\n");
		for (const FString& Warning : Warnings)
		{
			Result += TEXT("- ") + Warning + TEXT("\n");
		}
	}

	Result += TEXT(
		"\nFix the graph so these errors disappear and answer in the same two-part format "
		"(short explanation, then the complete corrected JSON in one ```json block). "
		"Only use node types, function names and pin names that really exist. "
		"If my request cannot be built with the node types described above, say so in the explanation "
		"and give the closest graph that does work.\n");
	return Result;
}
