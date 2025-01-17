// Copyright Thibault Lambert

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "VdbVolumeAsset.generated.h"

class UVdbVolumeBase;

UCLASS()
class VOLUMERUNTIME_API UVdbVolumeAsset : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	UVdbVolumeAsset() = default;
	virtual ~UVdbVolumeAsset() = default;

	UVdbVolumeBase* GetVdbVolume(int32 Index);

	UPROPERTY(VisibleAnywhere, Category = VdbGrids)
	TArray<UVdbVolumeBase*> VdbVolumes;

	UPROPERTY(VisibleAnywhere ,Transient, Category = "Sequence")
	bool IsSequence = false;

	UPROPERTY(VisibleAnywhere, Category = "Sequence", meta=(EditCondition = "IsSequence", EditConditionHides))
	float FrameRate = 30.0;

	float GetFrameRate() const { return FrameRate; }
	void ChangeFrameRate(float Fps) { FrameRate = Fps; }

	// UObject Interface.
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

#if WITH_EDITORONLY_DATA
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	class UAssetImportData* GetAssetImportData() { return AssetImportData; }

	UPROPERTY(BlueprintReadOnly, Instanced, Category = ImportSettings)
	TObjectPtr<class UAssetImportData> AssetImportData;
#endif
};
