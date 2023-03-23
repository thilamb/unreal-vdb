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

#include "VdbMaterialRendering.h"
#include "VdbShaders.h"
#include "VdbMaterialSceneProxy.h"
#include "VdbRenderBuffer.h"
#include "VdbCommon.h"
#include "VdbComposite.h"
#include "VolumeMesh.h"
#include "SceneTexturesConfig.h"
#include "SystemTextures.h"

#include "LocalVertexFactory.h"
//#include "MeshPassProcessor.h"
//#include "MeshPassProcessor.inl"
#include "VdbMeshPassProcessor.inl"

#include "Modules\ModuleManager.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "RenderUtils.h"

#include "VolumeLighting.h"
#include "VolumetricFog.h"

DEFINE_LOG_CATEGORY(LogSparseVolumetrics);
DECLARE_GPU_STAT_NAMED(StatVdbMaterial, TEXT("Vdb Material Rendering"));
DECLARE_GPU_STAT_NAMED(StatVdbShadowDepthMaterial, TEXT("Vdb Shadow Depth Material Rendering"));


FShadowDepthType CSMVdbShadowDepthType(true, false);

void SetupDummyForwardLightUniformParameters(FRDGBuilder& GraphBuilder, FForwardLightData& ForwardLightData)
{
	const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);

	ForwardLightData.DirectionalLightShadowmapAtlas = SystemTextures.Black;
	ForwardLightData.DirectionalLightStaticShadowmap = GBlackTexture->TextureRHI;

	FRDGBufferRef ForwardLocalLightBuffer = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(FVector4f));
	ForwardLightData.ForwardLocalLightBuffer = GraphBuilder.CreateSRV(ForwardLocalLightBuffer, PF_A32B32G32R32F);

	FRDGBufferRef NumCulledLightsGrid = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(uint32));
	ForwardLightData.NumCulledLightsGrid = GraphBuilder.CreateSRV(NumCulledLightsGrid, PF_R32_UINT);

	if (RHISupportsBufferLoadTypeConversion(GMaxRHIShaderPlatform))
	{
		FRDGBufferRef CulledLightDataGrid = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(uint16));
		ForwardLightData.CulledLightDataGrid = GraphBuilder.CreateSRV(CulledLightDataGrid, PF_R16_UINT);
	}
	else
	{
		FRDGBufferRef CulledLightDataGrid = GSystemTextures.GetDefaultBuffer(GraphBuilder, sizeof(uint32));
		ForwardLightData.CulledLightDataGrid = GraphBuilder.CreateSRV(CulledLightDataGrid, PF_R32_UINT);
	}
}

TRDGUniformBufferRef<FForwardLightData> CreateDummyForwardLightUniformBuffer(FRDGBuilder& GraphBuilder)
{
	FForwardLightData* ForwardLightData = GraphBuilder.AllocParameters<FForwardLightData>();
	SetupDummyForwardLightUniformParameters(GraphBuilder, *ForwardLightData);
	return GraphBuilder.CreateUniformBuffer(ForwardLightData);
}

//-----------------------------------------------------------------------------
//--- FVdbMaterialRendering
//-----------------------------------------------------------------------------

