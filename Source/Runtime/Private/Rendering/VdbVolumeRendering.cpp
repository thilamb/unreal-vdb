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

#include "VdbVolumeRendering.h"
#include "VdbShaders.h"
#include "VdbVolumeSceneProxy.h"
#include "VdbRenderBuffer.h"
#include "VdbCommon.h"
#include "VdbComposite.h"
#include "VolumeMesh.h"
#include "SceneTexturesConfig.h"
#include "SystemTextures.h"

#include "LocalVertexFactory.h"
#include "VdbMeshPassProcessor.inl"

#include "Modules\ModuleManager.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "RenderUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "ClearQuad.h"

#include "VolumeLighting.h"
#include "VolumetricFog.h"

DEFINE_LOG_CATEGORY(LogSparseVolumetrics);
DECLARE_GPU_STAT_NAMED(StatVdbVolume, TEXT("Vdb Volume Rendering"));
DECLARE_GPU_STAT_NAMED(StatVdbShadowDepth, TEXT("Vdb Shadow Depth Rendering"));
DECLARE_GPU_STAT_NAMED(StatVdbTranslucentShadowDepth, TEXT("Vdb Translucent Shadow Depth Rendering"));

void SetupRenderPassParameters(
	FVdbVolumeSceneProxy* Proxy,
	FRDGBuilder& GraphBuilder,
	FVdbShaderPS::FParameters* PassParameters,
	// Light data
	bool ApplyEmissionAndTransmittance,
	bool ApplyDirectLighting,
	bool ApplyShadowTransmittance,
	uint32 LightType,
	FLightSceneInfo* LightSceneInfo,
	const FVisibleLightInfo* VisibleLightInfo,
	// Scene data
	const FPostOpaqueRenderParameters& Parameters,
	const FViewInfo* ViewInfo,
	// Path tracing
	uint32 NumAccumulations,
	FRDGTextureRef PrevAccumuliationTex
)
{
	FVdbShaderParams* VdbParameters = GraphBuilder.AllocParameters<FVdbShaderParams>();

	// Scene data
	VdbParameters->SceneDepthTexture = Parameters.DepthTexture;
	VdbParameters->LinearTexSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	VdbParameters->NumAccumulations = NumAccumulations;
	VdbParameters->PrevAccumTex = PrevAccumuliationTex;

	// Global Vdb data
	VdbParameters->Threshold = FMath::Max(0.0, FVdbCVars::CVarVolumetricVdbThreshold.GetValueOnAnyThread());

	// Light data
	VdbParameters->bApplyEmissionAndTransmittance = ApplyEmissionAndTransmittance;
	VdbParameters->bApplyDirectLighting = ApplyDirectLighting;
	VdbParameters->bApplyShadowTransmittance = ApplyShadowTransmittance;
	VdbParameters->LightType = LightType;

#if VDB_ENGINE_MODIFICATIONS
	FDeferredLightUniformStruct DeferredLightUniform;
	if (ApplyDirectLighting && (LightSceneInfo != nullptr))
	{
		DeferredLightUniform = GetDeferredLightParameters(*ViewInfo, *LightSceneInfo);
	}
	VdbParameters->DeferredLight = DeferredLightUniform;

	// Shadow data
	VdbParameters->ForwardLightData = *ViewInfo->ForwardLightingResources.ForwardLightData;
	VdbParameters->VirtualShadowMapId = VisibleLightInfo ? VisibleLightInfo->GetVirtualShadowMapId(ViewInfo) : INDEX_NONE;

	const FProjectedShadowInfo* ProjectedShadowInfo = VisibleLightInfo ? GetShadowForInjectionIntoVolumetricFog(*VisibleLightInfo) : nullptr;
	if (ProjectedShadowInfo != nullptr)
	{
		SetVolumeShadowingShaderParameters(GraphBuilder, VdbParameters->VolumeShadowingShaderParameters, *ViewInfo, LightSceneInfo, ProjectedShadowInfo);
	}
	else
	{
		SetVolumeShadowingDefaultShaderParameters(GraphBuilder, VdbParameters->VolumeShadowingShaderParameters);
		VdbParameters->VolumeShadowingShaderParameters.TranslatedWorldPosition = DeferredLightUniform.LightParameters.TranslatedWorldPosition;
		VdbParameters->VolumeShadowingShaderParameters.InvRadius = DeferredLightUniform.LightParameters.InvRadius;
	}
	PassParameters->VirtualShadowMapSamplingParameters = Parameters.VirtualShadowMapArray->GetSamplingParameters(GraphBuilder);

	// Indirect lighting data
	VdbParameters->LumenGIVolumeStruct = GetLumenTranslucencyLightingParameters(GraphBuilder, const_cast<FViewInfo*>(ViewInfo)->GetOwnLumenTranslucencyGIVolume(), ViewInfo->LumenFrontLayerTranslucency);
#endif // VDB_ENGINE_MODIFICATIONS

	// Pass params
	PassParameters->View = ViewInfo->ViewUniformBuffer;

	// Finalize VdbUniformBuffer
	TRDGUniformBufferRef<FVdbShaderParams> VdbUniformBuffer = GraphBuilder.CreateUniformBuffer(VdbParameters);
	PassParameters->VdbUniformBuffer = VdbUniformBuffer;
}

//-----------------------------------------------------------------------------
//--- FVdbVolumeRendering
//-----------------------------------------------------------------------------

