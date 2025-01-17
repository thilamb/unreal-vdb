// Copyright Thibault Lambert

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http ://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "VdbVolumeAsset.h"
#include "VdbVolumeBase.h"

#include "EditorFramework\AssetImportData.h"

UVdbVolumeAsset::UVdbVolumeAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UVdbVolumeBase* UVdbVolumeAsset::GetVdbVolume(int32 Index)
{
	if (Index < VdbVolumes.Num())
	{
		return VdbVolumes[Index];
	}

	return nullptr;
}

void UVdbVolumeAsset::PostInitProperties()
{
	Super::PostInitProperties();

#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
}

void UVdbVolumeAsset::PostLoad()
{
	Super::PostLoad();
	IsSequence = VdbVolumes.IsEmpty() ? false : VdbVolumes[0]->IsSequence();
}

#if WITH_EDITORONLY_DATA
void UVdbVolumeAsset::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	if (AssetImportData)
	{
		Context.AddTag(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}

	Super::GetAssetRegistryTags(Context);
}
#endif