FVdbMaterialRendering::FVdbMaterialRendering(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FVdbMaterialRendering::ShouldRenderVolumetricVdb() const
{
	return FVdbCVars::CVarVolumetricVdb.GetValueOnRenderThread() && VertexFactory.IsValid();
}

void FVdbMaterialRendering::InitRendering()
{
	check(IsInRenderingThread());

	ReleaseRendering();
	{
		InitVolumeMesh();
		InitVertexFactory();
		InitDelegate();
	}
}

#define RELEASE_RESOURCE(A) if(A) { A->ReleaseResource(); A.Reset(); }
void FVdbMaterialRendering::ReleaseRendering()
{
	check(IsInRenderingThread());

	ReleaseDelegate();
	RELEASE_RESOURCE(VertexFactory);
	RELEASE_RESOURCE(VertexBuffer);
}
#undef RELEASE_RESOURCE

void FVdbMaterialRendering::Init(UTextureRenderTarget2D* DefaultRenderTarget)
{
	if (IsInRenderingThread())
	{
		DefaultVdbRenderTarget = DefaultRenderTarget;
		InitRendering();
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

void FVdbMaterialRendering::Release()
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

void FVdbMaterialRendering::InitVolumeMesh()
{
	VertexBuffer = MakeUnique<FVolumeMeshVertexBuffer>();
	VertexBuffer->InitResource();
}

void FVdbMaterialRendering::InitVertexFactory()
{
	VertexFactory = MakeUnique<FVolumeMeshVertexFactory>(ERHIFeatureLevel::SM5);
	VertexFactory->Init(VertexBuffer.Get());
}

void FVdbMaterialRendering::InitDelegate()
{
	if (!RenderDelegateHandle.IsValid())
	{
		const FName RendererModuleName("Renderer");
		IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
		if (RendererModule)
		{
			ShadowDepthDelegate.BindRaw(this, &FVdbMaterialRendering::ShadowDepth_RenderThread);
			ShadowDepthDelegateHandle = RendererModule->RegisterShadowDepthRenderDelegate(ShadowDepthDelegate);

			RenderDelegate.BindRaw(this, &FVdbMaterialRendering::Render_RenderThread);
			// Render VDBs before or after Transparent objects
			if (FVdbCVars::CVarVolumetricVdbAfterTransparents.GetValueOnRenderThread())
			{
				RenderDelegateHandle = RendererModule->RegisterOverlayRenderDelegate(RenderDelegate);
			}
			else
			{
				RenderDelegateHandle = RendererModule->RegisterPostOpaqueRenderDelegate(RenderDelegate);
			}
		}
	}
}

void FVdbMaterialRendering::ReleaseDelegate()
{
	if (RenderDelegateHandle.IsValid())
	{
		const FName RendererModuleName("Renderer");
		IRendererModule* RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
		if (RendererModule)
		{
			RendererModule->RemovePostOpaqueRenderDelegate(RenderDelegateHandle);
			RendererModule->RemovePostOpaqueRenderDelegate(ShadowDepthDelegateHandle);
		}

		RenderDelegateHandle.Reset();
		ShadowDepthDelegateHandle.Reset();
	}
}

void FVdbMaterialRendering::CreateMeshBatch(const FSceneView* View, FMeshBatch& MeshBatch, const FVdbMaterialSceneProxy* PrimitiveProxy, FVdbVertexFactoryUserDataWrapper& UserData, const FMaterialRenderProxy* MaterialProxy) const
{
	const FPrimitiveViewRelevance& ViewRelevance = PrimitiveProxy->GetViewRelevance(View);

	MeshBatch.bUseWireframeSelectionColoring = PrimitiveProxy->IsSelected();
	MeshBatch.VertexFactory = VertexFactory.Get();
	MeshBatch.MaterialRenderProxy = MaterialProxy;
	MeshBatch.ReverseCulling = PrimitiveProxy->IsLocalToWorldDeterminantNegative() ^ PrimitiveProxy->IsIndexToLocalDeterminantNegative();
	MeshBatch.Type = PT_TriangleList;
	MeshBatch.DepthPriorityGroup = SDPG_World;
	MeshBatch.bCanApplyViewModeOverrides = true;
	MeshBatch.bUseForMaterial = true;
	MeshBatch.CastShadow = ViewRelevance.bShadowRelevance;
	MeshBatch.bUseForDepthPass = false;


	FMeshBatchElement& BatchElement = MeshBatch.Elements[0];
	BatchElement.PrimitiveUniformBuffer = PrimitiveProxy->GetUniformBuffer();
	BatchElement.IndexBuffer = &VertexBuffer->IndexBuffer;
	BatchElement.FirstIndex = 0;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = VertexBuffer->NumVertices - 1;
	BatchElement.NumPrimitives = VertexBuffer->NumPrimitives;
	BatchElement.VertexFactoryUserData = VertexFactory->GetUniformBuffer();
	BatchElement.UserData = &UserData;
}

void FVdbMaterialRendering::ShadowDepth_RenderThread(FShadowDepthRenderParameters& Parameters)
{
	// TODO LATER: manual culling I guess ?

	const FSceneView* View = static_cast<const FSceneView*>(Parameters.ShadowDepthView);
	const FMatrix& ViewMat = View->ShadowViewMatrices.GetViewMatrix();

	TArray<FVdbMaterialSceneProxy*> OpaqueProxies = VdbProxies.FilterByPredicate([View](const FVdbMaterialSceneProxy* Proxy) { return !Proxy->IsTranslucent()/* && Proxy->IsVisible(View)*/; });
	OpaqueProxies.Sort([ViewMat](const FVdbMaterialSceneProxy& Lhs, const FVdbMaterialSceneProxy& Rhs) -> bool
		{
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z < ViewMat.TransformPosition(RightProxyCenter).Z; // front to back
		});

	TArray<FVdbMaterialSceneProxy*> TranslucentProxies = VdbProxies.FilterByPredicate([View](const FVdbMaterialSceneProxy* Proxy) { return Proxy->IsTranslucent()/* && Proxy->IsVisible(View)*/; });
	TranslucentProxies.Sort([ViewMat](const FVdbMaterialSceneProxy& Lhs, const FVdbMaterialSceneProxy& Rhs) -> bool
		{
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z > ViewMat.TransformPosition(RightProxyCenter).Z; // back to front
		});

	auto DrawVdbProxies = [&](const TArray<FVdbMaterialSceneProxy*>& Proxies, TRDGUniformBufferRef<FVdbDepthShaderParams>& VdbUniformBuffer, bool Translucent)
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
				SCOPED_DRAW_EVENT(RHICmdList, StatVdbShadowDepthMaterial);
				SCOPED_GPU_STAT(RHICmdList, StatVdbShadowDepthMaterial);

				Parameters.ProjectedShadowInfo->SetStateForView(RHICmdList);

				FRHITextureViewCache TexCache;

				for (const FVdbMaterialSceneProxy* Proxy : Proxies)
				{
					if (Proxy && Proxy->GetMaterial() && /*Proxy->IsVisible(&InView) &&*/ Proxy->GetDensityRenderResource())
					{
						DrawDynamicMeshPass(InView, RHICmdList,
							[&](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
							{
								FVdbShadowDepthShaderElementData ShaderElementData;
								ShaderElementData.CustomIntData0 = Proxy->GetCustomIntData0();
								ShaderElementData.CustomIntData1 = Proxy->GetCustomIntData1();
								ShaderElementData.CustomFloatData0 = Proxy->GetCustomFloatData0();
								ShaderElementData.CustomFloatData1 = Proxy->GetCustomFloatData1();
								ShaderElementData.CustomFloatData2 = Proxy->GetCustomFloatData2();
								ShaderElementData.DensityBufferSRV = Proxy->GetDensityRenderResource()->GetBufferSRV();
								ShaderElementData.TemperatureBufferSRV = nullptr;
								ShaderElementData.ColorBufferSRV = nullptr;
								if (!ShaderElementData.DensityBufferSRV)
									return;

								FVdbDepthMeshProcessor PassMeshProcessor(
									InView.Family->Scene->GetRenderScene(),
									&InView,
									DynamicMeshPassContext,
									CSMVdbShadowDepthType,
									EMeshPass::CSMShadowDepth, // TODO: support VSM (no support for masking right now so it's useless to try support VSM)
									Proxy->IsLevelSet(), 
									MoveTemp(ShaderElementData));
								//ShadowInfo->bOnePassPointLightShadow

								FVdbVertexFactoryUserDataWrapper UserData;
								UserData.Data.IndexMin = Proxy->GetIndexMin() - ShaderElementData.CustomFloatData2.Y;
								UserData.Data.IndexSize = Proxy->GetIndexSize() + 2.0 * ShaderElementData.CustomFloatData2.Y;
								UserData.Data.IndexToLocal = Proxy->GetIndexToLocal();

								FMeshBatch VolumeMesh;
								CreateMeshBatch(&InView, VolumeMesh, Proxy, UserData, Proxy->GetMaterial()->GetRenderProxy());

								if (VolumeMesh.CastShadow)
								{
									const uint64 DefaultBatchElementMask = ~0ull; // or 1 << 0; // LOD 0 only
									PassMeshProcessor.AddMeshBatch(VolumeMesh, DefaultBatchElementMask, Proxy);
								}
							}
						);
					}
				}
			});
	};

	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;
	FVdbDepthShaderParams* UniformParameters = GraphBuilder.AllocParameters<FVdbDepthShaderParams>();
	UniformParameters->ShadowClipToTranslatedWorld = Parameters.ProjectedShadowInfo->TranslatedWorldToClipOuterMatrix.Inverse();
	UniformParameters->ShadowPreViewTranslation = FVector3f(Parameters.ProjectedShadowInfo->PreShadowTranslation);
	TRDGUniformBufferRef<FVdbDepthShaderParams> VdbUniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
	
	if (!OpaqueProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbOpaque_RT);
		DrawVdbProxies(OpaqueProxies, VdbUniformBuffer, false);
	}

	if (!TranslucentProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbTranslucent_RT);
		DrawVdbProxies(TranslucentProxies, VdbUniformBuffer, true);
	}
}

