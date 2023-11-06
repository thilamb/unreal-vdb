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
#include "MaterialDomain.h"
#include "DataDrivenShaderPlatformInfo.h"

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
	FIntVector4 CustomIntData1; // x: BlackbodyCurveIndex, y: CurveAtlaHeight, z: TranslucentLevelSet, w: TemperatureOnly
	FVector4f CustomFloatData0; // x: Local step size, y: Shadow step size multiplier, z: voxel size, w: jittering
	FVector4f CustomFloatData1; // x: anisotropy, y: albedo, z: blackbody intensity, w: blackbody temperature
	FVector4f CustomFloatData2; // x: density mul, y: padding, z: ambient, w: velocity mult
	FVector4f SliceMinData; // xyz: slice data, w: unused
	FVector4f SliceMaxData; // xyz: slice data, w: unused
	FShaderResourceViewRHIRef DensityBufferSRV;
	FShaderResourceViewRHIRef TemperatureBufferSRV;
	FShaderResourceViewRHIRef VelocityBufferSRV;
	FShaderResourceViewRHIRef ColorBufferSRV;
	FShaderResourceViewRHIRef BlackbodyColorSRV;
};

//-----------------------------------------------------------------------------
//					--- Main pass rendering ---

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
	// Global Vdb data
	SHADER_PARAMETER(float, Threshold)
	// Light data
	SHADER_PARAMETER(int, bApplyEmissionAndTransmittance)
	SHADER_PARAMETER(int, bApplyDirectLighting)
	SHADER_PARAMETER(int, bApplyShadowTransmittance)
	SHADER_PARAMETER(int, LightType)
#if VDB_ENGINE_MODIFICATIONS
	SHADER_PARAMETER_STRUCT(FDeferredLightUniformStruct, DeferredLight)
	// Shadow data
	SHADER_PARAMETER_STRUCT(FForwardLightData, ForwardLightData)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParametersGlobal0, VolumeShadowingShaderParameters)
	SHADER_PARAMETER(int32, VirtualShadowMapId)
	// Indirect Lighting
	SHADER_PARAMETER_STRUCT(FLumenTranslucencyLightingParameters, LumenGIVolumeStruct)
#endif
	// Path-tracing
	SHADER_PARAMETER(uint32, NumAccumulations)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, PrevAccumTex)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

class FVdbShaderPS : public FMeshMaterialShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FVdbShaderPS, NonVirtual);

	LAYOUT_FIELD(FShaderResourceParameter, DensityVdbBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, TemperatureVdbBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, VelocityVdbBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, ColorVdbBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, BlackbodyColor);
	LAYOUT_FIELD(FShaderParameter, CustomIntData0);
	LAYOUT_FIELD(FShaderParameter, CustomIntData1);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData0);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData1);
	LAYOUT_FIELD(FShaderParameter, CustomFloatData2);
	LAYOUT_FIELD(FShaderParameter, SliceMinData);
	LAYOUT_FIELD(FShaderParameter, SliceMaxData);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Rant: I still don't really understand how these structs work. As far as I understand:
		// Here you can only bind Uniform Buffers. All other options will not be passed to shader (even if some will compile)
		// If Uniform buffer is static it will "just work" (I think). If not, you need to add a LAYOUT_FIELD.
		// Most Uniform buffers have a non-uniform buffer form, usually just a struct that can be included/nested in a sub-uniform buffer.
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVdbShaderParams, VdbUniformBuffer)
#if VDB_ENGINE_MODIFICATIONS
		// THIS ONE PARTICULARLY REQUIRES STATIC UBO BINDING, so let's do it
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
#endif
		// Render targets
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	FVdbShaderPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		DensityVdbBuffer.Bind(Initializer.ParameterMap, TEXT("DensityVdbBuffer"));
		TemperatureVdbBuffer.Bind(Initializer.ParameterMap, TEXT("TemperatureVdbBuffer"));
		VelocityVdbBuffer.Bind(Initializer.ParameterMap, TEXT("VelocityVdbBuffer"));
		ColorVdbBuffer.Bind(Initializer.ParameterMap, TEXT("ColorVdbBuffer"));
		BlackbodyColor.Bind(Initializer.ParameterMap, TEXT("BlackbodyColor"));
		CustomIntData0.Bind(Initializer.ParameterMap, TEXT("CustomIntData0"));
		CustomIntData1.Bind(Initializer.ParameterMap, TEXT("CustomIntData1"));
		CustomFloatData0.Bind(Initializer.ParameterMap, TEXT("CustomFloatData0"));
		CustomFloatData1.Bind(Initializer.ParameterMap, TEXT("CustomFloatData1"));
		CustomFloatData2.Bind(Initializer.ParameterMap, TEXT("CustomFloatData2"));
		SliceMinData.Bind(Initializer.ParameterMap, TEXT("SliceMinData"));
		SliceMaxData.Bind(Initializer.ParameterMap, TEXT("SliceMaxData"));

		PassUniformBuffer.Bind(Initializer.ParameterMap, FVdbShaderParams::FTypeInfo::GetStructMetadata()->GetShaderVariableName());
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
		OutEnvironment.SetDefine(TEXT("VDB_ENGINE_MODIFICATIONS"), VDB_ENGINE_MODIFICATIONS);

