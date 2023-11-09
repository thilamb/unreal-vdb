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

#include "VdbVolumeSceneProxy.h"
#include "VdbAssetComponent.h"
#include "VdbMaterialComponent.h"
#include "VdbVolumeStatic.h"
#include "VdbCommon.h"
#include "VolumeRuntimeModule.h"
#include "VdbSequenceComponent.h"
#include "VdbVolumeSequence.h"
#include "Rendering/VolumeMesh.h"
#include "Rendering/VdbVolumeRendering.h"
#include "Materials/Material.h"
#include "Algo/AnyOf.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "TextureResource.h"

FVdbVolumeSceneProxy::FVdbVolumeSceneProxy(const UVdbAssetComponent* AssetComponent, const UVdbMaterialComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
	, VdbMaterialComponent(InComponent)
	, Material(InComponent->GetMaterial(0))
	, MaterialRelevance(Material->GetRelevance_Concurrent(GetScene().GetFeatureLevel()))
{
	LevelSet = AssetComponent->GetVdbClass() == EVdbClass::SignedDistance;
	TranslucentLevelSet = LevelSet && InComponent->TranslucentLevelSet;
	ImprovedEnvLight = InComponent->ImprovedEnvLight;
	TrilinearSampling = InComponent->TrilinearSampling;
	CastShadows = InComponent->CastShadow;
	RenderAfterTransparents = InComponent->RenderAfterTransparents;

	VdbMaterialRenderExtension = FVolumeRuntimeModule::GetRenderExtension(InComponent->RenderTarget);

	TemperatureOnly = !AssetComponent->GetDensityVolume() && AssetComponent->GetTemperatureVolume();
	const UVdbVolumeBase* MainVolume = AssetComponent->GetMainVolume();

	const FVolumeRenderInfos* PrimaryRenderInfos = AssetComponent->GetRenderInfos(MainVolume);
	DensityRenderBuffer = PrimaryRenderInfos ? PrimaryRenderInfos->GetRenderResource() : nullptr;

	IndexMin = PrimaryRenderInfos->GetIndexMin();
	IndexSize = PrimaryRenderInfos->GetIndexSize();
	IndexToLocal = PrimaryRenderInfos->GetIndexToLocal();
	IndexToLocalDeterminantNegative = IndexToLocal.Determinant() < 0.0f;

	SliceMinData = FVector4f(FMath::Max(0.0, InComponent->SliceMin.X), FMath::Max(0.0, InComponent->SliceMin.Y), FMath::Max(0.0, InComponent->SliceMin.Z));
	SliceMinData = FVector4f(FMath::Min(1.0, SliceMinData.X), FMath::Min(1.0, SliceMinData.Y), FMath::Min(1.0, SliceMinData.Z));
	SliceMaxData = FVector4f(FMath::Max(SliceMinData.X, InComponent->SliceMax.X), FMath::Max(SliceMinData.Y, InComponent->SliceMax.Y), FMath::Max(SliceMinData.Z, InComponent->SliceMax.Z));
	SliceMaxData = FVector4f(FMath::Min(1.0, SliceMaxData.X), FMath::Min(1.0, SliceMaxData.Y), FMath::Min(1.0, SliceMaxData.Z));

	CurveIndex = INDEX_NONE;
	CurveAtlas = InComponent->BlackBodyCurveAtlas;
	if (!InComponent->PhysicallyBasedBlackbody && InComponent->BlackBodyCurve && CurveAtlas)
	{
		CurveAtlas->GetCurveIndex(InComponent->BlackBodyCurve, CurveIndex);
	}
	CurveAtlasTex = CurveAtlas ? CurveAtlas->GetResource() : nullptr;
	uint32 AtlasHeight = CurveAtlas ? CurveAtlas->TextureHeight : 0;

	CustomIntDataOffline0 = CustomIntData0 = FIntVector4(InComponent->MaxRayDepth, InComponent->SamplesPerPixel, InComponent->ColoredTransmittance, InComponent->TemporalNoise);
	CustomIntDataOffline0.Y *= InComponent->OfflineSamplesPerPixelMultiplier;

	CustomIntData1 = FIntVector4(CurveIndex, int32(AtlasHeight), TranslucentLevelSet, TemperatureOnly);
	float VoxelSize = MainVolume->GetVoxelSize();
	CustomFloatDataOffline0 = CustomFloatData0 = FVector4f(InComponent->LocalStepSize, InComponent->ShadowStepSizeMultiplier, VoxelSize, InComponent->Jittering);
	CustomFloatDataOffline0.X /= InComponent->OfflineLocalStepMultiplier;
	CustomFloatDataOffline0.Y /= InComponent->OfflineShadowStepSizeMultiplier;

	CustomFloatData1 = FVector4f(InComponent->Anisotropy, InComponent->Albedo, InComponent->BlackbodyIntensity, (CurveIndex == INDEX_NONE) ? InComponent->BlackbodyTemperature : InComponent->TemperatureMultiplier);
	CustomFloatData2 = FVector4f(InComponent->DensityMultiplier, InComponent->VolumePadding, InComponent->Ambient, InComponent->VelocityMultiplier);

	auto FillValue = [AssetComponent](const UVdbVolumeBase* Base, FVdbRenderBuffer*& Buffer)
	{
		const FVolumeRenderInfos* RenderInfos = AssetComponent->GetRenderInfos(Base);
		Buffer = RenderInfos ? RenderInfos->GetRenderResource() : nullptr;
	};

	FillValue(AssetComponent->GetTemperatureVolume(), TemperatureRenderBuffer);
	FillValue(AssetComponent->GetVelocityVolume(), VelocityRenderBuffer);
	FillValue(AssetComponent->GetColorVolume(), ColorRenderBuffer);

	bCastDynamicShadow = true;
}

FMeshBatch* FVdbVolumeSceneProxy::CreateMeshBatch(const FSceneView* View, int32 ViewIndex, const FSceneViewFamily& ViewFamily, FMeshElementCollector& Collector, FVdbVolumeRendering* VolRendering, FVdbVertexFactoryUserDataWrapper& UserData, const FMaterialRenderProxy* MaterialProxy) const
{
	FVolumeMeshVertexBuffer* VertexBuffer = VolRendering->GetVertexBuffer();
	FVolumeMeshVertexFactory* VertexFactory = VolRendering->GetVertexFactory();

	FMeshBatch& MeshBatch = Collector.AllocateMesh();
	MeshBatch.bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

	const FPrimitiveViewRelevance& ViewRelevance = GetViewRelevance(View);

	MeshBatch.bUseWireframeSelectionColoring = IsSelected();
	MeshBatch.VertexFactory = VertexFactory;
	MeshBatch.MaterialRenderProxy = MaterialProxy;
	MeshBatch.ReverseCulling = IsLocalToWorldDeterminantNegative() ^ IsIndexToLocalDeterminantNegative();
	MeshBatch.Type = PT_TriangleList;
	MeshBatch.DepthPriorityGroup = SDPG_World;
	MeshBatch.bCanApplyViewModeOverrides = false;
	MeshBatch.bUseForMaterial = true;
	MeshBatch.CastShadow = ViewRelevance.bShadowRelevance;
	MeshBatch.bUseForDepthPass = false;

	FMeshBatchElement& BatchElement = MeshBatch.Elements[0];

	// Draw the mesh.
#if 0
	FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
	DynamicPrimitiveUniformBuffer.Set(GetLocalToWorld(), GetLocalToWorld(), GetBounds(), GetLocalBounds(), false, false, AlwaysHasVelocity());
	BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
	BatchElement.PrimitiveIdMode = PrimID_DynamicPrimitiveShaderData;
#else
	BatchElement.PrimitiveUniformBuffer = GetUniformBuffer();
#endif

	BatchElement.IndexBuffer = &VertexBuffer->IndexBuffer;
	BatchElement.FirstIndex = 0;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = VertexBuffer->NumVertices - 1;
	BatchElement.NumPrimitives = VertexBuffer->NumPrimitives;
	BatchElement.VertexFactoryUserData = VertexFactory->GetUniformBuffer();
	BatchElement.UserData = &UserData;

	Collector.AddMesh(ViewIndex, MeshBatch);

	return &MeshBatch;
}

// This setups associated volume mesh for built-in Unreal passes. 
// Actual rendering is prepared FVdbVolumeRendering::PostOpaque_RenderThread.
void FVdbVolumeSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	SCOPE_CYCLE_COUNTER(STAT_VdbSceneProxy_GetDynamicMeshElements);
	check(IsInRenderingThread());

	if (!Material || Material->GetMaterial()->MaterialDomain != MD_Volume)
		return;

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FSceneView* View = Views[ViewIndex];

		if (IsShown(View) && (VisibilityMap & (1 << ViewIndex)) && VdbMaterialRenderExtension->ShouldRenderVolumetricVdb())
		{
			VisibleViews.Add(View);

			FVdbVertexFactoryUserDataWrapper& UserData = Collector.AllocateOneFrameResource<FVdbVertexFactoryUserDataWrapper>();
			UserData.Data.IndexMin = GetIndexMin();
			UserData.Data.IndexSize = GetIndexSize();
			UserData.Data.IndexToLocal = GetIndexToLocal();

			FMeshBatch* Mesh = CreateMeshBatch(View, ViewIndex, ViewFamily, Collector, VdbMaterialRenderExtension.Get(), UserData, Material->GetRenderProxy());
			MeshBatchPerView.Add(View, Mesh);

			{
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				RenderBounds(PDI, ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
			}
		}
	}
}

