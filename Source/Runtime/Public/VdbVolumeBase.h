// Copyright 2022 Eidos-Montreal / Eidos-Sherbrooke

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
#include "UObject/NoExportTypes.h"
#include "Misc/Paths.h"
#include "VdbCommon.h"

#include "VdbVolumeBase.generated.h"

class FVdbRenderBuffer;
class FVolumeRenderInfos;
class UVdbVolumeAsset;

// Base interface for NanoVDB file containers
UCLASS(BlueprintType, hidecategories = (Object))
class VOLUMERUNTIME_API UVdbVolumeBase : public UObject
{
	GENERATED_UCLASS_BODY()

	static FBox ZeroBox;

public:

	UVdbVolumeBase() = default;
	virtual ~UVdbVolumeBase() = default;

	UFUNCTION(BlueprintCallable, Category = Volume)
	bool IsSequence() const { return IsVolSequence; }

	UFUNCTION(BlueprintCallable, Category = Volume)
	bool IsVectorGrid() const { return IsVolVector; }

	EVdbClass GetVdbClass() const { return VdbClass; };
	const FBox& GetGlobalBounds() const { return Bounds; }
	const FIntVector& GetLargestVolume() const { return LargestVolume; }
	// We only support Volume with cubic voxels (same dimension in all axes)
	float GetVoxelSize() const { return VoxelSize.X; }
	int GetMemorySize() const { return MemoryUsage; }
	FString GetType() const;
	float GetFrameRate() const;

	void UpdateFromMetadata(const nanovdb::GridMetaData* MetaData);

	virtual bool IsValid() const PURE_VIRTUAL(UVdbVolumeBase::IsValid, return false;);
	virtual const FBox& GetBounds(uint32 FrameIndex) const PURE_VIRTUAL(UVdbVolumeBase::GetBounds, return ZeroBox;);
	virtual const FIntVector& GetIndexMin(uint32 FrameIndex) const PURE_VIRTUAL(UVdbVolumeBase::GetIndexMin, return FIntVector::ZeroValue;);
	virtual const FIntVector& GetIndexMax(uint32 FrameIndex) const PURE_VIRTUAL(UVdbVolumeBase::GetIndexMax, return FIntVector::ZeroValue;);
	virtual const FMatrix44f& GetIndexToLocal(uint32 FrameIndex) const PURE_VIRTUAL(UVdbVolumeBase::GetIndexToLocal, return FMatrix44f::Identity;);

	virtual const FVolumeRenderInfos* GetRenderInfos(uint32 FrameIndex) const PURE_VIRTUAL(UVdbVolumeBase::GetRenderInfos, return nullptr;);
	virtual const uint8* GetGridData(uint32 FrameIndex) const PURE_VIRTUAL(UVdbVolumeBase::GetGridData, return nullptr;);
	virtual const nanovdb::GridMetaData* GetMetaData(uint32 FrameIndex) PURE_VIRTUAL(UVdbVolumeBase::GetGridData, return nullptr;);

#if WITH_EDITOR
	virtual void ForceStreaming(uint32 FrameIndex) {}
#endif

	// UObject Interface.
	virtual void PostInitProperties() override;

	UVdbVolumeAsset* GetParentAsset() { return ParentAsset; }

#if WITH_EDITORONLY_DATA
	FString GetGridName() const { return GridName; }
#endif

protected:

	UPROPERTY(VisibleAnywhere, Category = "Properties")
	EVdbClass VdbClass;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category = "Properties")
	FString GridName;

	UPROPERTY(VisibleAnywhere, Category = "Properties")
	FString Class;

	UPROPERTY(VisibleAnywhere, Category = "Properties")
	FString DataType;

	UPROPERTY(VisibleAnywhere, Category = "Properties", Meta = (DisplayName = "Total Memory"))
	FString MemoryUsageStr;

	UPROPERTY(VisibleAnywhere, Category = "Details")
	float MinValue;

	UPROPERTY(VisibleAnywhere, Category = "Details")
	float MaxValue;
#endif

	UPROPERTY(VisibleAnywhere, Category = "Details")
	FBox Bounds;

	UPROPERTY(VisibleAnywhere, Category = "Details")
	FIntVector LargestVolume;

	UPROPERTY(VisibleAnywhere, Category = "Details")
	FVector3f VoxelSize;

	UPROPERTY()
	uint64 MemoryUsage = 0;

	UPROPERTY(VisibleAnywhere, Category = "Properties")
	EQuantizationType Quantization = EQuantizationType::None;

	UPROPERTY()
	bool IsVolVector = false;

	UPROPERTY(Transient)
	bool IsVolSequence = false;

	UVdbVolumeAsset* ParentAsset = nullptr;
};