#if VDB_ENGINE_MODIFICATIONS
		bool bSupportVirtualShadowMap = IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		if (bSupportVirtualShadowMap)
		{
			OutEnvironment.SetDefine(TEXT("VIRTUAL_SHADOW_MAP"), 1);
			FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		}
#endif

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
		ShaderBindings.Add(VelocityVdbBuffer, ShaderElementData.VelocityBufferSRV);
		ShaderBindings.Add(ColorVdbBuffer, ShaderElementData.ColorBufferSRV);
		ShaderBindings.Add(BlackbodyColor, ShaderElementData.BlackbodyColorSRV);
		ShaderBindings.Add(CustomIntData0, ShaderElementData.CustomIntData0);
		ShaderBindings.Add(CustomIntData1, ShaderElementData.CustomIntData1);
		ShaderBindings.Add(CustomFloatData0, ShaderElementData.CustomFloatData0);
		ShaderBindings.Add(CustomFloatData1, ShaderElementData.CustomFloatData1);
		ShaderBindings.Add(CustomFloatData2, ShaderElementData.CustomFloatData2);
		ShaderBindings.Add(SliceMinData, ShaderElementData.SliceMinData);
		ShaderBindings.Add(SliceMaxData, ShaderElementData.SliceMaxData);
	}
};

template<bool IsLevelSet, bool UseTemperatureBuffer, bool UseVelocity, bool UseColorBuffer, bool NicerEnvLight, bool Trilinear>
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
		OutEnvironment.SetDefine(TEXT("USE_VELOCITY_VDB"), UseVelocity);
		OutEnvironment.SetDefine(TEXT("USE_COLOR_VDB"), UseColorBuffer);
		OutEnvironment.SetDefine(TEXT("NICER_BUT_EXPENSIVE_ENVLIGHT"), NicerEnvLight);
		OutEnvironment.SetDefine(TEXT("USE_TRILINEAR_SAMPLING"), Trilinear);
	}
};