void FVdbMaterialRendering::Render_RenderThread(FPostOpaqueRenderParameters& Parameters)
{
	if (!ShouldRenderVolumetricVdb())
		return;

	SCOPE_CYCLE_COUNTER(STAT_VdbRendering_RT);

	RDG_EVENT_SCOPE(*Parameters.GraphBuilder, "Vdb Material Rendering");
	RDG_GPU_STAT_SCOPE(*Parameters.GraphBuilder, StatVdbMaterial);

	const FSceneView* View = static_cast<FSceneView*>(Parameters.Uid);
	const FMatrix& ViewMat = View->ViewMatrices.GetViewMatrix();

	TArray<FVdbMaterialSceneProxy*> OpaqueProxies = VdbProxies.FilterByPredicate([View](const FVdbMaterialSceneProxy* Proxy) { return !Proxy->IsTranslucent() && Proxy->IsVisible(View); });
	OpaqueProxies.Sort([ViewMat](const FVdbMaterialSceneProxy& Lhs, const FVdbMaterialSceneProxy& Rhs) -> bool 
		{ 
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z < ViewMat.TransformPosition(RightProxyCenter).Z; // front to back
		});

	TArray<FVdbMaterialSceneProxy*> TranslucentProxies = VdbProxies.FilterByPredicate([View](const FVdbMaterialSceneProxy* Proxy) { return Proxy->IsTranslucent() && Proxy->IsVisible(View); });
	TranslucentProxies.Sort([ViewMat](const FVdbMaterialSceneProxy& Lhs, const FVdbMaterialSceneProxy& Rhs) -> bool
		{
			const FVector& LeftProxyCenter = Lhs.GetBounds().GetSphere().Center;
			const FVector& RightProxyCenter = Rhs.GetBounds().GetSphere().Center;
			return ViewMat.TransformPosition(LeftProxyCenter).Z > ViewMat.TransformPosition(RightProxyCenter).Z; // back to front
		});

	//const FViewInfo* ViewInfo = static_cast<const FViewInfo*>(View);
	//const FSceneViewFamily* ViewFamily = View->Family;
	//const FScene* Scene = (FScene*)ViewFamily->Scene;
	//FLightSceneInfo* SimpleDirectional = Scene->SimpleDirectionalLight;

	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

	if (!OpaqueProxies.IsEmpty())
	{
		SCOPE_CYCLE_COUNTER(STAT_VdbOpaque_RT);
		//DrawVdbProxies(OpaqueProxies, false, VdbUniformBuffer, nullptr, nullptr);
		for (const FVdbMaterialSceneProxy* Proxy : OpaqueProxies)
		{
			RenderLights(Proxy, false, Parameters, nullptr, nullptr);
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
		}

		//DrawVdbProxies(TranslucentProxies, true, VdbUniformBuffer, VdbCurrRenderTexture, DepthTestTexture);
		for (const FVdbMaterialSceneProxy* Proxy : TranslucentProxies)
		{
			RenderLights(Proxy, true, Parameters, VdbCurrRenderTexture, DepthTestTexture);
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

void FVdbMaterialRendering::RenderLights(
	// Object Data
	const FVdbMaterialSceneProxy* Proxy,
	bool Translucent, 
	// Scene data
	const FPostOpaqueRenderParameters& Parameters,
	FRDGTexture* RenderTexture, 
	FRDGTexture* DepthRenderTexture)
{
	const FSceneView* View = static_cast<const FSceneView*>(Parameters.View);
	//const FViewInfo* ViewInfo = static_cast<const FViewInfo*>(View);
	const FSceneViewFamily* ViewFamily = View->Family;
	const FScene* Scene = (FScene*)ViewFamily->Scene;

	if (!Proxy || !Proxy->GetMaterial() || !Proxy->IsVisible(View) || !Proxy->GetDensityRenderResource())
		return;

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
	int32 NumPasses = FMath::Max(LightSceneInfoCompact.Num(), 1);
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
			if (LightSceneInfo)
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
			RenderTexture,
			DepthRenderTexture
		);
	}
}

void SetupRenderPassParameters(
	FRDGBuilder& GraphBuilder,
	FVdbShaderParametersPS* PassParameters,
	// Light data
	bool ApplyEmissionAndTransmittance,
	bool ApplyDirectLighting,
	bool ApplyShadowTransmittance,
	uint32 LightType,
	FLightSceneInfo* LightSceneInfo,
	const FVisibleLightInfo* VisibleLightInfo,
	// Scene data
	const FPostOpaqueRenderParameters& Parameters,
	const FViewInfo* ViewInfo
)
{
	FVdbShaderParams* UniformParameters = GraphBuilder.AllocParameters<FVdbShaderParams>();

	// Scene data
	UniformParameters->SceneDepthTexture = Parameters.DepthTexture;
	UniformParameters->LinearTexSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	// Vdb data
	UniformParameters->Threshold = FMath::Max(0.0, FVdbCVars::CVarVolumetricVdbThreshold.GetValueOnAnyThread());

	// Light data
	FDeferredLightUniformStruct DeferredLightUniform;
	UniformParameters->bApplyEmissionAndTransmittance = ApplyEmissionAndTransmittance;
	UniformParameters->bApplyDirectLighting = ApplyDirectLighting;
	UniformParameters->bApplyShadowTransmittance = ApplyShadowTransmittance;
	UniformParameters->LightType = LightType;
	if (ApplyDirectLighting && (LightSceneInfo != nullptr))
	{
		DeferredLightUniform = GetDeferredLightParameters(*ViewInfo, *LightSceneInfo);
		UniformParameters->VolumetricScatteringIntensity = LightSceneInfo->Proxy->GetVolumetricScatteringIntensity();
	}
	PassParameters->DeferredLight = CreateUniformBufferImmediate(DeferredLightUniform, UniformBuffer_SingleDraw);

	// Shadow data
	UniformParameters->ShadowStepSize = 8.0;// todo make a variable
	UniformParameters->ForwardLightData = *ViewInfo->ForwardLightingResources.ForwardLightData;
	if (VisibleLightInfo != nullptr)
	{
		const FProjectedShadowInfo* ProjectedShadowInfo = GetShadowForInjectionIntoVolumetricFog(*VisibleLightInfo);
		bool bDynamicallyShadowed = ProjectedShadowInfo != NULL;
		if (bDynamicallyShadowed)
		{
			GetVolumeShadowingShaderParameters(GraphBuilder, *ViewInfo, LightSceneInfo, ProjectedShadowInfo, PassParameters->VolumeShadowingShaderParameters);
		}
		else
		{
			SetVolumeShadowingDefaultShaderParametersGlobal(GraphBuilder, PassParameters->VolumeShadowingShaderParameters);
		}
		UniformParameters->VirtualShadowMapId = VisibleLightInfo->GetVirtualShadowMapId(ViewInfo);
	}
	else
	{
		SetVolumeShadowingDefaultShaderParametersGlobal(GraphBuilder, PassParameters->VolumeShadowingShaderParameters);
	}
	PassParameters->VirtualShadowMapSamplingParameters = Parameters.VirtualShadowMapArray->GetSamplingParameters(GraphBuilder);

	// Indirect lighting data
	auto* LumenUniforms = GraphBuilder.AllocParameters<FLumenTranslucencyLightingUniforms>();
	LumenUniforms->Parameters = GetLumenTranslucencyLightingParameters(GraphBuilder, ViewInfo->LumenTranslucencyGIVolume, ViewInfo->LumenFrontLayerTranslucency);
	PassParameters->LumenGIVolumeStruct = GraphBuilder.CreateUniformBuffer(LumenUniforms);

	// Pass params
	PassParameters->DeferredLight = CreateUniformBufferImmediate(DeferredLightUniform, UniformBuffer_SingleDraw);
	PassParameters->View = ViewInfo->ViewUniformBuffer;
	PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);

	// Finalize VdbUniformBuffer
	TRDGUniformBufferRef<FVdbShaderParams> VdbUniformBuffer = GraphBuilder.CreateUniformBuffer(UniformParameters);
	PassParameters->VdbUniformBuffer = VdbUniformBuffer;
}

