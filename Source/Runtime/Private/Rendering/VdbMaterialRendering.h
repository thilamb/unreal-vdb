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
#include "RendererInterface.h"
#include "SceneViewExtension.h"
#include "VdbDenoiser.h"

class FVdbMaterialSceneProxy;

class FVdbMaterialRendering : public FSceneViewExtensionBase
{
public:

	FVdbMaterialRendering(const FAutoRegister& AutoRegister);

	bool ShouldRenderVolumetricVdb() const;

	void Init(UTextureRenderTarget2D* DefaultRenderTarget);
	void Release();

	void AddVdbProxy(FVdbMaterialSceneProxy* Proxy);
	void RemoveVdbProxy(FVdbMaterialSceneProxy* Proxy);

	void CreateMeshBatch(const FSceneView* View, struct FMeshBatch&, const FVdbMaterialSceneProxy*, struct FVdbVertexFactoryUserDataWrapper&, const class FMaterialRenderProxy*) const;

	//~ Setters
	void SetDenoiserMethod(EVdbDenoiserMethod Method) { DenoiserMethod = Method; }
	//~ End Setters

	//~ Begin ISceneViewExtension Interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {}
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual int32 GetPriority() const override { return -1; }
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const { return true; }
	//~ End ISceneViewExtension Interface

private:

	struct SVdbPathtrace
	{
		uint32 NumAccumulations = 0;
		int32 MaxSPP = 1;
		FIntPoint RtSize = FIntPoint::ZeroValue;
		bool UsePathtracing = false;
		bool IsEven = false;
		bool FirstRender = true;
	};

	void InitRendering();
	void ReleaseRendering();

	void InitVolumeMesh();
	void InitVertexFactory();
	void InitDelegate();
	void ReleaseDelegate();

#if VDB_CAST_SHADOWS
	void ShadowDepth_RenderThread(FShadowDepthRenderParameters& Parameters);
#endif
	void Render_RenderThread(FPostOpaqueRenderParameters& Parameters, bool PostOpaque);
	void RenderPostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters);
	void RenderOverlay_RenderThread(FPostOpaqueRenderParameters& Parameters);

	void RenderLights(
		// Object Data
		FVdbMaterialSceneProxy* Proxy,
		bool Translucent,
		// Scene data
		const FPostOpaqueRenderParameters& Parameters,
		const SVdbPathtrace& VdbPathtrace,
		FRDGTexture* RenderTexture,
		FRDGTexture* DepthRenderTexture);

	void RenderLight(
		// Object data
		FVdbMaterialSceneProxy* Proxy,
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
		FRDGTexture* DepthRenderTexture);

	TArray<FVdbMaterialSceneProxy*> VdbProxies;
	TUniquePtr<class FVolumeMeshVertexBuffer> VertexBuffer;
	TUniquePtr<class FVolumeMeshVertexFactory> VertexFactory;
	FPostOpaqueRenderDelegate RenderPostOpaqueDelegate;
	FPostOpaqueRenderDelegate RenderOverlayDelegate;
	FDelegateHandle RenderPostOpaqueDelegateHandle;
	FDelegateHandle RenderOverlayDelegateHandle;
#if VDB_CAST_SHADOWS
	FShadowDepthRenderDelegate ShadowDepthDelegate;
#endif
	FDelegateHandle ShadowDepthDelegateHandle;

	UTextureRenderTarget2D* DefaultVdbRenderTarget = nullptr;
	FTexture* DefaultVdbRenderTargetTex = nullptr;

	EVdbDenoiserMethod DenoiserMethod = EVdbDenoiserMethod::None;
};