FVdbVolumeRendering::FVdbVolumeRendering(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FVdbVolumeRendering::ShouldRenderVolumetricVdb() const
{
	return FVdbCVars::CVarVolumetricVdb.GetValueOnRenderThread() && VertexFactory.IsValid();
}

void FVdbVolumeRendering::InitRendering(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	ReleaseRendering();
	{
		InitVolumeMesh(RHICmdList);
		InitVertexFactory();
		InitDelegate();
	}
}

#define RELEASE_RESOURCE(A) if(A) { A->ReleaseResource(); A.Reset(); }
void FVdbVolumeRendering::ReleaseRendering()
{
	check(IsInRenderingThread());

	ReleaseDelegate();
	RELEASE_RESOURCE(VertexFactory);
	RELEASE_RESOURCE(VertexBuffer);
}
#undef RELEASE_RESOURCE

void FVdbVolumeRendering::Init(UTextureRenderTarget2D* DefaultRenderTarget)
{
	if (IsInRenderingThread())
	{
		DefaultVdbRenderTarget = DefaultRenderTarget;
		InitRendering(FRHICommandListExecutor::GetImmediateCommandList());
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(InitVdbRendering)(
			[this, DefaultRenderTarget](FRHICommandListImmediate& RHICmdList)
			{
				Init(DefaultRenderTarget);
			});
	}
}

void FVdbVolumeRendering::Release()
{
	if (IsInRenderingThread())
	{
		ReleaseRendering();
	}
	else
	{
		ENQUEUE_RENDER_COMMAND(InitVdbRendering)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				Release();
			});
	}
}

void FVdbVolumeRendering::InitVolumeMesh(FRHICommandListImmediate& RHICmdList)
{
	VertexBuffer = MakeUnique<FVolumeMeshVertexBuffer>();
	VertexBuffer->InitResource(RHICmdList);
}

void FVdbVolumeRendering::InitVertexFactory()
{
	VertexFactory = MakeUnique<FVolumeMeshVertexFactory>(ERHIFeatureLevel::SM5);
	VertexFactory->Init(VertexBuffer.Get());
}

void FVdbVolumeRendering::InitDelegate()
{
	if (!RenderPostOpaqueDelegateHandle.IsValid())
	{
		const FName RendererModuleName("Renderer");
		IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
		if (RendererModule)
		{
#if VDB_CAST_SHADOWS
			ShadowDepthDelegate.BindRaw(this, &FVdbVolumeRendering::ShadowDepth_RenderThread);
			ShadowDepthDelegateHandle = RendererModule->RegisterShadowDepthRenderDelegate(ShadowDepthDelegate);
			TranslucentShadowDepthDelegate.BindRaw(this, &FVdbVolumeRendering::TranslucentShadowDepth_RenderThread);
			TranslucentShadowDepthDelegateHandle = RendererModule->RegisterTranslucentShadowDepthRenderDelegate(TranslucentShadowDepthDelegate);
#endif

			RenderPostOpaqueDelegate.BindRaw(this, &FVdbVolumeRendering::RenderPostOpaque_RenderThread);
			RenderOverlayDelegate.BindRaw(this, &FVdbVolumeRendering::RenderOverlay_RenderThread);
			// Render VDBs before or after Transparent objects
			RenderPostOpaqueDelegateHandle = RendererModule->RegisterPostOpaqueRenderDelegate(RenderPostOpaqueDelegate);
			RenderOverlayDelegateHandle = RendererModule->RegisterOverlayRenderDelegate(RenderOverlayDelegate);
		}
	}
}

void FVdbVolumeRendering::ReleaseDelegate()
{
	if (RenderPostOpaqueDelegateHandle.IsValid())
	{
		const FName RendererModuleName("Renderer");
		IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
		if (RendererModule)
		{
#if VDB_CAST_SHADOWS
			RendererModule->RemoveShadowDepthRenderDelegate(ShadowDepthDelegateHandle);
			RendererModule->RemoveShadowDepthRenderDelegate(TranslucentShadowDepthDelegateHandle);
#endif
			RendererModule->RemovePostOpaqueRenderDelegate(RenderPostOpaqueDelegateHandle);
			RendererModule->RemovePostOpaqueRenderDelegate(RenderOverlayDelegateHandle);
		}

		RenderPostOpaqueDelegateHandle.Reset();
		RenderOverlayDelegateHandle.Reset();
		ShadowDepthDelegateHandle.Reset();
		TranslucentShadowDepthDelegateHandle.Reset();
	}
}

