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

#include "VdbCommon.h"
#include "MeshMaterialShader.h"
#include "InstanceCulling/InstanceCullingMergedContext.h"
#include "VirtualShadowMaps/VirtualShadowMapArray.h"
#include "SceneRendering.h"
#include "VolumeLighting.h"

THIRD_PARTY_INCLUDES_START
#include <nanovdb/NanoVDB.h>
THIRD_PARTY_INCLUDES_END

namespace VdbShaders
{
	static bool IsSupportedVertexFactoryType(const FVertexFactoryType* VertexFactoryType)
	{
		static FName VdbVfName = FName(TEXT("FVolumeMeshVertexFactory"), FNAME_Find);
		return VertexFactoryType == FindVertexFactoryType(VdbVfName);
	}
}

struct FVdbElementData : public FMeshMaterialShaderElementData
{
	FIntVector4 CustomIntData0; // x: MaxRayDepth, y: SamplesPerPixel, z: colored transmittance, w: temporal noise
	FIntVector4 CustomIntData1; // x: BlackbodyCurveIndex, y: CurveAtlaHeight, z: TranslucentLevelSet, w: unused
	FVector4f CustomFloatData0; // x: Local step size, y: Shadow step size mutliplier, z: voxel size, w: jittering
	FVector4f CustomFloatData1; // x: anisotropy, y: albedo, z: blackbody intensity, w: blackbody temperature
	FVector4f CustomFloatData2; // x: density mul, y: padding, z: ambient, w: shadow threshold
	FShaderResourceViewRHIRef DensityBufferSRV;
	FShaderResourceViewRHIRef TemperatureBufferSRV;
	FShaderResourceViewRHIRef ColorBufferSRV;
	FShaderResourceViewRHIRef BlackbodyColorSRV;
};

class FVdbShaderVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FVdbShaderVS , MeshMaterial);

	FVdbShaderVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	FVdbShaderVS() = default;

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Volume &&
			FMeshMaterialShader::ShouldCompilePermutation(Parameters) &&
			VdbShaders::IsSupportedVertexFactoryType(Parameters.VertexFactoryType);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVdbShaderParams, )
	// Scene data
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, LinearTexSampler)
	// Vdb data
	SHADER_PARAMETER(float, Threshold)
	// Light data
	SHADER_PARAMETER(int, bApplyEmissionAndTransmittance)
	SHADER_PARAMETER(int, bApplyDirectLighting)
	SHADER_PARAMETER(int, bApplyShadowTransmittance)
	SHADER_PARAMETER(int, LightType)
	SHADER_PARAMETER_STRUCT_INCLUDE(FDeferredLightUniformStruct, DeferredLight)
	// Shadow data
	SHADER_PARAMETER_STRUCT(FForwardLightData, ForwardLightData)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParameters, VolumeShadowingShaderParameters)
	SHADER_PARAMETER(int32, VirtualShadowMapId)
	// Indirect Lighting
	SHADER_PARAMETER_STRUCT_INCLUDE(FLumenTranslucencyLightingParameters, LumenGIVolumeStruct)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


