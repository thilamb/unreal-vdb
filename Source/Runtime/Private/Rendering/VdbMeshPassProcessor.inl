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

//#include "VdbMaterialRendering.h"
#include "VdbShaders.h"
//#include "VdbMaterialSceneProxy.h"
//#include "VdbRenderBuffer.h"
//#include "VdbCommon.h"  
//#include "VdbComposite.h"
//#include "VolumeMesh.h"
//#include "SceneTexturesConfig.h"
//#include "SystemTextures.h"
//
//#include "LocalVertexFactory.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "ScenePrivate.h"
//#include "Modules\ModuleManager.h"
//#include "SceneView.h"
//#include "ScenePrivate.h"
//#include "RenderUtils.h"
//
//#include "VolumeLighting.h"
//#include "VolumetricFog.h"

//-----------------------------------------------------------------------------
//--- FVdbDepthMeshProcessor
//-----------------------------------------------------------------------------


class FVdbDepthMeshProcessor : public FMeshPassProcessor
{
public:
	FVdbDepthMeshProcessor(
		const FScene* Scene,
		const FSceneView* InView,
		FMeshPassDrawListContext* InDrawListContext,
		FShadowDepthType InShadowDepthType,
		EMeshPass::Type InMeshPassTargetType,
		bool IsLevelSet,
		FVdbShadowDepthShaderElementData&& ShaderElementData)
		: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InView, InDrawListContext)
		, VdbShaderElementData(ShaderElementData)
		, FeatureLevel(Scene->GetFeatureLevel())
		, ShadowDepthType(InShadowDepthType)
		, MeshPassTargetType(InMeshPassTargetType)
		, bLevelSet(IsLevelSet)
	{
		// Depth stuff
		EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(FeatureLevel);
		if (UseNonNaniteVirtualShadowMaps(ShaderPlatform, FeatureLevel))
		{
			// set up mesh filtering.
			MeshSelectionMask = MeshPassTargetType == EMeshPass::VSMShadowDepth ? EShadowMeshSelection::VSM : EShadowMeshSelection::SM;
		}
		else
		{
			// If VSMs are disabled, pipe all kinds of draws into the regular SMs
			MeshSelectionMask = EShadowMeshSelection::All;
		}
		
		// Disable color writes
		PassDrawRenderState.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());

		if (MeshPassTargetType == EMeshPass::VSMShadowDepth)
		{
			PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		}
		else if (ShadowDepthType.bOnePassPointLightShadow || MeshPassTargetType == EMeshPass::VSMShadowDepth)
		{
			// Point lights use reverse Z depth maps
			PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		}
		else
		{
			PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_LessEqual>::GetRHI());
		}
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

		if (Material && Material->GetMaterialDomain() == MD_Volume && Material->GetRenderingThreadShaderMap())
		{
			const ERasterizerFillMode MeshFillMode = FM_Solid;
			const ERasterizerCullMode MeshCullMode = MeshBatch.ReverseCulling ? CM_CW : CM_CCW;

			Process(MeshBatch, BatchElementMask, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, StaticMeshId, MeshFillMode, MeshCullMode);
		}
	}