#if VDB_CAST_SHADOWS
void FVdbVolumeRendering::ShadowDepth_RenderThread(FShadowDepthRenderParameters& Parameters)
{
	SCOPE_CYCLE_COUNTER(STAT_VdbRendering_RT);

	const FSceneView* View = static_cast<const FSceneView*>(Parameters.ShadowDepthView);
	const FMatrix& ViewMat = View->ShadowViewMatrices.GetViewMatrix();

	TArray<FVdbVolumeSceneProxy*> OpaqueProxies = VdbProxies.FilterByPredicate([View](const FVdbVolumeSceneProxy* Proxy) { return !Proxy->IsTranslucent() && Proxy->IsVisible(View) && !Proxy->IsTemperatureOnly(); });
	OpaqueProxies.Sort([ViewMat](const FVdbVolumeSceneProxy& Lhs, const FVdbVolumeSceneProxy& Rhs) -> bool
		{
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z < ViewMat.TransformPosition(RightProxyCenter).Z; // front to back
		});

	TArray<FVdbVolumeSceneProxy*> TranslucentProxies = VdbProxies.FilterByPredicate([View](const FVdbVolumeSceneProxy* Proxy) { return Proxy->IsTranslucent() && Proxy->IsVisible(View) && !Proxy->IsTemperatureOnly(); });
	TranslucentProxies.Sort([ViewMat](const FVdbVolumeSceneProxy& Lhs, const FVdbVolumeSceneProxy& Rhs) -> bool
		{
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z > ViewMat.TransformPosition(RightProxyCenter).Z; // back to front
		});

	auto DrawVdbProxies = [&](const TArray<FVdbVolumeSceneProxy*>& Proxies, TRDGUniformBufferRef<FVdbDepthShaderParams>& VdbUniformBuffer, bool Translucent)
	{
		FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

		FVdbShadowDepthPassParameters* PassParameters = GraphBuilder.AllocParameters<FVdbShadowDepthPassParameters>();
		PassParameters->View = Parameters.ShadowDepthView->ViewUniformBuffer;
		PassParameters->DeferredPassUniformBuffer = Parameters.DeferredPassUniformBuffer;
		PassParameters->VirtualShadowMapSamplingParameters = Parameters.VirtualShadowMapArray->GetSamplingParameters(GraphBuilder);
		PassParameters->VdbUniformBuffer = VdbUniformBuffer;
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(
			Parameters.ShadowDepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ENoAction,
			FExclusiveDepthStencil::DepthWrite_StencilNop);

		GraphBuilder.AddPass(
			Translucent ? RDG_EVENT_NAME("Vdb Translucent Rendering") : RDG_EVENT_NAME("Vdb Opaque Rendering"),
			PassParameters,
			ERDGPassFlags::Raster,
			[this, &InView = *View, Parameters, Proxies](FRHICommandListImmediate& RHICmdList)
			{
				SCOPED_DRAW_EVENT(RHICmdList, StatVdbShadowDepth);
				SCOPED_GPU_STAT(RHICmdList, StatVdbShadowDepth);

				Parameters.ProjectedShadowInfo->SetStateForView(RHICmdList);

				for (const FVdbVolumeSceneProxy* Proxy : Proxies)
				{
					if (Proxy && Proxy->GetMaterial() && Proxy->GetDensityRenderResource())
					{
						SCOPED_DRAW_EVENTF(RHICmdList, StatVdbShadowDepth, TEXT("VDB (shadows) %s"), *Proxy->GetOwnerName().ToString());

						DrawDynamicMeshPass(InView, RHICmdList,
							[&](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
							{
								FVdbShadowDepthShaderElementData ShaderElementData;
								ShaderElementData.CustomIntData0 = Proxy->GetCustomIntData0();
								ShaderElementData.CustomIntData1 = Proxy->GetCustomIntData1();
								ShaderElementData.CustomFloatData0 = Proxy->GetCustomFloatData0();
								ShaderElementData.CustomFloatData1 = Proxy->GetCustomFloatData1();
								ShaderElementData.CustomFloatData2 = Proxy->GetCustomFloatData2();
								ShaderElementData.SliceMinData = Proxy->GetSliceMin();
								ShaderElementData.SliceMaxData = Proxy->GetSliceMax();
								ShaderElementData.DensityBufferSRV = Proxy->GetDensityRenderResource()->GetBufferSRV();
								ShaderElementData.TemperatureBufferSRV = nullptr;
								ShaderElementData.ColorBufferSRV = nullptr;
								if (!ShaderElementData.DensityBufferSRV)
									return;

								FVdbDepthMeshProcessor PassMeshProcessor(
									InView.Family->Scene->GetRenderScene(),
									&InView,
									DynamicMeshPassContext,
									Parameters.ProjectedShadowInfo->GetShadowDepthType(),
									EMeshPass::CSMShadowDepth, // TODO: support VSM
									Proxy->IsLevelSet(), 
									MoveTemp(ShaderElementData));
								//ShadowInfo->bOnePassPointLightShadow

								FVdbVertexFactoryUserDataWrapper UserData;
								UserData.Data.IndexMin = Proxy->GetIndexMin() - ShaderElementData.CustomFloatData2.Y;
								UserData.Data.IndexSize = Proxy->GetIndexSize() + 2.0 * ShaderElementData.CustomFloatData2.Y;
								UserData.Data.IndexToLocal = Proxy->GetIndexToLocal();

								FMeshBatch* VolumeMesh = Proxy->GetMeshFromView(&InView);
								if (VolumeMesh && VolumeMesh->CastShadow)
								{
									const uint64 DefaultBatchElementMask = ~0ull; // or 1 << 0; // LOD 0 only
									PassMeshProcessor.AddMeshBatch(*VolumeMesh, DefaultBatchElementMask, Proxy);
								}
							}
						);
					}
				}
			});
	};

	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;
	FVdbDepthShaderParams* VdbParameters = GraphBuilder.AllocParameters<FVdbDepthShaderParams>();
	VdbParameters->ShadowClipToTranslatedWorld = Parameters.ProjectedShadowInfo->TranslatedWorldToClipOuterMatrix.Inverse();
	VdbParameters->ShadowPreViewTranslation = FVector3f(Parameters.ProjectedShadowInfo->PreShadowTranslation);

	{
		auto& CachedParams = Parameters.ShadowDepthView->CachedViewUniformShaderParameters;

		float Mx = 2.0f * CachedParams->ViewSizeAndInvSize.Z;
		float My = -2.0f * CachedParams->ViewSizeAndInvSize.W;
		float Ax = -1.0f - 2.0f * CachedParams->ViewRectMin.X * CachedParams->ViewSizeAndInvSize.Z;
		float Ay = 1.0f + 2.0f * CachedParams->ViewRectMin.Y * CachedParams->ViewSizeAndInvSize.W;

		FTranslationMatrix44f TranslationMat(FVector3f(Parameters.ProjectedShadowInfo->PreShadowTranslation - View->ViewMatrices.GetPreViewTranslation()));
		FMatrix44f ProjectionMatrix = TranslationMat * Parameters.ProjectedShadowInfo->TranslatedWorldToClipOuterMatrix;		// LWC_TDOO: Precision loss?

		VdbParameters->ShadowClipToTranslatedWorld = ProjectionMatrix.Inverse();
		VdbParameters->ShadowSVPositionToClip = FVector4f(Mx, Ax, My, Ay);
	}


	if (!Parameters.ProjectedShadowInfo->OnePassShadowViewProjectionMatrices.IsEmpty())
	{
		for (int32 idx = 0; idx < 6; ++idx)
		{
			FMatrix ViewProjMat = Parameters.ProjectedShadowInfo->OnePassShadowViewProjectionMatrices[idx];
			VdbParameters->CubeShadowClipToTranslatedWorld[idx] = FMatrix44f(ViewProjMat.Inverse());
		}
	}
	TRDGUniformBufferRef<FVdbDepthShaderParams> VdbUniformBuffer = GraphBuilder.CreateUniformBuffer(VdbParameters);
	
	if (!OpaqueProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbShadowDepth_RT);
		DrawVdbProxies(OpaqueProxies, VdbUniformBuffer, false);
	}

	if (!TranslucentProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbShadowDepth_RT);
		DrawVdbProxies(TranslucentProxies, VdbUniformBuffer, true);
	}
}