class FVdbShaderPS : public FMeshMaterialShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FVdbShaderPS, NonVirtual);

	LAYOUT_FIELD(FShaderResourceParameter, DensityVdbBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, TemperatureVdbBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, ColorVdbBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, BlackbodyColor);
	LAYOUT_FIELD(FShaderParameter, CustomIntData0);
	LAYOUT_FIELD(FShaderParameter, CustomIntData1);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData0);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData1);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData2);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Rant: I still don't really understand how these structs work. As far as I understand:
		// Here you can only bind Uniform Buffers. All other options will not be passed to shader (even if some will compile)
		// If Uniform buffer is static it will "just work" (I think). If not, you need to add a LAYOUT_FIELD.
		// Most Uniform buffers have a non-uniform buffer form, usually just a struct that can be included/nested in a sub-uniform buffer.
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVdbShaderParams, VdbUniformBuffer)
		// THIS ONE PARTICULARLY REQUIRES STATIC UBO BINDING, so let's do it
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
		// Render targets
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	FVdbShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		DensityVdbBuffer.Bind(Initializer.ParameterMap, TEXT("DensityVdbBuffer"));
		TemperatureVdbBuffer.Bind(Initializer.ParameterMap, TEXT("TemperatureVdbBuffer"));
		ColorVdbBuffer.Bind(Initializer.ParameterMap, TEXT("ColorVdbBuffer"));
		BlackbodyColor.Bind(Initializer.ParameterMap, TEXT("BlackbodyColor"));
		CustomIntData0.Bind(Initializer.ParameterMap, TEXT("CustomIntData0"));
		CustomIntData1.Bind(Initializer.ParameterMap, TEXT("CustomIntData1"));
		CustomFloatData0.Bind(Initializer.ParameterMap, TEXT("CustomFloatData0"));
		CustomFloatData1.Bind(Initializer.ParameterMap, TEXT("CustomFloatData1"));
		CustomFloatData2.Bind(Initializer.ParameterMap, TEXT("CustomFloatData2"));

		PassUniformBuffer.Bind(Initializer.ParameterMap, FVdbShaderParams::StaticStructMetadata.GetShaderVariableName());
	}

	FVdbShaderPS() {}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FVdbShaderVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_FORCE_TEXTURE_MIP"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MAJOR"), NANOVDB_MAJOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MINOR"), NANOVDB_MINOR_VERSION_NUMBER);

		bool bSupportVirtualShadowMap = IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		if (bSupportVirtualShadowMap)
		{
			OutEnvironment.SetDefine(TEXT("VIRTUAL_SHADOW_MAP"), 1);
			FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		}

		// This shader takes a very long time to compile with FXC, so we pre-compile it with DXC first and then forward the optimized HLSL to FXC.
		OutEnvironment.CompilerFlags.Add(CFLAG_PrecompileWithDXC);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FVdbElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(DensityVdbBuffer, ShaderElementData.DensityBufferSRV);
		ShaderBindings.Add(TemperatureVdbBuffer, ShaderElementData.TemperatureBufferSRV);
		ShaderBindings.Add(ColorVdbBuffer, ShaderElementData.ColorBufferSRV);
		ShaderBindings.Add(BlackbodyColor, ShaderElementData.BlackbodyColorSRV);
		ShaderBindings.Add(CustomIntData0, ShaderElementData.CustomIntData0);
		ShaderBindings.Add(CustomIntData1, ShaderElementData.CustomIntData1);
		ShaderBindings.Add(CustomFloatData0, ShaderElementData.CustomFloatData0);
		ShaderBindings.Add(CustomFloatData1, ShaderElementData.CustomFloatData1);
		ShaderBindings.Add(CustomFloatData2, ShaderElementData.CustomFloatData2);
	}
};

template<bool IsLevelSet, bool UseTemperatureBuffer, bool UseColorBuffer, bool NicerEnvLight, bool Trilinear>
class TVdbShaderPS : public FVdbShaderPS
{
	DECLARE_SHADER_TYPE(TVdbShaderPS, MeshMaterial);

	TVdbShaderPS() = default;
	TVdbShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FVdbShaderPS(Initializer)
	{
		BindForLegacyShaderParameters<FParameters>(this, Initializer.PermutationId, Initializer.ParameterMap, false);
	}

	void SetParameters(
		FRHIComputeCommandList& RHICmdList,
		FRHIComputeShader* ShaderRHI,
		const FViewInfo& View,
		const FMaterialRenderProxy* MaterialProxy,
		const FMaterial& Material
	)
	{
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialProxy, Material, View);
	}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FVdbShaderPS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVdbShaderPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("VDB_LEVEL_SET"), IsLevelSet);
		OutEnvironment.SetDefine(TEXT("USE_TEMPERATURE_VDB"), UseTemperatureBuffer);
		OutEnvironment.SetDefine(TEXT("USE_COLOR_VDB"), UseColorBuffer);
		OutEnvironment.SetDefine(TEXT("NICER_BUT_EXPENSIVE_ENVLIGHT"), NicerEnvLight);
		OutEnvironment.SetDefine(TEXT("USE_TRILINEAR_SAMPLING"), Trilinear);
	}
};