void FVdbMaterialRendering::RenderLight(
	// Object data
	const FVdbMaterialSceneProxy* Proxy,
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
	FRDGTexture* RenderTexture,
	FRDGTexture* DepthRenderTexture)
{
	const FSceneView* View = static_cast<FSceneView*>(Parameters.Uid);
	const FViewInfo* ViewInfo = static_cast<const FViewInfo*>(View);

	FRDGBuilder& GraphBuilder = *Parameters.GraphBuilder;

	FVdbShaderParametersPS* PassParameters = GraphBuilder.AllocParameters<FVdbShaderParametersPS>();
	SetupRenderPassParameters(
		GraphBuilder,
		PassParameters, 
		ApplyEmissionAndTransmittance, 
		ApplyDirectLighting, 
		ApplyShadowTransmittance, 
		LightType,
		LightSceneInfo,
		VisibleLightInfo,
		Parameters,
		ViewInfo);

	// Render Targets
	bool bWriteDepth = DepthRenderTexture != nullptr;
	if (RenderTexture)
	{
		PassParameters->RenderTargets[0] = FRenderTargetBinding(RenderTexture, ERenderTargetLoadAction::EClear);
		// Don't bind depth buffer, we will read it in Pixel Shader instead
		if (bWriteDepth)
		{
			PassParameters->RenderTargets.DepthStencil =
				FDepthStencilBinding(DepthRenderTexture, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilNop);
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
		[this, &InView = *View, ViewportRect = Parameters.ViewportRect, Proxy, bWriteDepth](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENT(RHICmdList, StatVdbMaterial);
			SCOPED_GPU_STAT(RHICmdList, StatVdbMaterial);

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
					ShaderElementData.DensityBufferSRV = Proxy->GetDensityRenderResource()->GetBufferSRV();
					ShaderElementData.TemperatureBufferSRV = Proxy->GetTemperatureRenderResource() ? Proxy->GetTemperatureRenderResource()->GetBufferSRV() : nullptr;
					ShaderElementData.ColorBufferSRV = Proxy->GetColorRenderResource() ? Proxy->GetColorRenderResource()->GetBufferSRV() : nullptr;
					if (!ShaderElementData.DensityBufferSRV)
						return;

					FTexture* CurveAtlas = Proxy->GetBlackbodyAtlasResource();
					FTextureRHIRef CurveAtlasRHI = CurveAtlas ? CurveAtlas->GetTextureRHI() : nullptr;
					ShaderElementData.BlackbodyColorSRV = CurveAtlasRHI ? TexCache.GetOrCreateSRV(CurveAtlasRHI, FRHITextureSRVCreateInfo()) : GBlackTextureWithSRV->ShaderResourceViewRHI;

					FVdbMeshProcessor PassMeshProcessor(
						InView.Family->Scene->GetRenderScene(),
						&InView,
						DynamicMeshPassContext,
						Proxy->IsLevelSet(), Proxy->IsTranslucentLevelSet(),
						false,
						Proxy->UseImprovedSkylight(),
						Proxy->UseTrilinearSampling() || FVdbCVars::CVarVolumetricVdbTrilinear.GetValueOnRenderThread(),
						bWriteDepth,
						ShaderElementData.TemperatureBufferSRV != nullptr,
						ShaderElementData.ColorBufferSRV != nullptr,
						MoveTemp(ShaderElementData));

					FVdbVertexFactoryUserDataWrapper UserData;
					UserData.Data.IndexMin = Proxy->GetIndexMin() - ShaderElementData.CustomFloatData2.Y;
					UserData.Data.IndexSize = Proxy->GetIndexSize() + 2.0 * ShaderElementData.CustomFloatData2.Y;
					UserData.Data.IndexToLocal = Proxy->GetIndexToLocal();

					FMeshBatch VolumeMesh;
					CreateMeshBatch(&InView, VolumeMesh, Proxy, UserData, Proxy->GetMaterial()->GetRenderProxy());

					const uint64 DefaultBatchElementMask = ~0ull; // or 1 << 0; // LOD 0 only
					PassMeshProcessor.AddMeshBatch(VolumeMesh, DefaultBatchElementMask, Proxy);
				}
			);
			
		}
	);
}


void FVdbMaterialRendering::AddVdbProxy(FVdbMaterialSceneProxy* Proxy)
{
	ENQUEUE_RENDER_COMMAND(FAddVdbProxyCommand)(
		[this, Proxy](FRHICommandListImmediate& RHICmdList)
		{
			check(VdbProxies.Find(Proxy) == INDEX_NONE);
			VdbProxies.Emplace(Proxy);
		});
}

void FVdbMaterialRendering::RemoveVdbProxy(FVdbMaterialSceneProxy* Proxy)
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

void FVdbMaterialRendering::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	// Reset visibility on all registered FVdbProxies, before SceneVisibility is computed 
	for (FVdbMaterialSceneProxy* Proxy : VdbProxies)
	{
		Proxy->ResetVisibility();
		Proxy->UpdateCurveAtlasTex();
	}
}

// Called on game thread when view family is about to be rendered.
void FVdbMaterialRendering::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
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