void FVdbVolumeRendering::TranslucentShadowDepth_RenderThread(FTranslucentShadowDepthRenderParameters& Parameters)
{
	SCOPE_CYCLE_COUNTER(STAT_VdbRendering_RT);

	const FSceneView* View = static_cast<const FSceneView*>(Parameters.ShadowDepthView);

	auto DrawVdbProxies = [&](const TArray<FVdbVolumeSceneProxy*>& Proxies, TRDGUniformBufferRef<FVdbDepthShaderParams>& VdbUniformBuffer)
	{
		FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

		FVdbTrasnlucentShadowDepthPassParameters* PassParameters = GraphBuilder.AllocParameters<FVdbTrasnlucentShadowDepthPassParameters>();
		PassParameters->View = Parameters.ShadowDepthView->ViewUniformBuffer;
		PassParameters->PassUniformBuffer = Parameters.DeferredPassUniformBuffer;
		//PassParameters->VirtualShadowMapSamplingParameters = Parameters.VirtualShadowMapArray->GetSamplingParameters(GraphBuilder);
		PassParameters->VdbUniformBuffer = VdbUniformBuffer;
		PassParameters->RenderTargets = Parameters.RenderTargets;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Vdb Translucent Shadow Rendering"),
			PassParameters,
			ERDGPassFlags::Raster,
			[this, &InView = *View, Parameters, Proxies](FRHICommandListImmediate& RHICmdList)
			{
				SCOPED_DRAW_EVENT(RHICmdList, StatVdbTranslucentShadowDepth);
				SCOPED_GPU_STAT(RHICmdList, StatVdbTranslucentShadowDepth);

				const FProjectedShadowInfo* ShadowInfo = Parameters.ProjectedShadowInfo;
				ShadowInfo->SetStateForView(RHICmdList);

				// Clear the shadow and its border
				RHICmdList.SetViewport(
					ShadowInfo->X,
					ShadowInfo->Y,
					0.0f,
					(ShadowInfo->X + ShadowInfo->BorderSize * 2 + ShadowInfo->ResolutionX),
					(ShadowInfo->Y + ShadowInfo->BorderSize * 2 + ShadowInfo->ResolutionY),
					1.0f
				);

				FLinearColor ClearColors[2] = { FLinearColor(0,0,0,0), FLinearColor(0,0,0,0) };
				DrawClearQuadMRT(RHICmdList, true, UE_ARRAY_COUNT(ClearColors), ClearColors, false, 1.0f, false, 0);

				// Set the viewport for the shadow.
				RHICmdList.SetViewport(
					(ShadowInfo->X + ShadowInfo->BorderSize),
					(ShadowInfo->Y + ShadowInfo->BorderSize),
					0.0f,
					(ShadowInfo->X + ShadowInfo->BorderSize + ShadowInfo->ResolutionX),
					(ShadowInfo->Y + ShadowInfo->BorderSize + ShadowInfo->ResolutionY),
					1.0f
				);

				for (const FVdbVolumeSceneProxy* Proxy : Proxies)
				{
					if (Proxy && Proxy->GetMaterial() && Proxy->GetDensityRenderResource())
					{
						SCOPED_DRAW_EVENTF(RHICmdList, StatVdbShadowDepthMaterial, TEXT("VDB (shadows) %s"), *Proxy->GetOwnerName().ToString());

						DrawDynamicMeshPass(InView, RHICmdList,
							[&](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
							{
								FVdbShadowDepthShaderElementData ShaderElementData;
								ShaderElementData.CustomIntData0 = Proxy->GetCustomIntData0();
								ShaderElementData.CustomIntData1 = Proxy->GetCustomIntData1();
								ShaderElementData.CustomFloatData0 = Proxy->GetCustomFloatData0();
								ShaderElementData.CustomFloatData1 = Proxy->GetCustomFloatData1();
								ShaderElementData.CustomFloatData2 = Proxy->GetCustomFloatData2();
								ShaderElementData.SliceMinData = Proxy->GetSliceMin();
								ShaderElementData.SliceMaxData = Proxy->GetSliceMax();
								ShaderElementData.DensityBufferSRV = Proxy->GetDensityRenderResource()->GetBufferSRV();
								ShaderElementData.TemperatureBufferSRV = nullptr;
								ShaderElementData.ColorBufferSRV = nullptr;
								if (!ShaderElementData.DensityBufferSRV)
									return;

								FVdbTranslucentDepthMeshProcessor PassMeshProcessor(
									InView.Family->Scene->GetRenderScene(),
									&InView,
									DynamicMeshPassContext,
									Parameters.ProjectedShadowInfo,
									MoveTemp(ShaderElementData));
								//ShadowInfo->bOnePassPointLightShadow

								FVdbVertexFactoryUserDataWrapper UserData;
								UserData.Data.IndexMin = Proxy->GetIndexMin() - ShaderElementData.CustomFloatData2.Y;
								UserData.Data.IndexSize = Proxy->GetIndexSize() + 2.0 * ShaderElementData.CustomFloatData2.Y;
								UserData.Data.IndexToLocal = Proxy->GetIndexToLocal();

								FMeshBatch* VolumeMesh = Proxy->GetMeshFromView(&InView);
								if (VolumeMesh && VolumeMesh->CastShadow)
								{
									const uint64 DefaultBatchElementMask = ~0ull; // or 1 << 0; // LOD 0 only
									PassMeshProcessor.AddMeshBatch(*VolumeMesh, DefaultBatchElementMask, Proxy);
								}
							}
						);
					}
				}
			});
	};

	const FMatrix& ViewMat = View->ShadowViewMatrices.GetViewMatrix();

	TArray<FVdbVolumeSceneProxy*> TranslucentProxies = VdbProxies.FilterByPredicate([View](const FVdbVolumeSceneProxy* Proxy) { return Proxy->IsTranslucent() && Proxy->IsVisible(View) && !Proxy->IsTemperatureOnly(); });
	TranslucentProxies.Sort([ViewMat](const FVdbVolumeSceneProxy& Lhs, const FVdbVolumeSceneProxy& Rhs) -> bool
		{
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z > ViewMat.TransformPosition(RightProxyCenter).Z; // back to front
		});

	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;
	FVdbDepthShaderParams* VdbParameters = GraphBuilder.AllocParameters<FVdbDepthShaderParams>();
	VdbParameters->ShadowClipToTranslatedWorld = Parameters.ProjectedShadowInfo->TranslatedWorldToClipOuterMatrix.Inverse();
	if (!Parameters.ProjectedShadowInfo->OnePassShadowViewProjectionMatrices.IsEmpty())
	{
		for (int32 idx = 0; idx < 6; ++idx)
		{
			FMatrix ViewProjMat = Parameters.ProjectedShadowInfo->OnePassShadowViewProjectionMatrices[idx];
			VdbParameters->CubeShadowClipToTranslatedWorld[idx] = FMatrix44f(ViewProjMat.Inverse());
		}
	}
	VdbParameters->ShadowPreViewTranslation = FVector3f(Parameters.ProjectedShadowInfo->PreShadowTranslation);
	TRDGUniformBufferRef<FVdbDepthShaderParams> VdbUniformBuffer = GraphBuilder.CreateUniformBuffer(VdbParameters);

	if (!TranslucentProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbTranslucentShadowDepth_RT);
		DrawVdbProxies(TranslucentProxies, VdbUniformBuffer);
	}
}
#endif