// TODO: this is getting ridiculous. Find other solution
typedef TVdbShaderPS<true, false, false, false, false> FVdbShaderPS_LevelSet;
typedef TVdbShaderPS<true, true, false, false, false> FVdbShaderPS_LevelSet_Translucent; // reusing USE_TEMPERATURE_VDB variation for translucency to avoid another variation
typedef TVdbShaderPS<true, true, false, true, false> FVdbShaderPS_LevelSet_Translucent_EnvLight; // reusing USE_TEMPERATURE_VDB variation for translucency to avoid another variation
typedef TVdbShaderPS<false, false, false, false, false>  FVdbShaderPS_FogVolume;
typedef TVdbShaderPS<false, false, false, false, true>  FVdbShaderPS_FogVolume_Trilinear;
typedef TVdbShaderPS<false, false, false, true, false>  FVdbShaderPS_FogVolume_EnvLight;
typedef TVdbShaderPS<false, false, false, true, true>  FVdbShaderPS_FogVolume_EnvLight_Trilinear;
typedef TVdbShaderPS<false, false, true, false, false>  FVdbShaderPS_FogVolume_Color;
typedef TVdbShaderPS<false, false, true, false, true>  FVdbShaderPS_FogVolume_Color_Trilinear;
typedef TVdbShaderPS<false, false, true, true, false>  FVdbShaderPS_FogVolume_Color_EnvLight;
typedef TVdbShaderPS<false, false, true, true, true>  FVdbShaderPS_FogVolume_Color_EnvLight_Trilinear;
typedef TVdbShaderPS<false, true, false, false, false>  FVdbShaderPS_FogVolume_Blackbody;
typedef TVdbShaderPS<false, true, false, false, true>  FVdbShaderPS_FogVolume_Blackbody_Trilinear;
typedef TVdbShaderPS<false, true, false, true, false>  FVdbShaderPS_FogVolume_Blackbody_EnvLight;
typedef TVdbShaderPS<false, true, false, true, true>  FVdbShaderPS_FogVolume_Blackbody_EnvLight_Trilinear;
typedef TVdbShaderPS<false, true, true, false, false>  FVdbShaderPS_FogVolume_Blackbody_Color;
typedef TVdbShaderPS<false, true, true, false, true>  FVdbShaderPS_FogVolume_Blackbody_Color_Trilinear;
typedef TVdbShaderPS<false, true, true, true, false>  FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight;
typedef TVdbShaderPS<false, true, true, true, true>  FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight_Trilinear;

//-----------------------------------------------------------------------------

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVdbDepthShaderParams, )
	SHADER_PARAMETER(FMatrix44f, ShadowClipToTranslatedWorld)
	SHADER_PARAMETER(FVector3f, ShadowPreViewTranslation)
	END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FVdbShadowDepthPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	//SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileShadowDepthPassUniformParameters, MobilePassUniformBuffer)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FShadowDepthPassUniformParameters, DeferredPassUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
	//SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
	//SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVdbDepthShaderParams, VdbUniformBuffer)

	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FVdbShadowDepthShaderElementData : public FVdbElementData
{
public:
	int32 LayerId;
	int32 bUseGpuSceneInstancing;
};

/**
* A vertex shader for rendering the depth of a mesh.
*/
class FVdbShadowDepthVS : public FMeshMaterialShader
{
public:
	DECLARE_INLINE_TYPE_LAYOUT(FVdbShadowDepthVS, NonVirtual);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return false;
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FVdbShadowDepthShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(LayerId, ShaderElementData.LayerId);
		ShaderBindings.Add(bUseGpuSceneInstancing, ShaderElementData.bUseGpuSceneInstancing);
	}
	
	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MAJOR"), NANOVDB_MAJOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MINOR"), NANOVDB_MINOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_MASKED"), TEXT("1"));
	}

	FVdbShadowDepthVS() = default;
	FVdbShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		LayerId.Bind(Initializer.ParameterMap, TEXT("LayerId"));
		bUseGpuSceneInstancing.Bind(Initializer.ParameterMap, TEXT("bUseGpuSceneInstancing"));
	}