private:

	//template<typename VertexShaderType, typename PixelShaderType>
	bool GetPassShaders(
		const FMaterial& Material,
		FVertexFactoryType* VertexFactoryType,
		TShaderRef<FVdbShadowDepthVS>& VertexShader,
		TShaderRef<FVdbShadowDepthPS>& PixelShader
	)
	{
		// Use perspective correct shadow depths for shadow types which typically render low poly meshes into the shadow depth buffer.
		// Depth will be interpolated to the pixel shader and written out, which disables HiZ and double speed Z.
		// Directional light shadows use an ortho projection and can use the non-perspective correct path without artifacts.
		// One pass point lights don't output a linear depth, so they are already perspective correct.
		bool bUsePerspectiveCorrectShadowDepths = !ShadowDepthType.bDirectionalLight && !ShadowDepthType.bOnePassPointLightShadow;
		bool bOnePassPointLightShadow = ShadowDepthType.bOnePassPointLightShadow;

		bool bVirtualShadowMap = MeshPassType == EMeshPass::VSMShadowDepth;
		if (bVirtualShadowMap)
		{
			bUsePerspectiveCorrectShadowDepths = false;
			bOnePassPointLightShadow = false;
		}

		// Fetch corresponding shaders
		FMaterialShaderTypes ShaderTypes;

		// Vertex shaders
		if (bOnePassPointLightShadow) {
			if (DoesRuntimeSupportOnePassPointLightShadows(GShaderPlatformForFeatureLevel[FeatureLevel])) {
				ShaderTypes.AddShaderType<FVdbShadowDepthVS_OnePassPointLight>();
			}
			else {
				return false;
			}
		}
		else if (bVirtualShadowMap) {
			ShaderTypes.AddShaderType<FVdbShadowDepthVS_VirtualShadowMap>();
		}
		else if (bUsePerspectiveCorrectShadowDepths) {
			ShaderTypes.AddShaderType<FVdbShadowDepthVS_PerspectiveCorrect>();
		}
		else {
			ShaderTypes.AddShaderType<FVdbShadowDepthVS_OutputDepth>();
		}

		// Pixel shaders
		if (bLevelSet)
		{
			if (bVirtualShadowMap)
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_VirtualShadowMap_LevelSet>();
			else if (bUsePerspectiveCorrectShadowDepths)
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_PerspectiveCorrect_LevelSet>();
			else if (bOnePassPointLightShadow)
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_OnePassPointLight_LevelSet>();
			else
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_NonPerspectiveCorrecth_LevelSet>();
		}
		else
		{
			if (bVirtualShadowMap)
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_VirtualShadowMap_FogVolume>();
			else if (bUsePerspectiveCorrectShadowDepths)
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_PerspectiveCorrect_FogVolume>();
			else if (bOnePassPointLightShadow)
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_OnePassPointLight_FogVolume>();
			else
				ShaderTypes.AddShaderType<FVdbShadowDepthPS_NonPerspectiveCorrecth_FogVolume>();
		}

		// Finalize
		FMaterialShaders Shaders;
		if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			return false;
		}

		Shaders.TryGetVertexShader(VertexShader);
		Shaders.TryGetPixelShader(PixelShader);

		return VertexShader.IsValid() && PixelShader.IsValid();
	}

	//template<typename VertexShaderType, typename PixelShaderType>
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		int32 StaticMeshId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode)
	{
		VdbShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders<FVdbShadowDepthVS, FVdbShadowDepthPS> PassShaders;
		if (!GetPassShaders(
			MaterialResource,
			VertexFactory->GetType(),
			PassShaders.VertexShader,
			PassShaders.PixelShader))
		{
			return;
		}

		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);
		
		const bool bUseGpuSceneInstancing = false;// UseGPUScene(GShaderPlatformForFeatureLevel[FeatureLevel], FeatureLevel) && VertexFactory->SupportsGPUScene(FeatureLevel);
		// Need to replicate for cube faces on host if GPU-scene is not available (for this draw).
		const bool bPerformHostCubeFaceReplication = ShadowDepthType.bOnePassPointLightShadow && !bUseGpuSceneInstancing;
		const uint32 InstanceFactor = bPerformHostCubeFaceReplication ? 6 : 1;

		for (uint32 i = 0; i < InstanceFactor; i++)
		{
			VdbShaderElementData.LayerId = i;
			VdbShaderElementData.bUseGpuSceneInstancing = bUseGpuSceneInstancing;

			BuildMeshDrawCommands(
				MeshBatch,
				BatchElementMask,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				MaterialResource,
				PassDrawRenderState,
				PassShaders,
				MeshFillMode,
				MeshCullMode,
				SortKey,
				EMeshPassFeatures::Default,
				VdbShaderElementData);
		}
	}

	FMeshPassProcessorRenderState PassDrawRenderState;
	FVdbShadowDepthShaderElementData VdbShaderElementData;
	ERHIFeatureLevel::Type FeatureLevel;
	FShadowDepthType ShadowDepthType;
	EMeshPass::Type MeshPassTargetType = EMeshPass::CSMShadowDepth;
	EShadowMeshSelection MeshSelectionMask = EShadowMeshSelection::All;
	bool bLevelSet;
};

//-----------------------------------------------------------------------------
//--- FVdbMeshProcessor
//-----------------------------------------------------------------------------