void FVdbVolumeRendering::RenderPostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters)
{
	Render_RenderThread(Parameters, false);
}

void FVdbVolumeRendering::RenderOverlay_RenderThread(FPostOpaqueRenderParameters& Parameters)
{
	Render_RenderThread(Parameters, true);
}

void FVdbVolumeRendering::Render_RenderThread(FPostOpaqueRenderParameters& Parameters, bool PostTranslucents)
{
	if (!ShouldRenderVolumetricVdb())
		return;

	SCOPE_CYCLE_COUNTER(STAT_VdbRendering_RT);

	RDG_EVENT_SCOPE(*Parameters.GraphBuilder, "Vdb Material Rendering");
	RDG_GPU_STAT_SCOPE(*Parameters.GraphBuilder, StatVdbVolume);

	const FSceneView* View = static_cast<FSceneView*>(Parameters.Uid);
	const FViewInfo* ViewInfo = static_cast<const FViewInfo*>(View);
	const FMatrix& ViewMat = View->ViewMatrices.GetViewMatrix();
	
	const bool UsePathTracing = View->Family->EngineShowFlags.PathTracing;
	if (UsePathTracing && !PostTranslucents) // When using pathtracing only use overlay delegate render mode
		return;

	TArray<FVdbVolumeSceneProxy*> OpaqueProxies = VdbProxies.FilterByPredicate(
		[View, PostTranslucents, UsePathTracing](const FVdbVolumeSceneProxy* Proxy)
		{ 
			return !Proxy->IsTranslucent() && Proxy->IsVisible(View) && (Proxy->RendersAfterTransparents() == PostTranslucents || UsePathTracing);
		});
	OpaqueProxies.Sort([ViewMat](const FVdbVolumeSceneProxy& Lhs, const FVdbVolumeSceneProxy& Rhs) -> bool 
		{ 
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z < ViewMat.TransformPosition(RightProxyCenter).Z; // front to back
		});

	TArray<FVdbVolumeSceneProxy*> TranslucentProxies = VdbProxies.FilterByPredicate(
		[View, PostTranslucents, UsePathTracing](const FVdbVolumeSceneProxy* Proxy)
		{ 
			return Proxy->IsTranslucent() && Proxy->IsVisible(View) && (Proxy->RendersAfterTransparents() == PostTranslucents || UsePathTracing);
		});
	TranslucentProxies.Sort([ViewMat](const FVdbVolumeSceneProxy& Lhs, const FVdbVolumeSceneProxy& Rhs) -> bool
		{
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z > ViewMat.TransformPosition(RightProxyCenter).Z; // back to front
		});

	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

	SVdbPathtrace VdbPathtrace;
#if RHI_RAYTRACING
	if (ViewInfo->Family->EngineShowFlags.PathTracing)
	{
		if (FSceneViewState* ViewState = ViewInfo->ViewState)
		{
			VdbPathtrace.NumAccumulations = ViewState->GetPathTracingSampleIndex() ? ViewState->GetPathTracingSampleIndex() - 1u : 0u;
		}
		VdbPathtrace.RtSize = Parameters.ColorTexture->Desc.Extent;
		VdbPathtrace.IsEven = VdbPathtrace.NumAccumulations % 2;
		VdbPathtrace.FirstRender = true;
		VdbPathtrace.MaxSPP = FMath::Max(View->FinalPostProcessSettings.PathTracingSamplesPerPixel, 1);
		VdbPathtrace.UsePathtracing = true;
	}
#endif

	if (!OpaqueProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbOpaque_RT);
		for (FVdbVolumeSceneProxy* Proxy : OpaqueProxies)
		{
			RenderLights(Proxy, false, Parameters, VdbPathtrace, nullptr, nullptr);
		}
	}

	if (!TranslucentProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbTranslucent_RT);

		FRDGTexture* VdbCurrRenderTexture = nullptr;
		if (DefaultVdbRenderTargetTex && DefaultVdbRenderTargetTex->GetTextureRHI())
		{
			VdbCurrRenderTexture = RegisterExternalTexture(GraphBuilder, DefaultVdbRenderTargetTex->GetTextureRHI(), TEXT("VdbRenderTarget"));
		}
		else
		{
			FRDGTextureDesc TexDesc = Parameters.ColorTexture->Desc;
			TexDesc.Format = PF_FloatRGBA; // force RGBA. Depending on quality settings, ColorTexture might not have alpha
			TexDesc.ClearValue = FClearValueBinding(FLinearColor::Transparent);
			VdbCurrRenderTexture = GraphBuilder.CreateTexture(TexDesc, TEXT("VdbRenderTexture"));
		}
		AddClearRenderTargetPass(GraphBuilder, VdbCurrRenderTexture);

		bool bWriteDepth = FVdbCVars::CVarVolumetricVdbWriteDepth.GetValueOnRenderThread();
		FRDGTextureRef DepthTestTexture = nullptr;
		if (bWriteDepth)
		{
			DepthTestTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Parameters.DepthTexture->Desc.Extent,
					PF_DepthStencil,
					FClearValueBinding::DepthFar,
					TexCreate_DepthStencilTargetable | TexCreate_ShaderResource,
					1),
				TEXT("VdbMaterialDepth"));
			AddClearDepthStencilPass(GraphBuilder, DepthTestTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::ENoAction);
		}

		for (FVdbVolumeSceneProxy* Proxy : TranslucentProxies)
		{
			if (!VdbPathtrace.UsePathtracing || VdbPathtrace.NumAccumulations < (uint32)VdbPathtrace.MaxSPP)
			{
				RenderLights(Proxy, true, Parameters, VdbPathtrace, VdbCurrRenderTexture, DepthTestTexture);
			}

			if (UsePathTracing)
			{
				FRDGTextureRef RenderTexture = Proxy->GetOrCreateRenderTarget(GraphBuilder, VdbPathtrace.RtSize, VdbPathtrace.IsEven);
				VdbComposite::CompositeFullscreen(GraphBuilder, RenderTexture, VdbCurrRenderTexture, nullptr, nullptr, View);
			}
		}

		// Add optional post-processing (blurring, denoising etc.)
		EVdbDenoiserMethod Method = FVdbCVars::CVarVolumetricVdbDenoiser.GetValueOnAnyThread() >= 0 ?
			EVdbDenoiserMethod(FVdbCVars::CVarVolumetricVdbDenoiser.GetValueOnAnyThread()) : DenoiserMethod;
		FRDGTexture* DenoisedTex = VdbDenoiser::ApplyDenoising(
			GraphBuilder, VdbCurrRenderTexture, View, Parameters.ViewportRect, Method);

		// Composite VDB offscreen rendering onto back buffer
		VdbComposite::CompositeFullscreen(
			GraphBuilder,
			DenoisedTex,
			Parameters.ColorTexture,
			bWriteDepth ? DepthTestTexture : nullptr,
			bWriteDepth ? Parameters.DepthTexture : nullptr,
			View);
	}
}

void FVdbVolumeRendering::RenderLights(
	// Object Data
	FVdbVolumeSceneProxy* Proxy,
	bool Translucent, 
	// Scene data
	const FPostOpaqueRenderParameters& Parameters,
	const SVdbPathtrace& VdbPathtrace,
	FRDGTexture* RenderTexture, 
	FRDGTexture* DepthRenderTexture)
{
	const FSceneView* View = static_cast<const FSceneView*>(Parameters.View);
	//const FViewInfo* ViewInfo = static_cast<const FViewInfo*>(View);
	const FSceneViewFamily* ViewFamily = View->Family;
	const FScene* Scene = (FScene*)ViewFamily->Scene;

	if (!Proxy || !Proxy->GetMaterial() || !Proxy->IsVisible(View) || !Proxy->GetDensityRenderResource() || !Proxy->GetDensityRenderResource()->IsInitialized())
		return;

#if VDB_ENGINE_MODIFICATIONS
	// Light culling
	TArray<FLightSceneInfoCompact, TInlineAllocator<64>> LightSceneInfoCompact;
	for (auto LightIt = Scene->Lights.CreateConstIterator(); LightIt; ++LightIt)
	{
		if (LightIt->AffectsPrimitive(Proxy->GetBounds(), Proxy))
		{
			LightSceneInfoCompact.Add(*LightIt);
		}
	}

	// Light loop:
	int32 NumPasses = Proxy->IsTemperatureOnly() ? 1 : FMath::Max(LightSceneInfoCompact.Num(), 1);
	for (int32 PassIndex = 0; PassIndex < NumPasses; ++PassIndex)
	{
		bool bApplyEmissionAndTransmittance = PassIndex == 0;
		bool bApplyDirectLighting = !LightSceneInfoCompact.IsEmpty();
		bool bApplyShadowTransmittance = false;

		uint32 LightType = 0;
		FLightSceneInfo* LightSceneInfo = nullptr;
		const FVisibleLightInfo* VisibleLightInfo = nullptr;
		if (bApplyDirectLighting)
		{
			LightType = LightSceneInfoCompact[PassIndex].LightType;
			LightSceneInfo = LightSceneInfoCompact[PassIndex].LightSceneInfo;
			check(LightSceneInfo != nullptr);

			bApplyDirectLighting = (LightSceneInfo != nullptr);
			if (LightSceneInfo && LightSceneInfo->Id < Parameters.VisibleLightInfos.Num())
			{
				VisibleLightInfo = &Parameters.VisibleLightInfos[LightSceneInfo->Id];
				bApplyShadowTransmittance = LightSceneInfo->Proxy->CastsVolumetricShadow();
			}
		}

		RenderLight(
			// Object data
			Proxy,
			Translucent,
			// Light data
			bApplyEmissionAndTransmittance,
			bApplyDirectLighting,
			bApplyShadowTransmittance,
			LightType,
			LightSceneInfo,
			VisibleLightInfo,
			// Scene data
			Parameters,
			VdbPathtrace,
			RenderTexture,
			DepthRenderTexture
		);

		// Disable any depth test / write after first lighting pass
		DepthRenderTexture = nullptr;
	}
#else
	RenderLight(
		// Object data
		Proxy,
		Translucent,
		// Hardcoded directional light data
		true, // bApplyEmissionAndTransmittance
		Scene->SimpleDirectionalLight != nullptr, // bApplyDirectLighting
		true, // bApplyShadowTransmittance
		0, // LightType
		nullptr,
		nullptr,
		// Scene data
		Parameters,
		VdbPathtrace,
		RenderTexture,
		DepthRenderTexture
	);

	// Disable any depth test / write after first lighting pass
	DepthRenderTexture = nullptr;
#endif
}

