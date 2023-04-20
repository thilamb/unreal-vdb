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

#include "VdbAssetComponent.h"

#include "VdbCommon.h"
#include "VdbVolumeBase.h"
#include "VdbVolumeAsset.h"

#define LOCTEXT_NAMESPACE "VdbAssetComponent"

UVdbAssetComponent::UVdbAssetComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}
UVdbAssetComponent::~UVdbAssetComponent() {}

TArray<const UVdbVolumeBase*> UVdbAssetComponent::GetConstVolumes() const
{ 
	TArray<const UVdbVolumeBase*> Array;
	if (VdbAsset)
	{
		for (auto& Grid : VdbAsset->VdbVolumes)
		{
			Array.Add(Grid);
		}
	}
	return Array;
}

TArray<UVdbVolumeBase*> UVdbAssetComponent::GetVolumes()
{
	TArray<UVdbVolumeBase*> Array;
	if (VdbAsset)
	{
		for (auto& Grid : VdbAsset->VdbVolumes)
		{
			Array.Add(Grid);
		}
	}
	return Array;
}

const FVolumeRenderInfos* UVdbAssetComponent::GetRenderInfos(const UVdbVolumeBase* VdbVolume) const
{
	if (VdbVolume != nullptr)
	{
		bool ValidSequence = VdbVolume->IsSequence();
		if (ValidSequence)
		{
			return VdbVolume->GetRenderInfos(CurrFrameIndex);
		}
		else
		{
			return VdbVolume->GetRenderInfos(0);
		}
	}
	else
	{
		return nullptr;
	}
}

EVdbClass UVdbAssetComponent::GetVdbClass() const
{
	if (VdbAsset && !VdbAsset->VdbVolumes.IsEmpty())
	{
		return VdbAsset->VdbVolumes[0]->GetVdbClass();
	}
	else
	{
		return EVdbClass::Undefined;
	}
}

void UVdbAssetComponent::BroadcastFrameChanged(uint32 Frame, bool Force)
{
	if (Force || (CurrFrameIndex != Frame))
	{
		CurrFrameIndex = Frame;
		TargetFrameIndex = CurrFrameIndex;
		OnFrameChanged.Broadcast(Frame);
	}
}

void UVdbAssetComponent::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	for (auto& Grid : VdbAsset->VdbVolumes)
	{
		Objects.Add(Grid);
	}
}

FVector3f UVdbAssetComponent::GetVolumeSize() const
{
	if (VdbAsset && !VdbAsset->VdbVolumes.IsEmpty())
	{
		return FVector3f(VdbAsset->VdbVolumes[0]->GetBounds(TargetFrameIndex).GetSize());
	}
	return FVector3f::OneVector;
}

FVector3f UVdbAssetComponent::GetVolumeOffset() const
{
	if (VdbAsset && !VdbAsset->VdbVolumes.IsEmpty())
	{
		return FVector3f(VdbAsset->VdbVolumes[0]->GetBounds(TargetFrameIndex).Min);
	}
	return FVector3f::ZeroVector;
}

FVector3f UVdbAssetComponent::GetVolumeUvScale() const
{
	if (VdbAsset && !VdbAsset->VdbVolumes.IsEmpty())
	{
		UVdbVolumeBase* VdbVolume = VdbAsset->VdbVolumes[0];
		const FIntVector& LargestVolume = VdbVolume->GetLargestVolume();
		const FVector3f& VolumeSize = VdbVolume->GetRenderInfos(TargetFrameIndex)->GetIndexSize();

		return FVector3f(VolumeSize.X / (float)LargestVolume.X,
			VolumeSize.Y / (float)LargestVolume.Y,
			VolumeSize.Z / (float)LargestVolume.Z);
	}
	return FVector3f::OneVector;
}

UVdbVolumeBase* UVdbAssetComponent::GetDensityVolume()
{
	if (VdbAsset && DensityGridIndex >= 0 && DensityGridIndex < VdbAsset->VdbVolumes.Num())
	{
		UVdbVolumeBase* Volume = VdbAsset->VdbVolumes[DensityGridIndex];
		return !Volume->IsVectorGrid() ? Volume : nullptr;
	}
	return nullptr;
}
const UVdbVolumeBase* UVdbAssetComponent::GetDensityVolume() const
{
	if (VdbAsset && DensityGridIndex >= 0 && DensityGridIndex < VdbAsset->VdbVolumes.Num())
	{
		UVdbVolumeBase* Volume = VdbAsset->VdbVolumes[DensityGridIndex];
		return !Volume->IsVectorGrid() ? Volume : nullptr;
	}
	return nullptr;
}

UVdbVolumeBase* UVdbAssetComponent::GetTemperatureVolume()
{
	if (VdbAsset && TemperatureGridIndex >= 0 && TemperatureGridIndex < VdbAsset->VdbVolumes.Num())
	{
		UVdbVolumeBase* Volume = VdbAsset->VdbVolumes[TemperatureGridIndex];
		return !Volume->IsVectorGrid() ? Volume : nullptr;
	}
	return nullptr;
}
const UVdbVolumeBase* UVdbAssetComponent::GetTemperatureVolume() const
{
	if (VdbAsset && TemperatureGridIndex >= 0 && TemperatureGridIndex < VdbAsset->VdbVolumes.Num())
	{
		UVdbVolumeBase* Volume = VdbAsset->VdbVolumes[TemperatureGridIndex];
		return !Volume->IsVectorGrid() ? Volume : nullptr;
	}
	return nullptr;
}

UVdbVolumeBase* UVdbAssetComponent::GetColorVolume()
{
	if (VdbAsset && ColorGridIndex >= 0 && ColorGridIndex < VdbAsset->VdbVolumes.Num())
	{
		UVdbVolumeBase* Volume = VdbAsset->VdbVolumes[ColorGridIndex];
		return Volume->IsVectorGrid() ? Volume : nullptr;
	}
	return nullptr;
}
const UVdbVolumeBase* UVdbAssetComponent::GetColorVolume() const
{
	if (VdbAsset && ColorGridIndex >= 0 && ColorGridIndex < VdbAsset->VdbVolumes.Num())
	{
		UVdbVolumeBase* Volume = VdbAsset->VdbVolumes[ColorGridIndex];
		return Volume->IsVectorGrid() ? Volume : nullptr;
	}
	return nullptr;
}

#if WITH_EDITOR
void UVdbAssetComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetName() == TEXT("VdbAsset"))
	{
		// trigger details customization refresh
		OnVdbChanged.ExecuteIfBound();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Not exactly sure why but overriding property with DetailsCustomization prevents a parent Actor  
	// PostEditChangeProperty, which is necessary to force a visual refresh of the VDB volume. Do it manually
	GetOwner()->PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE
