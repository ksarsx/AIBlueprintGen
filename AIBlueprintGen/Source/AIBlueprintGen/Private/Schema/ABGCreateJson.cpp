#include "Schema/ABGCreateJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FABGCreateJson::IsValidAssetName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Len() > 100 || FChar::IsDigit(Name[0]))
	{
		return false;
	}
	for (const TCHAR Ch : Name)
	{
		if (!(FChar::IsAlnum(Ch) || Ch == TEXT('_')))
		{
			return false;
		}
	}
	return true;
}

bool FABGCreateJson::Parse(const FString& InJson, FABGCreateSpec& OutSpec, TArray<FString>& OutErrors)
{
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutErrors.Add(TEXT("Invalid JSON (could not parse). Check commas, quotes and brackets."));
		return false;
	}

	const int32 ErrorsBefore = OutErrors.Num();

	const TSharedPtr<FJsonObject>* AssetObj = nullptr;
	if (Root->TryGetObjectField(TEXT("asset"), AssetObj) && AssetObj)
	{
		FABGAssetSpec& Asset = OutSpec.Asset;
		Asset.bPresent = true;
		(*AssetObj)->TryGetStringField(TEXT("type"), Asset.Type);
		(*AssetObj)->TryGetStringField(TEXT("name"), Asset.Name);
		(*AssetObj)->TryGetStringField(TEXT("path"), Asset.Path);
		if (!(*AssetObj)->TryGetStringField(TEXT("parentClass"), Asset.ParentClass))
		{
			(*AssetObj)->TryGetStringField(TEXT("parent"), Asset.ParentClass);
		}
		Asset.Type = Asset.Type.TrimStartAndEnd();
		Asset.Name = Asset.Name.TrimStartAndEnd();
		Asset.Path = Asset.Path.TrimStartAndEnd();
		Asset.ParentClass = Asset.ParentClass.TrimStartAndEnd();

		if (!Asset.Name.IsEmpty() && !IsValidAssetName(Asset.Name))
		{
			OutErrors.Add(FString::Printf(TEXT("asset.name \"%s\" is not a valid asset name: use letters, digits and underscores only, not starting with a digit."), *Asset.Name));
		}
		if (!Asset.Path.IsEmpty() && (!Asset.Path.StartsWith(TEXT("/Game")) || Asset.Path.Contains(TEXT(".."))))
		{
			OutErrors.Add(FString::Printf(TEXT("asset.path \"%s\" must be a project folder starting with /Game, e.g. /Game/AI."), *Asset.Path));
		}
	}
	else if (Root->HasField(TEXT("asset")))
	{
		OutErrors.Add(TEXT("\"asset\" must be an object: { \"type\": ..., \"name\": ..., \"path\": ... }."));
	}

	const TArray<TSharedPtr<FJsonValue>>* TexArr = nullptr;
	if (Root->TryGetArrayField(TEXT("textures"), TexArr) && TexArr)
	{
		TSet<FString> Names;
		for (int32 i = 0; i < TexArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* TexObj = nullptr;
			if (!(*TexArr)[i].IsValid() || !(*TexArr)[i]->TryGetObject(TexObj) || !TexObj)
			{
				OutErrors.Add(FString::Printf(TEXT("textures[%d] is not an object."), i));
				continue;
			}
			FABGTextureSpec Tex;
			(*TexObj)->TryGetStringField(TEXT("name"), Tex.Name);
			(*TexObj)->TryGetStringField(TEXT("file"), Tex.File);
			(*TexObj)->TryGetStringField(TEXT("usage"), Tex.Usage);
			Tex.Name = Tex.Name.TrimStartAndEnd();
			Tex.File = Tex.File.TrimStartAndEnd();
			Tex.Usage = Tex.Usage.TrimStartAndEnd();

			if (!IsValidAssetName(Tex.Name))
			{
				OutErrors.Add(FString::Printf(TEXT("textures[%d]: \"name\" must be a valid asset name (letters, digits, underscores)."), i));
				continue;
			}
			if (Names.Contains(Tex.Name))
			{
				OutErrors.Add(FString::Printf(TEXT("textures[%d]: duplicate texture name \"%s\"."), i, *Tex.Name));
				continue;
			}
			Names.Add(Tex.Name);
			if (Tex.File.IsEmpty())
			{
				OutErrors.Add(FString::Printf(TEXT("textures[%d] (%s): \"file\" (path of the image file) is missing."), i, *Tex.Name));
				continue;
			}
			if (!Tex.Usage.IsEmpty()
				&& !Tex.Usage.Equals(TEXT("Color"), ESearchCase::IgnoreCase)
				&& !Tex.Usage.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)
				&& !Tex.Usage.Equals(TEXT("Mask"), ESearchCase::IgnoreCase)
				&& !Tex.Usage.Equals(TEXT("Data"), ESearchCase::IgnoreCase))
			{
				OutErrors.Add(FString::Printf(TEXT("textures[%d] (%s): \"usage\" must be Color, Normal or Mask."), i, *Tex.Name));
				continue;
			}
			OutSpec.Textures.Add(MoveTemp(Tex));
		}
	}
	else if (Root->HasField(TEXT("textures")))
	{
		OutErrors.Add(TEXT("\"textures\" must be an array of { \"name\", \"file\", \"usage\" } objects."));
	}

	return OutErrors.Num() == ErrorsBefore;
}