private:
	LAYOUT_FIELD(FShaderParameter, LayerId);
	LAYOUT_FIELD(FShaderParameter, bUseGpuSceneInstancing);
};

enum EVdbShadowDepthVertexShaderMode
{
	VertexShadowDepth_PerspectiveCorrect,
	VertexShadowDepth_OutputDepth,
	VertexShadowDepth_OnePassPointLight,
	VertexShadowDepth_VirtualShadowMap,
};

template <EVdbShadowDepthVertexShaderMode ShaderMode>
class TVdbShadowDepthVS : public FVdbShadowDepthVS
{
	DECLARE_SHADER_TYPE(TVdbShadowDepthVS, MeshMaterial);

	TVdbShadowDepthVS() = default;
	TVdbShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FVdbShadowDepthVS(Initializer)
	{}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// TODO: check and add info from TShadowDepthVS::ShouldCompilePermutation
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Volume &&
			FMeshMaterialShader::ShouldCompilePermutation(Parameters) &&
			VdbShaders::IsSupportedVertexFactoryType(Parameters.VertexFactoryType);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVdbShadowDepthVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == VertexShadowDepth_PerspectiveCorrect));
		OutEnvironment.SetDefine(TEXT("ONEPASS_POINTLIGHT_SHADOW"), (uint32)(ShaderMode == VertexShadowDepth_OnePassPointLight));
		OutEnvironment.SetDefine(TEXT("POSITION_ONLY"), 0);// (uint32)bUsePositionOnlyStream);

		bool bEnableNonNaniteVSM = (ShaderMode == VertexShadowDepth_VirtualShadowMap);
		OutEnvironment.SetDefine(TEXT("ENABLE_NON_NANITE_VSM"), bEnableNonNaniteVSM ? 1 : 0);
		if (bEnableNonNaniteVSM)
		{
			FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		}

		if (ShaderMode == VertexShadowDepth_OnePassPointLight)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_VertexUseAutoCulling);
		}
	}
};
typedef TVdbShadowDepthVS<VertexShadowDepth_PerspectiveCorrect> FVdbShadowDepthVS_PerspectiveCorrect;
typedef TVdbShadowDepthVS<VertexShadowDepth_OutputDepth> FVdbShadowDepthVS_OutputDepth;
typedef TVdbShadowDepthVS<VertexShadowDepth_OnePassPointLight> FVdbShadowDepthVS_OnePassPointLight;
typedef TVdbShadowDepthVS<VertexShadowDepth_VirtualShadowMap> FVdbShadowDepthVS_VirtualShadowMap;


class FVdbShadowDepthPS : public FMeshMaterialShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FVdbShadowDepthPS, NonVirtual);

	LAYOUT_FIELD(FShaderResourceParameter, DensityVdbBuffer);
	LAYOUT_FIELD(FShaderParameter, CustomIntData0);
	LAYOUT_FIELD(FShaderParameter, CustomIntData1);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData0);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData1);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData2);

	FVdbShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		DensityVdbBuffer.Bind(Initializer.ParameterMap, TEXT("DensityVdbBuffer"));
		CustomIntData0.Bind(Initializer.ParameterMap, TEXT("CustomIntData0"));
		CustomIntData1.Bind(Initializer.ParameterMap, TEXT("CustomIntData1"));
		CustomFloatData0.Bind(Initializer.ParameterMap, TEXT("CustomFloatData0"));
		CustomFloatData1.Bind(Initializer.ParameterMap, TEXT("CustomFloatData1"));
		CustomFloatData2.Bind(Initializer.ParameterMap, TEXT("CustomFloatData2"));

		PassUniformBuffer.Bind(Initializer.ParameterMap, FVdbShaderParams::StaticStructMetadata.GetShaderVariableName());
	}

	FVdbShadowDepthPS() {}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FVdbShaderVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_FORCE_TEXTURE_MIP"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MAJOR"), NANOVDB_MAJOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MINOR"), NANOVDB_MINOR_VERSION_NUMBER);

		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_MASKED"), TEXT("1"));
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FVdbElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(DensityVdbBuffer, ShaderElementData.DensityBufferSRV);
		ShaderBindings.Add(CustomIntData0, ShaderElementData.CustomIntData0);
		ShaderBindings.Add(CustomIntData1, ShaderElementData.CustomIntData1);
		ShaderBindings.Add(CustomFloatData0, ShaderElementData.CustomFloatData0);
		ShaderBindings.Add(CustomFloatData1, ShaderElementData.CustomFloatData1);
		ShaderBindings.Add(CustomFloatData2, ShaderElementData.CustomFloatData2);
	}
};

