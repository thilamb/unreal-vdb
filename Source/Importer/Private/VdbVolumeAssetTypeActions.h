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

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "AssetTypeActions_Base.h"

class UVdbVolumeAsset;
class UVdbVolumeBase;

class FVdbVolumeAssetTypeActions : public FAssetTypeActions_Base
{
public:
	FVdbVolumeAssetTypeActions(EAssetTypeCategories::Type InAssetCategory);

	// IAssetTypeActions interface
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;	
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return true; }
	virtual void GetActions(const TArray<UObject*>& InObjects, struct FToolMenuSection& Section) override;
	virtual bool IsImportedAsset() const { return true; }
	virtual uint32 GetCategories() override;
	virtual void GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const override;
	// End of IAssetTypeActions interface

private:

	void ToVolumeSubMenu(UToolMenu* Menu, TArray<TWeakObjectPtr<UVdbVolumeAsset>> VdbVolumes);
	void ExecuteConvertToVolume(UVdbVolumeBase* VdbVolume, bool IsSequence);
	void FrameRateSubMenu(UToolMenu* Menu, TArray<TWeakObjectPtr<UVdbVolumeAsset>> VdbVolumes);
	void ExecuteChangeFrameRate(TArray<TWeakObjectPtr<UVdbVolumeAsset>> VdbVolumes);

	EAssetTypeCategories::Type MyAssetCategory;

	uint32 CurrentFrame = 0;
	float ModifiedFrameRate = 30.0;
};