class FVdbMeshProcessor : public FMeshPassProcessor
{
public:
	FVdbMeshProcessor(
		const FScene* Scene,
		const FSceneView* InView,
		FMeshPassDrawListContext* InDrawListContext,
		bool IsLevelSet, bool IsTranslucentLevelSet,
		bool ImprovedSkylight,
		bool TrilinearSampling,
		bool WriteDepth,
		bool FirstLight,
		bool UseTempVdb, bool UseColorVdb,
		FVdbElementData&& ShaderElementData)
		: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InView, InDrawListContext)
		, VdbShaderElementData(ShaderElementData)
		, bLevelSet(IsLevelSet)
		, bTranslucentLevelSet(IsTranslucentLevelSet)
		, bImprovedSkylight(ImprovedSkylight)
		, bTrilinearSampling(TrilinearSampling)
		, bTemperatureVdb(UseTempVdb)
		, bColorVdb(UseColorVdb)
	{
		if (bLevelSet && !bTranslucentLevelSet)
		{
			PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
			PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		}
		else
		{
			if (FirstLight)
			{
			    // premultiplied alpha blending
				PassDrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI());
			}
			else
			{
			    // just add light contribution
				PassDrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI());
			}

			if (WriteDepth)
			{
				PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
			}
			else
			{
				PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
			}

		}

		int32 CinematicMode = FVdbCVars::CVarVolumetricVdbCinematicQuality.GetValueOnAnyThread();
		if (CinematicMode == 1)
		{
			VdbShaderElementData.CustomFloatData0[0] /= 4.f; // local step size
			VdbShaderElementData.CustomFloatData0[1] = FMath::Max(1.f, VdbShaderElementData.CustomFloatData0[1] / 4.f); // local shadow step size
			VdbShaderElementData.CustomIntData0[0] *= 2; // Max number of steps
			VdbShaderElementData.CustomIntData0[1] *= 2; // Samples per pixels
		}
		else if (CinematicMode == 2)
		{
			VdbShaderElementData.CustomFloatData0[0] /= 10.f; // local step size
			VdbShaderElementData.CustomFloatData0[1] = FMath::Max(1.f, VdbShaderElementData.CustomFloatData0[1] / 10.f); // local shadow step size
			VdbShaderElementData.CustomIntData0[0] *= 4; // Max number of steps
			VdbShaderElementData.CustomIntData0[1] *= 4; // Samples per pixels
			bTrilinearSampling = true;
		}
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

		if (Material && Material->GetMaterialDomain() == MD_Volume && Material->GetRenderingThreadShaderMap())
		{
			const ERasterizerFillMode MeshFillMode = FM_Solid;
			const ERasterizerCullMode MeshCullMode = MeshBatch.ReverseCulling ? CM_CW : CM_CCW;

#define PROCESS_SHADER(shader) { Process<FVdbShaderVS, ##shader>(MeshBatch, BatchElementMask, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, StaticMeshId, MeshFillMode, MeshCullMode); }

			if (bLevelSet)
			{
				if (bTranslucentLevelSet && bImprovedSkylight)
					PROCESS_SHADER(FVdbShaderPS_LevelSet_Translucent_EnvLight)
				else if (bTranslucentLevelSet)
					PROCESS_SHADER(FVdbShaderPS_LevelSet_Translucent)
				else
					PROCESS_SHADER(FVdbShaderPS_LevelSet)
			}
			else
			{
				// combination of 4 params: 2^4 = 16 different cases
				// TODO: this is getting ridiculous, find better solution
				if (!bTemperatureVdb && !bColorVdb && !bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume)
				else if (!bTemperatureVdb && !bColorVdb && !bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Trilinear)
				else if (!bTemperatureVdb && !bColorVdb && bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_EnvLight)
				else if (!bTemperatureVdb && !bColorVdb && bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_EnvLight_Trilinear)
				else if (!bTemperatureVdb && bColorVdb && !bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Color)
				else if (!bTemperatureVdb && bColorVdb && !bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Color_Trilinear)
				else if (!bTemperatureVdb && bColorVdb && bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Color_EnvLight)
				else if (!bTemperatureVdb && bColorVdb && bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Color_EnvLight_Trilinear)
				else if (bTemperatureVdb && !bColorVdb && !bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody)
				else if (bTemperatureVdb && !bColorVdb && !bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody_Trilinear)
				else if (bTemperatureVdb && !bColorVdb && bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody_EnvLight)
				else if (bTemperatureVdb && !bColorVdb && bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody_EnvLight_Trilinear)
				else if (bTemperatureVdb && bColorVdb && !bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody_Color)
				else if (bTemperatureVdb && bColorVdb && !bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody_Color_Trilinear)
				else if (bTemperatureVdb && bColorVdb && bImprovedSkylight && !bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight)
				else if (bTemperatureVdb && bColorVdb && bImprovedSkylight && bTrilinearSampling)
					PROCESS_SHADER(FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight_Trilinear)
			}
		}
	}

private:

	template<typename VertexShaderType, typename PixelShaderType>
	bool GetPassShaders(
		const FMaterial& Material,
		FVertexFactoryType* VertexFactoryType,
		TShaderRef<VertexShaderType>& VertexShader,
		TShaderRef<PixelShaderType>& PixelShader
	)
	{
		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<VertexShaderType>();
		ShaderTypes.AddShaderType<PixelShaderType>();

		FMaterialShaders Shaders;
		if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
		{
			return false;
		}

		Shaders.TryGetVertexShader(VertexShader);
		Shaders.TryGetPixelShader(PixelShader);

		return VertexShader.IsValid() && PixelShader.IsValid();
	}

	template<typename VertexShaderType, typename PixelShaderType>
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		int32 StaticMeshId,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode)
	{
		VdbShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders<VertexShaderType, PixelShaderType> PassShaders;
		if (!GetPassShaders(
			MaterialResource,
			VertexFactory->GetType(),
			PassShaders.VertexShader,
			PassShaders.PixelShader))
		{
			return;
		}

		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);
		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			PassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			VdbShaderElementData);
	}

	FMeshPassProcessorRenderState PassDrawRenderState;
	FVdbElementData VdbShaderElementData;
	bool bLevelSet;
	bool bTranslucentLevelSet;
	bool bImprovedSkylight;
	bool bTrilinearSampling;
	bool bTemperatureVdb;
	bool bColorVdb;
};
