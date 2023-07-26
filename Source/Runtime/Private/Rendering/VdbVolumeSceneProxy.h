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

#include "CoreMinimal.h"
#include "VdbCommon.h"
#include "PrimitiveSceneProxy.h"

class UVdbAssetComponent;
class UVdbMaterialComponent;
class FVdbRenderBuffer;

// Render Thread equivalent of VdbMaterialComponent
class FVdbVolumeSceneProxy : public FPrimitiveSceneProxy
{
public:
	FVdbVolumeSceneProxy(const UVdbAssetComponent* AssetComponent, const UVdbMaterialComponent* InComponent);
	virtual ~FVdbVolumeSceneProxy() = default;

	FVector3f GetIndexMin() const { return IndexMin; }
	FVector3f GetIndexSize() const { return IndexSize; }
	FIntVector4 GetCustomIntData0() const { return CustomIntData0; }
	FIntVector4 GetCustomIntData1() const { return CustomIntData1; }
	FVector4f GetCustomFloatData0() const { return CustomFloatData0; }
	FVector4f GetCustomFloatData1() const { return CustomFloatData1; }
	FVector4f GetCustomFloatData2() const { return CustomFloatData2; }
	const FMatrix44f& GetIndexToLocal() const { return IndexToLocal; }
	class UMaterialInterface* GetMaterial() const { return Material; }
	const FVdbRenderBuffer* GetDensityRenderResource() const { return DensityRenderBuffer; }
	const FVdbRenderBuffer* GetTemperatureRenderResource() const { return TemperatureRenderBuffer; }
	const FVdbRenderBuffer* GetColorRenderResource() const { return ColorRenderBuffer; }
	FTexture* GetBlackbodyAtlasResource() const { return (CurveIndex != INDEX_NONE) ? CurveAtlasTex : nullptr; }
	
	bool IsLevelSet() const { return LevelSet; }
	bool IsTranslucentLevelSet() const { return LevelSet && TranslucentLevelSet; }
	bool IsTranslucent() const { return !LevelSet || TranslucentLevelSet; }
	bool IsIndexToLocalDeterminantNegative() const { return IndexToLocalDeterminantNegative; }
	bool IsTemperatureOnly() const { return TemperatureOnly; }
	bool UseImprovedEnvLight() const { return ImprovedEnvLight; }
	bool UseTrilinearSampling() const { return TrilinearSampling; }
	bool RendersAfterTransparents() const { return RenderAfterTransparents; }
	void ResetVisibility() { VisibleViews.Empty(4); }
	bool IsVisible(const FSceneView* View) const { return VisibleViews.Find(View) != INDEX_NONE; }
	void Update(const FMatrix44f& IndexToLocal, const FVector3f& IndexMin, const FVector3f& IndexSize, FVdbRenderBuffer* DensityRenderBuffer, FVdbRenderBuffer* TemperatureRenderBuffer, FVdbRenderBuffer* ColorRenderBuffer);
	void UpdateCurveAtlasTex();

	FRDGTextureRef GetOrCreateRenderTarget(FRDGBuilder& GraphBuilder, const FIntPoint& RtSize, bool EvenFrame);

	//~ Begin FPrimitiveSceneProxy Interface
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

protected:
	virtual SIZE_T GetTypeHash() const override;
	virtual void CreateRenderThreadResources() override;
	virtual void DestroyRenderThreadResources() override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual uint32 GetMemoryFootprint() const override { return(sizeof(*this) + GetAllocatedSize()); }
	//~ End FPrimitiveSceneProxy Interface

private:
	
	TSharedPtr<class FVdbVolumeRendering, ESPMode::ThreadSafe> VdbMaterialRenderExtension;

	// Fixed attributes
	const UVdbMaterialComponent* VdbMaterialComponent = nullptr;
	class UMaterialInterface* Material = nullptr;
	FMaterialRelevance MaterialRelevance;
	bool LevelSet;
	bool TranslucentLevelSet;
	bool ImprovedEnvLight;
	bool TrilinearSampling;
	bool IndexToLocalDeterminantNegative;
	bool CastShadows;
	bool TemperatureOnly;
	bool RenderAfterTransparents;

	FIntVector4 CustomIntData0;
	FIntVector4 CustomIntData1;
	FVector4f CustomFloatData0;
	FVector4f CustomFloatData1;
	FVector4f CustomFloatData2;

	int32 CurveIndex;
	UCurveLinearColorAtlas* CurveAtlas;
	FTexture* CurveAtlasTex = nullptr;

	FVdbRenderBuffer* DensityRenderBuffer;
	FVdbRenderBuffer* TemperatureRenderBuffer;
	FVdbRenderBuffer* ColorRenderBuffer;
	FVector3f IndexMin;
	FVector3f IndexSize;
	FMatrix44f IndexToLocal;

	mutable TArray<const FSceneView*> VisibleViews;

	// For path-tracing accumulation only
	TRefCountPtr<IPooledRenderTarget> OffscreenRenderTarget[2];
};