enum EVdbShadowDepthPixelShaderMode
{
	PixelShadowDepth_NonPerspectiveCorrect,
	PixelShadowDepth_PerspectiveCorrect,
	PixelShadowDepth_OnePassPointLight,
	PixelShadowDepth_VirtualShadowMap
};

template <EVdbShadowDepthPixelShaderMode ShaderMode, bool LevelSet>
class TVdbShadowDepthPS : public FVdbShadowDepthPS
{
	DECLARE_SHADER_TYPE(TVdbShadowDepthPS, MeshMaterial);

	TVdbShadowDepthPS() = default;
	TVdbShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FVdbShadowDepthPS(Initializer)
	{}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// TODO: check and add relevant things from TShadowDepthPS::ShouldCompilePermutation
		return FVdbShadowDepthPS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVdbShadowDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == PixelShadowDepth_PerspectiveCorrect));
		OutEnvironment.SetDefine(TEXT("ONEPASS_POINTLIGHT_SHADOW"), (uint32)(ShaderMode == PixelShadowDepth_OnePassPointLight));
		OutEnvironment.SetDefine(TEXT("VIRTUAL_TEXTURE_TARGET"), (uint32)(ShaderMode == PixelShadowDepth_VirtualShadowMap));

		bool bEnableNonNaniteVSM = (ShaderMode == PixelShadowDepth_VirtualShadowMap);
		OutEnvironment.SetDefine(TEXT("ENABLE_NON_NANITE_VSM"), bEnableNonNaniteVSM ? 1 : 0);
		if (bEnableNonNaniteVSM != 0)
		{
			FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		}

		OutEnvironment.SetDefine(TEXT("LEVEL_SET"), LevelSet);
	}
};
typedef TVdbShadowDepthPS<PixelShadowDepth_NonPerspectiveCorrect, true> FVdbShadowDepthPS_NonPerspectiveCorrecth_LevelSet;
typedef TVdbShadowDepthPS<PixelShadowDepth_PerspectiveCorrect, true> FVdbShadowDepthPS_PerspectiveCorrect_LevelSet;
typedef TVdbShadowDepthPS<PixelShadowDepth_OnePassPointLight, true> FVdbShadowDepthPS_OnePassPointLight_LevelSet;
typedef TVdbShadowDepthPS<PixelShadowDepth_VirtualShadowMap, true> FVdbShadowDepthPS_VirtualShadowMap_LevelSet;
typedef TVdbShadowDepthPS<PixelShadowDepth_NonPerspectiveCorrect, false> FVdbShadowDepthPS_NonPerspectiveCorrecth_FogVolume;
typedef TVdbShadowDepthPS<PixelShadowDepth_PerspectiveCorrect, false> FVdbShadowDepthPS_PerspectiveCorrect_FogVolume;
typedef TVdbShadowDepthPS<PixelShadowDepth_OnePassPointLight, false> FVdbShadowDepthPS_OnePassPointLight_FogVolume;
typedef TVdbShadowDepthPS<PixelShadowDepth_VirtualShadowMap, false> FVdbShadowDepthPS_VirtualShadowMap_FogVolume;