// TODO: this is getting ridiculous. Find other solution
typedef TVdbShaderPS<true, false, false, false, false, false> FVdbShaderPS_LevelSet;
typedef TVdbShaderPS<true, true, false, false, false, false> FVdbShaderPS_LevelSet_Translucent; // reusing USE_TEMPERATURE_VDB variation for translucency to avoid another variation
typedef TVdbShaderPS<true, true, false, false, true, false> FVdbShaderPS_LevelSet_Translucent_EnvLight; // reusing USE_TEMPERATURE_VDB variation for translucency to avoid another variation
typedef TVdbShaderPS<false, false, false, false, false, false>  FVdbShaderPS_FogVolume;
typedef TVdbShaderPS<false, false, true, false, false, false>  FVdbShaderPS_FogVolume_Velocity;
typedef TVdbShaderPS<false, false, false, false, false, true>  FVdbShaderPS_FogVolume_Trilinear;
typedef TVdbShaderPS<false, false, true, false, false, true>  FVdbShaderPS_FogVolume_Velocity_Trilinear;
typedef TVdbShaderPS<false, false, false, false, true, false>  FVdbShaderPS_FogVolume_EnvLight;
typedef TVdbShaderPS<false, false, true, false, true, false>  FVdbShaderPS_FogVolume_Velocity_EnvLight;
typedef TVdbShaderPS<false, false, false, false, true, true>  FVdbShaderPS_FogVolume_EnvLight_Trilinear;
typedef TVdbShaderPS<false, false, true, false, true, true>  FVdbShaderPS_FogVolume_Velocity_EnvLight_Trilinear;
typedef TVdbShaderPS<false, false, false, true, false, false>  FVdbShaderPS_FogVolume_Color;
typedef TVdbShaderPS<false, false, true, true, false, false>  FVdbShaderPS_FogVolume_Velocity_Color;
typedef TVdbShaderPS<false, false, false, true, false, true>  FVdbShaderPS_FogVolume_Color_Trilinear;
typedef TVdbShaderPS<false, false, true, true, false, true>  FVdbShaderPS_FogVolume_Velocity_Color_Trilinear;
typedef TVdbShaderPS<false, false, false, true, true, false>  FVdbShaderPS_FogVolume_Color_EnvLight;
typedef TVdbShaderPS<false, false, true, true, true, false>  FVdbShaderPS_FogVolume_Velocity_Color_EnvLight;
typedef TVdbShaderPS<false, false, false, true, true, true>  FVdbShaderPS_FogVolume_Color_EnvLight_Trilinear;
typedef TVdbShaderPS<false, false, true, true, true, true>  FVdbShaderPS_FogVolume_Velocity_Color_EnvLight_Trilinear;
typedef TVdbShaderPS<false, true, false, false, false, false>  FVdbShaderPS_FogVolume_Blackbody;
typedef TVdbShaderPS<false, true, true, false, false, false>  FVdbShaderPS_FogVolume_Velocity_Blackbody;
typedef TVdbShaderPS<false, true, false, false, false, true>  FVdbShaderPS_FogVolume_Blackbody_Trilinear;
typedef TVdbShaderPS<false, true, true, false, false, true>  FVdbShaderPS_FogVolume_Velocity_Blackbody_Trilinear;
typedef TVdbShaderPS<false, true, false, false, true, false>  FVdbShaderPS_FogVolume_Blackbody_EnvLight;
typedef TVdbShaderPS<false, true, true, false, true, false>  FVdbShaderPS_FogVolume_Velocity_Blackbody_EnvLight;
typedef TVdbShaderPS<false, true, false, false, true, true>  FVdbShaderPS_FogVolume_Blackbody_EnvLight_Trilinear;
typedef TVdbShaderPS<false, true, true, false, true, true>  FVdbShaderPS_FogVolume_Velocity_Blackbody_EnvLight_Trilinear;
typedef TVdbShaderPS<false, true, false, true, false, false>  FVdbShaderPS_FogVolume_Blackbody_Color;
typedef TVdbShaderPS<false, true, true, true, false, false>  FVdbShaderPS_FogVolume_Velocity_Blackbody_Color;
typedef TVdbShaderPS<false, true, false, true, false, true>  FVdbShaderPS_FogVolume_Blackbody_Color_Trilinear;
typedef TVdbShaderPS<false, true, true, true, false, true>  FVdbShaderPS_FogVolume_Velocity_Blackbody_Color_Trilinear;
typedef TVdbShaderPS<false, true, false, true, true, false>  FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight;
typedef TVdbShaderPS<false, true, true, true, true, false>  FVdbShaderPS_FogVolume_Velocity_Blackbody_Color_EnvLight;
typedef TVdbShaderPS<false, true, false, true, true, true>  FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight_Trilinear;
typedef TVdbShaderPS<false, true, true, true, true, true>  FVdbShaderPS_FogVolume_Velocity_Blackbody_Color_EnvLight_Trilinear;


#if VDB_CAST_SHADOWS

//-----------------------------------------------------------------------------
//					--- Shadow Depth rendering ---

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVdbDepthShaderParams, )
	SHADER_PARAMETER(FMatrix44f, ShadowClipToTranslatedWorld)
	SHADER_PARAMETER(FVector4f, ShadowSVPositionToClip)
	SHADER_PARAMETER_ARRAY(FMatrix44f, CubeShadowClipToTranslatedWorld, [6])
	SHADER_PARAMETER(FVector3f, ShadowPreViewTranslation)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FVdbShadowDepthPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FShadowDepthPassUniformParameters, DeferredPassUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
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
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			Parameters.MaterialParameters.MaterialDomain == MD_Volume &&
			FMeshMaterialShader::ShouldCompilePermutation(Parameters) &&
			VdbShaders::IsSupportedVertexFactoryType(Parameters.VertexFactoryType);
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
		OutEnvironment.SetDefine(TEXT("VDB_ENGINE_MODIFICATIONS"), VDB_ENGINE_MODIFICATIONS);
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
		return FVdbShadowDepthVS::ShouldCompilePermutation(Parameters);
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
	LAYOUT_FIELD(FShaderParameter, SliceMinData);
	LAYOUT_FIELD(FShaderParameter, SliceMaxData);

	FVdbShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		DensityVdbBuffer.Bind(Initializer.ParameterMap, TEXT("DensityVdbBuffer"));
		CustomIntData0.Bind(Initializer.ParameterMap, TEXT("CustomIntData0"));
		CustomIntData1.Bind(Initializer.ParameterMap, TEXT("CustomIntData1"));
		CustomFloatData0.Bind(Initializer.ParameterMap, TEXT("CustomFloatData0"));
		CustomFloatData1.Bind(Initializer.ParameterMap, TEXT("CustomFloatData1"));
		CustomFloatData2.Bind(Initializer.ParameterMap, TEXT("CustomFloatData2"));
		SliceMinData.Bind(Initializer.ParameterMap, TEXT("SliceMinData"));
		SliceMaxData.Bind(Initializer.ParameterMap, TEXT("SliceMaxData"));

		PassUniformBuffer.Bind(Initializer.ParameterMap, FVdbShaderParams::FTypeInfo::GetStructMetadata()->GetShaderVariableName());
	}

	FVdbShadowDepthPS() {}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FVdbShaderVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_FORCE_TEXTURE_MIP"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MAJOR"), NANOVDB_MAJOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("SHADER_VERSION_MINOR"), NANOVDB_MINOR_VERSION_NUMBER);
		OutEnvironment.SetDefine(TEXT("MATERIALBLENDING_MASKED"), TEXT("1"));
		OutEnvironment.SetDefine(TEXT("VDB_ENGINE_MODIFICATIONS"), VDB_ENGINE_MODIFICATIONS);
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
		ShaderBindings.Add(SliceMinData, ShaderElementData.SliceMinData);
		ShaderBindings.Add(SliceMaxData, ShaderElementData.SliceMaxData);
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
//				--- Translucent Shadow Depth rendering ---