void FVdbVolumeRendering::RenderLight(
	// Object data
	FVdbVolumeSceneProxy* Proxy,
	bool Translucent,
	// Light data
	bool ApplyEmissionAndTransmittance,
	bool ApplyDirectLighting,
	bool ApplyShadowTransmittance,
	uint32 LightType,
	FLightSceneInfo* LightSceneInfo,
	const FVisibleLightInfo* VisibleLightInfo,
	// Scene data
	const FPostOpaqueRenderParameters& Parameters,
	const SVdbPathtrace& VdbPathtrace,
	FRDGTexture* RenderTexture,
	FRDGTexture* DepthRenderTexture)
{
	const FSceneView* View = static_cast<FSceneView*>(Parameters.Uid);
	const FViewInfo* ViewInfo = static_cast<const FViewInfo*>(View);

	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

	FVdbShaderPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVdbShaderPS::FParameters>();
	SetupRenderPassParameters(
		Proxy,
		GraphBuilder,
		PassParameters, 
		ApplyEmissionAndTransmittance, 
		ApplyDirectLighting, 
		ApplyShadowTransmittance, 
		LightType,
		LightSceneInfo,
		VisibleLightInfo,
		Parameters,
		ViewInfo,
		VdbPathtrace.NumAccumulations,
		VdbPathtrace.UsePathtracing ? Proxy->GetOrCreateRenderTarget(GraphBuilder, VdbPathtrace.RtSize, !VdbPathtrace.IsEven) : FRDGSystemTextures::Get(GraphBuilder).Black
		);

	if (VdbPathtrace.UsePathtracing)
	{
		RenderTexture = Proxy->GetOrCreateRenderTarget(GraphBuilder, VdbPathtrace.RtSize, VdbPathtrace.IsEven);
	}

	bool bClear = VdbPathtrace.UsePathtracing && ApplyEmissionAndTransmittance;

	// Render Targets
	bool bWriteDepth = DepthRenderTexture != nullptr;
	if (RenderTexture)
	{
		PassParameters->RenderTargets[0] = FRenderTargetBinding(RenderTexture, bClear ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
		if (bWriteDepth)
		{
			PassParameters->RenderTargets.DepthStencil =
				FDepthStencilBinding(DepthRenderTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);
		}
	}
	else
	{
		PassParameters->RenderTargets[0] = FRenderTargetBinding(Parameters.ColorTexture, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil =
			FDepthStencilBinding(Parameters.DepthTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);
	}

	GraphBuilder.AddPass(
		Translucent ? RDG_EVENT_NAME("Vdb Translucent Rendering") : RDG_EVENT_NAME("Vdb Opaque Rendering"),
		PassParameters,
		ERDGPassFlags::Raster,
		[this, &InView = *View, ViewportRect = Parameters.ViewportRect, Proxy, bWriteDepth, LightSceneInfo, FirstLight = ApplyEmissionAndTransmittance](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENTF(RHICmdList, StatVdbVolume, TEXT("VDB (main pass) %s, Light %s"), *Proxy->GetOwnerName().ToString(), LightSceneInfo ? *LightSceneInfo->Proxy->GetOwnerNameOrLabel() : *FString(""));
			SCOPED_GPU_STAT(RHICmdList, StatVdbVolume);

			RHICmdList.SetViewport(ViewportRect.Min.X, ViewportRect.Min.Y, 0.0f, ViewportRect.Max.X, ViewportRect.Max.Y, 1.0f);
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

			FRHITextureViewCache TexCache;

			DrawDynamicMeshPass(InView, RHICmdList,
				[&](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
				{
					FVdbElementData ShaderElementData;
					ShaderElementData.CustomIntData0 = Proxy->GetCustomIntData0();
					ShaderElementData.CustomIntData1 = Proxy->GetCustomIntData1();
					ShaderElementData.CustomFloatData0 = Proxy->GetCustomFloatData0();
					ShaderElementData.CustomFloatData1 = Proxy->GetCustomFloatData1();
					ShaderElementData.CustomFloatData2 = Proxy->GetCustomFloatData2();
					ShaderElementData.SliceMinData = Proxy->GetSliceMin();
					ShaderElementData.SliceMaxData = Proxy->GetSliceMax();
					ShaderElementData.DensityBufferSRV = Proxy->IsTemperatureOnly() ? Proxy->GetTemperatureRenderResource()->GetBufferSRV() : Proxy->GetDensityRenderResource()->GetBufferSRV();
					ShaderElementData.TemperatureBufferSRV = Proxy->GetTemperatureRenderResource() ? Proxy->GetTemperatureRenderResource()->GetBufferSRV() : nullptr;
					ShaderElementData.ColorBufferSRV = Proxy->GetColorRenderResource() ? Proxy->GetColorRenderResource()->GetBufferSRV() : nullptr;
					if (!ShaderElementData.DensityBufferSRV)
						return;

					FTexture* CurveAtlas = Proxy->GetBlackbodyAtlasResource();
					FTextureRHIRef CurveAtlasRHI = CurveAtlas ? CurveAtlas->GetTextureRHI() : nullptr;
					if (CurveAtlasRHI)
					{
						ShaderElementData.BlackbodyColorSRV = TexCache.GetOrCreateSRV(RHICmdList, CurveAtlasRHI, FRHITextureSRVCreateInfo());
					}
					else
					{
						ShaderElementData.BlackbodyColorSRV = GBlackTextureWithSRV->ShaderResourceViewRHI;
					}

					FVdbMeshProcessor PassMeshProcessor(
						InView.Family->Scene->GetRenderScene(),
						&InView,
						DynamicMeshPassContext,
						Proxy->IsLevelSet(), Proxy->IsTranslucentLevelSet(),
						Proxy->UseImprovedEnvLight(),
						Proxy->UseTrilinearSampling() || FVdbCVars::CVarVolumetricVdbTrilinear.GetValueOnRenderThread(),
						bWriteDepth, FirstLight,
						ShaderElementData.TemperatureBufferSRV != nullptr,
						ShaderElementData.ColorBufferSRV != nullptr,
						MoveTemp(ShaderElementData));

					FVdbVertexFactoryUserDataWrapper UserData;
					UserData.Data.IndexMin = Proxy->GetIndexMin() - ShaderElementData.CustomFloatData2.Y;
					UserData.Data.IndexSize = Proxy->GetIndexSize() + 2.0 * ShaderElementData.CustomFloatData2.Y;
					UserData.Data.IndexToLocal = Proxy->GetIndexToLocal();

					if (FMeshBatch* VolumeMesh = Proxy->GetMeshFromView(&InView))
					{
						const uint64 DefaultBatchElementMask = ~0ull; // or 1 << 0; // LOD 0 only
						PassMeshProcessor.AddMeshBatch(*VolumeMesh, DefaultBatchElementMask, Proxy);
					}
				}
			);
		}
	);
}


void FVdbVolumeRendering::AddVdbProxy(FVdbVolumeSceneProxy* Proxy)
{
	ENQUEUE_RENDER_COMMAND(FAddVdbProxyCommand)(
		[this, Proxy](FRHICommandListImmediate& RHICmdList)
		{
			check(VdbProxies.Find(Proxy) == INDEX_NONE);
			VdbProxies.Emplace(Proxy);
		});
}

void FVdbVolumeRendering::RemoveVdbProxy(FVdbVolumeSceneProxy* Proxy)
{
	ENQUEUE_RENDER_COMMAND(FRemoveVdbProxyCommand)(
		[this, Proxy](FRHICommandListImmediate& RHICmdList)
		{
			auto Idx = VdbProxies.Find(Proxy);
			if (Idx != INDEX_NONE)
			{
				VdbProxies.Remove(Proxy);
			}
		});
}

void FVdbVolumeRendering::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// Reset visibility on all registered FVdbProxies, before SceneVisibility is computed 
	for (FVdbVolumeSceneProxy* Proxy : VdbProxies)
	{
		Proxy->ResetVisibility();
		Proxy->UpdateCurveAtlasTex();
	}
}

// Called on game thread when view family is about to be rendered.
void FVdbVolumeRendering::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (DefaultVdbRenderTarget)
	{
		if (const FRenderTarget* RefRenderTarget = InViewFamily.RenderTarget)
		{
			const FSceneTexturesConfig& Config = FSceneTexturesConfig::Get();
			if ((Config.Extent.X != DefaultVdbRenderTarget->SizeX ||
				Config.Extent.Y != DefaultVdbRenderTarget->SizeY ||
				DefaultVdbRenderTarget->RenderTargetFormat != RTF_RGBA16f) &&
				(Config.Extent.X > 0 && Config.Extent.Y > 0))
			{
				DefaultVdbRenderTarget->ClearColor = FLinearColor::Transparent;
				DefaultVdbRenderTarget->InitCustomFormat(Config.Extent.X, Config.Extent.Y, PF_FloatRGBA, true);
				DefaultVdbRenderTarget->UpdateResourceImmediate(true);
			}
		}

		DefaultVdbRenderTargetTex = DefaultVdbRenderTarget->GetResource();
	}
	else
	{
		DefaultVdbRenderTargetTex = nullptr;
	}
}