//-----------------------------------------------------------------------------

BEGIN_UNIFORM_BUFFER_STRUCT(FVdbPrincipledShaderParams, )
	// Volume properties
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, VdbDensity)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, VdbTemperature)
	SHADER_PARAMETER_SRV(StructuredBuffer<uint>, VdbColor)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BlackbodyCurveAtlas)
	SHADER_PARAMETER_SAMPLER(SamplerState, LinearTexSampler)

	SHADER_PARAMETER(FVector3f, VolumeScale)
	SHADER_PARAMETER(float, StepSize)
	SHADER_PARAMETER(FVector3f, VolumeTranslation)
	SHADER_PARAMETER(float, VoxelSize)
	SHADER_PARAMETER(FMatrix44f, VolumeToLocal)
	SHADER_PARAMETER(FMatrix44f, LocalToWorld)
	SHADER_PARAMETER(FMatrix44f, WorldToLocal)
	SHADER_PARAMETER(uint32, SamplesPerPixel)
	SHADER_PARAMETER(uint32, MaxRayDepth)
	SHADER_PARAMETER(uint32, ColoredTransmittance)
	SHADER_PARAMETER(uint32, TemporalNoise)
	// Material parameters
	SHADER_PARAMETER(FVector3f, Color)
	SHADER_PARAMETER(float, DensityMult)
	SHADER_PARAMETER(float, Albedo)
	SHADER_PARAMETER(float, Ambient)
	SHADER_PARAMETER(float, Anisotropy)
	SHADER_PARAMETER(float, EmissionStrength)
	SHADER_PARAMETER(FVector3f, EmissionColor)
	SHADER_PARAMETER(float, Threshold)
	SHADER_PARAMETER(FVector3f, BlackbodyTint)
	SHADER_PARAMETER(float, BlackbodyIntensity)
	SHADER_PARAMETER(float, Temperature)
	SHADER_PARAMETER(float, UseDirectionalLight)
	SHADER_PARAMETER(float, UseEnvironmentLight)
	SHADER_PARAMETER(int32, CurveIndex)
	SHADER_PARAMETER(int32, CurveAtlasHeight)
END_UNIFORM_BUFFER_STRUCT()

class FVdbPrincipledVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVdbPrincipledVS);
	SHADER_USE_PARAMETER_STRUCT(FVdbPrincipledVS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVdbPrincipledShaderParams, VdbGlobalParams)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_VERTEX"), 1);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MAJOR"), NANOVDB_MAJOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MINOR"), NANOVDB_MINOR_VERSION_NUMBER);
	}
};

class FVdbPrincipledPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVdbPrincipledPS);
	SHADER_USE_PARAMETER_STRUCT(FVdbPrincipledPS, FGlobalShader)

	class FPathTracing : SHADER_PERMUTATION_BOOL("PATH_TRACING");
	class FUseTemperature : SHADER_PERMUTATION_BOOL("USE_TEMPERATURE_VDB");
	class FUseColor : SHADER_PERMUTATION_BOOL("USE_COLOR_VDB");
	class FLevelSet : SHADER_PERMUTATION_BOOL("LEVEL_SET");
	class FTrilinear : SHADER_PERMUTATION_BOOL("USE_TRILINEAR_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FPathTracing, FUseTemperature, FUseColor, FLevelSet, FTrilinear>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Scene / Unreal data
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
		// VdbRendering data
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevAccumTex)
		SHADER_PARAMETER(uint32, NumAccumulations)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVdbPrincipledShaderParams, VdbGlobalParams)
		// Debug
		SHADER_PARAMETER(uint32, DisplayBounds)
		// Render Target
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); 
	}
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_PIXEL"), 1);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MAJOR"), NANOVDB_MAJOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MINOR"), NANOVDB_MINOR_VERSION_NUMBER);

		bool bSupportVirtualShadowMap = IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		if (bSupportVirtualShadowMap)
		{
			OutEnvironment.SetDefine(TEXT("VIRTUAL_SHADOW_MAP"), 1);
			FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		}
	}
};