FPrimitiveViewRelevance FVdbVolumeSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = CastShadows && IsShadowCast(View) && ShouldRenderInMainPass() && !TemperatureOnly;
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	
	// Only if translucent volumetric self shadows are enabled can we setup translucent shadows thanks to fourier opacity maps
	if (AllowTranslucencyPerObjectShadows(View->GetShaderPlatform()))
	{
		Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
		MaterialRelevance.SetPrimitiveViewRelevance(Result);
		Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
	}

	return Result;
}

SIZE_T FVdbVolumeSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FVdbVolumeSceneProxy::CreateRenderThreadResources()
{
	FPrimitiveSceneProxy::CreateRenderThreadResources();

	VdbMaterialRenderExtension->AddVdbProxy(this);
}

void FVdbVolumeSceneProxy::DestroyRenderThreadResources()
{
	FPrimitiveSceneProxy::DestroyRenderThreadResources();

	VdbMaterialRenderExtension->RemoveVdbProxy(this);
}

void FVdbVolumeSceneProxy::Update(const FMatrix44f& InIndexToLocal, const FVector3f& InIndexMin, const FVector3f& InIndexSize, FVdbRenderBuffer* PrimRenderBuffer, FVdbRenderBuffer* SecRenderBuffer, FVdbRenderBuffer* VelRenderBuffer, FVdbRenderBuffer* TerRenderBuffer)
{
	IndexToLocal = InIndexToLocal;
	IndexMin = InIndexMin;
	IndexSize = InIndexSize;
	DensityRenderBuffer = PrimRenderBuffer;
	TemperatureRenderBuffer = SecRenderBuffer;
	VelocityRenderBuffer = VelRenderBuffer;
	ColorRenderBuffer = TerRenderBuffer;
}

void FVdbVolumeSceneProxy::UpdateCurveAtlasTex()
{
	// Doing this every frame allows realtime preview and update when modifying color curves
	CurveAtlasTex = CurveAtlas ? CurveAtlas->GetResource() : nullptr;
}

FRDGTextureRef FVdbVolumeSceneProxy::GetOrCreateRenderTarget(FRDGBuilder& GraphBuilder, const FIntPoint& RtSize, bool EvenFrame)
{
	if (!OffscreenRenderTarget[EvenFrame].IsValid() || OffscreenRenderTarget[EvenFrame]->GetDesc().Extent != RtSize)
	{
		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

		const FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			RtSize,
			PF_FloatRGBA,
			FClearValueBinding(FLinearColor::Transparent),
			TexCreate_None,
			TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable,
			false);

		for (int idx = 0; idx < 2; ++idx)
		{
			FString DebugName = FString::Printf(TEXT("VdbRenderTarget_%d"), idx);
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, OffscreenRenderTarget[idx], *DebugName);
			ensure(OffscreenRenderTarget[idx].IsValid());
		}
	}

	return GraphBuilder.RegisterExternalTexture(OffscreenRenderTarget[EvenFrame]);
}