BEGIN_SHADER_PARAMETER_STRUCT(FVdbTrasnlucentShadowDepthPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FTranslucencyDepthPassUniformParameters, PassUniformBuffer)
	//SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVdbDepthShaderParams, VdbUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

/**
* Vertex shader used to render shadow maps for translucency.
*/

enum ETranslucencyShadowDepthShaderMode
{
	TranslucencyShadowDepth_PerspectiveCorrect,
	TranslucencyShadowDepth_Standard,
};

template <ETranslucencyShadowDepthShaderMode ShaderMode>
class TVdbTranslucencyShadowDepthVS : public FVdbShadowDepthVS
{
	DECLARE_SHADER_TYPE(TVdbTranslucencyShadowDepthVS, MeshMaterial);

	TVdbTranslucencyShadowDepthVS() = default;
	TVdbTranslucencyShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FVdbShadowDepthVS(Initializer)
	{}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return AllowTranslucencyPerObjectShadows(Parameters.Platform) && IsTranslucentBlendMode(Parameters.MaterialParameters) &&
			FVdbShadowDepthVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVdbShadowDepthVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == TranslucencyShadowDepth_PerspectiveCorrect ? 1 : 0));
		OutEnvironment.SetDefine(TEXT("TRANSLUCENT_SHADOWS"), 1);
	}
};
typedef TVdbTranslucencyShadowDepthVS<TranslucencyShadowDepth_PerspectiveCorrect> FVdbTranslucentShadowDepthVS_PerspectiveCorrect;
typedef TVdbTranslucencyShadowDepthVS<TranslucencyShadowDepth_Standard> FVdbTranslucentShadowDepthVS_Standard;

/**
 * Pixel shader used for accumulating translucency layer densities
 */
template <ETranslucencyShadowDepthShaderMode ShaderMode>
class TVdbTranslucencyShadowDepthPS : public FVdbShadowDepthPS
{
public:
	DECLARE_SHADER_TYPE(TVdbTranslucencyShadowDepthPS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return AllowTranslucencyPerObjectShadows(Parameters.Platform) && IsTranslucentBlendMode(Parameters.MaterialParameters) &&
			FVdbShadowDepthVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FVdbShadowDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == TranslucencyShadowDepth_PerspectiveCorrect ? 1 : 0));
		OutEnvironment.SetDefine(TEXT("STRATA_INLINE_SHADING"), 1);
		OutEnvironment.SetDefine(TEXT("TRANSLUCENT_SHADOWS"), 1);
	}

	TVdbTranslucencyShadowDepthPS() = default;
	TVdbTranslucencyShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FVdbShadowDepthPS(Initializer)
	{
	}


private:
	// We actually DO NOT want to support self translucency, we are doing a state of the art shading in main pass
	//LAYOUT_FIELD(FShaderParameter, TranslucentShadowStartOffset);
};
typedef TVdbTranslucencyShadowDepthPS<TranslucencyShadowDepth_PerspectiveCorrect> FVdbTranslucentShadowDepthPS_PerspectiveCorrect;
typedef TVdbTranslucencyShadowDepthPS<TranslucencyShadowDepth_Standard> FVdbTranslucentShadowDepthPS_Standard;

#endif // VDB_CAST_SHADOWS