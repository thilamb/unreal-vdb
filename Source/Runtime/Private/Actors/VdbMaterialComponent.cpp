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

#include "VdbMaterialComponent.h"

#include "VdbCommon.h"
#include "VdbVolumeBase.h"
#include "VdbVolumeSequence.h"
#include "VdbAssetComponent.h"
#include "Rendering/VdbVolumeSceneProxy.h"

#include "Curves/CurveLinearColorAtlas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "VdbMaterialComponent"

UVdbMaterialComponent::UVdbMaterialComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(TEXT("/SparseVolumetrics/Materials/M_VDB_Lit_Inst"));
	Material = DefaultMaterial.Object;

	static ConstructorHelpers::FObjectFinder<UTextureRenderTarget2D> DefaultRenderTarget(TEXT("TextureRenderTarget2D'/SparseVolumetrics/Misc/RT_VdbMatRenderTarget.RT_VdbMatRenderTarget'"));
	RenderTarget = DefaultRenderTarget.Object;
}

UVdbMaterialComponent::~UVdbMaterialComponent() {}

void UVdbMaterialComponent::SetVdbAssets(UVdbAssetComponent* Comp) 
{
	VdbAssets = Comp;
	Comp->OnFrameChanged.AddUObject(this, &UVdbMaterialComponent::UpdateSceneProxy);
	Comp->OnSubFrameChanged.AddUObject(this, &UVdbMaterialComponent::UpdateSubFrame);
}

void UVdbMaterialComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (Material != nullptr)
	{
		OutMaterials.Add(Material);
	}
}

FPrimitiveSceneProxy* UVdbMaterialComponent::CreateSceneProxy()
{
	const UVdbVolumeBase* MainVolume = VdbAssets->GetMainVolume();
	if (!MainVolume || !MainVolume->IsValid() || MainVolume->IsVectorGrid() )
		return nullptr;

	UMaterialInterface* VdbMaterial = GetMaterial(0);
	if (!VdbMaterial || VdbMaterial->GetMaterial()->MaterialDomain != EMaterialDomain::MD_Volume)
	{
		UE_LOG(LogSparseVolumetrics, Warning, TEXT("VDB %s needs a Volumetric Material."), *GetName());
		return nullptr;
	}

	return new FVdbVolumeSceneProxy(VdbAssets, this);
}

FBoxSphereBounds UVdbMaterialComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const UVdbVolumeBase* MainVolume = VdbAssets->GetMainVolume();
	if (MainVolume != nullptr)
	{
		FBoxSphereBounds VdbBounds(MainVolume->GetGlobalBounds());
		return VdbBounds.TransformBy(LocalToWorld);
	}
	else
	{
		return Super::CalcBounds(LocalToWorld);
	}
}

void UVdbMaterialComponent::UpdateSceneProxy(uint32 FrameIndex)
{
	FVdbVolumeSceneProxy* VdbMaterialSceneProxy = static_cast<FVdbVolumeSceneProxy*>(SceneProxy);
	if (VdbMaterialSceneProxy == nullptr)
	{
		return;
	}

	UVdbVolumeSequence* DensitySequence = Cast<UVdbVolumeSequence>(VdbAssets->GetDensityVolume());
	const FVolumeRenderInfos* RenderInfosDensity = DensitySequence ? DensitySequence->GetRenderInfos(FrameIndex) : nullptr;

	UVdbVolumeSequence* TemperatureSequence = Cast<UVdbVolumeSequence>(VdbAssets->GetTemperatureVolume());
	const FVolumeRenderInfos* RenderInfosTemperature = TemperatureSequence ? TemperatureSequence->GetRenderInfos(FrameIndex) : nullptr;

	UVdbVolumeSequence* VelocitySequence = Cast<UVdbVolumeSequence>(VdbAssets->GetVelocityVolume());
	const FVolumeRenderInfos* RenderInfosVelocity = VelocitySequence ? VelocitySequence->GetRenderInfos(FrameIndex) : nullptr;

	UVdbVolumeSequence* ColorSequence = Cast<UVdbVolumeSequence>(VdbAssets->GetColorVolume());
	const FVolumeRenderInfos* RenderInfosColor = ColorSequence ? ColorSequence->GetRenderInfos(FrameIndex) : nullptr;

	const FVolumeRenderInfos* MainRenderInfosDensity = RenderInfosDensity ? RenderInfosDensity : RenderInfosTemperature;
	if (MainRenderInfosDensity)
	{
		ENQUEUE_RENDER_COMMAND(UploadVdbGpuData)(
			[this,
			VdbMaterialSceneProxy,
			IndexMin = MainRenderInfosDensity->GetIndexMin(),
			IndexSize = MainRenderInfosDensity->GetIndexSize(),
			IndexToLocal = MainRenderInfosDensity->GetIndexToLocal(),
			DensRenderBuffer = MainRenderInfosDensity->GetRenderResource(),
			TempRenderBuffer = RenderInfosTemperature ? RenderInfosTemperature->GetRenderResource() : nullptr,
			VelRenderBuffer = RenderInfosVelocity ? RenderInfosVelocity->GetRenderResource() : nullptr,
			ColorRenderBuffer = RenderInfosColor ? RenderInfosColor->GetRenderResource() : nullptr]
		(FRHICommandList& RHICmdList)
		{
			VdbMaterialSceneProxy->Update(IndexToLocal, IndexMin, IndexSize, DensRenderBuffer, TempRenderBuffer, VelRenderBuffer, ColorRenderBuffer);
		});
	}
}

void UVdbMaterialComponent::UpdateSubFrame(float Value)
{
	FVdbVolumeSceneProxy* VdbMaterialSceneProxy = static_cast<FVdbVolumeSceneProxy*>(SceneProxy);
	if (VdbMaterialSceneProxy != nullptr)
	{
		ENQUEUE_RENDER_COMMAND(VdbInterFrame)(
			[this,
			VdbMaterialSceneProxy,
			Value]
			(FRHICommandList& RHICmdList)
			{
				VdbMaterialSceneProxy->UpdateSubFrameValue(Value);
			});
	}
}

int32 UVdbMaterialComponent::GetNumMaterials() const 
{ 
	return (Material != nullptr) ? 1 : 0; 
}

void UVdbMaterialComponent::SetMaterial(int32 ElementIndex, class UMaterialInterface* InMaterial)
{
	if (InMaterial != Material)
	{
		Material = InMaterial;
		MarkRenderStateDirty();
	}
}

template<typename T>
void UVdbMaterialComponent::SetAttribute(T& Attribute, const T& NewValue)
{
	if (AreDynamicDataChangesAllowed() && Attribute != NewValue)
	{
		Attribute = NewValue;
		MarkRenderStateDirty();
	}
}
template void UVdbMaterialComponent::SetAttribute<float>(float& Attribute, const float& NewValue);

#if WITH_EDITOR

void UVdbMaterialComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property
		&& (PropertyChangedEvent.Property->GetName() == TEXT("BlackBodyCurveAtlas") || 
			PropertyChangedEvent.Property->GetName() == TEXT("BlackBodyCurve")))
	{
		if (!BlackBodyCurveAtlas)
		{
			// Need a Curve Atlas before selecting a Curve
			BlackBodyCurve = nullptr;
		}
		else
		{
			int32 CurveIndex = INDEX_NONE;
			if (!BlackBodyCurveAtlas->GetCurveIndex(BlackBodyCurve, CurveIndex))
			{
				// Selected Curve should be part of the selected Curve Atlas. Otherwise reset.
				BlackBodyCurve = nullptr;
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

#undef LOCTEXT_NAMESPACE