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

#include "VdbShaders.h"

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVdbShaderParams, "VdbParams", DeferredDecals);
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FVdbDepthShaderParams, "VdbDepthParams", DeferredDecals);
IMPLEMENT_UNIFORM_BUFFER_STRUCT(FVdbPrincipledShaderParams, "VdbPrincipledParams");

// Material
IMPLEMENT_MATERIAL_SHADER_TYPE(, FVdbShaderVS, TEXT("/Plugin/VdbVolume/Private/VdbVertexShader.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_LevelSet, TEXT("/Plugin/VdbVolume/Private/VdbLevelSet.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_LevelSet_Translucent, TEXT("/Plugin/VdbVolume/Private/VdbLevelSet.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_LevelSet_Translucent_EnvLight, TEXT("/Plugin/VdbVolume/Private/VdbLevelSet.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_EnvLight, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_EnvLight_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Color, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Color_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Color_EnvLight, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Color_EnvLight_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody_EnvLight, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody_EnvLight_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody_Color, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody_Color_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShaderPS_FogVolume_Blackbody_Color_EnvLight_Trilinear, TEXT("/Plugin/VdbVolume/Private/VdbFogVolume.usf"), TEXT("MainPS"), SF_Pixel);

// Shadow Depth
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthVS_PerspectiveCorrect, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthVS_OutputDepth, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthVS_OnePassPointLight, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthVS_VirtualShadowMap, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainVS"), SF_Vertex);

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_NonPerspectiveCorrecth_LevelSet, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_PerspectiveCorrect_LevelSet, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_OnePassPointLight_LevelSet, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_VirtualShadowMap_LevelSet, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_NonPerspectiveCorrecth_FogVolume, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_PerspectiveCorrect_FogVolume, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_OnePassPointLight_FogVolume, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FVdbShadowDepthPS_VirtualShadowMap_FogVolume, TEXT("/Plugin/VdbVolume/Private/VdbShadowDepth.usf"), TEXT("MainPS"), SF_Pixel);

// Principled
IMPLEMENT_GLOBAL_SHADER(FVdbPrincipledVS, "/Plugin/VdbVolume/Private/VdbPrincipled.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FVdbPrincipledPS, "/Plugin/VdbVolume/Private/VdbPrincipled.usf", "MainPS", SF_Pixel);