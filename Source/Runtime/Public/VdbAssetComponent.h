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
#include "Components/PrimitiveComponent.h"
#include "VdbCommon.h"

#include "VdbAssetComponent.generated.h"

class FVolumeRenderInfos;
class UVdbSequenceComponent;
class UVdbVolumeBase;
class UVdbVolumeAsset;

// Can contain several grids of the same OpenVDB/NanoVDB file
UCLASS(Blueprintable, ClassGroup = Rendering, HideCategories = (Activation, Collision, Cooking, HLOD, Navigation, VirtualTexture), meta = (BlueprintSpawnableComponent))
class VOLUMERUNTIME_API UVdbAssetComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

	virtual ~UVdbAssetComponent();

	//----------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Volume, meta = (AllowPrivateAccess = "true", DisplayName = "VDB Grids"))
	TObjectPtr<UVdbVolumeAsset> VdbAsset;

	UPROPERTY(EditAnywhere, Category = Volume, meta = (AllowPrivateAccess = "true"))
	int32 DensityGridIndex = 0;

	UPROPERTY(EditAnywhere, Category = Volume, meta = (AllowPrivateAccess = "true"))
	int32 TemperatureGridIndex = -1;

	UPROPERTY(EditAnywhere, Category = Volume, meta = (AllowPrivateAccess = "true"))
	int32 VelocityGridIndex = -1;

	UPROPERTY(EditAnywhere, Category = Volume, meta = (AllowPrivateAccess = "true"))
	int32 ColorGridIndex = -1;

	UFUNCTION(BlueprintCallable, Category = Volume)
	UVdbVolumeBase* GetDensityVolume();
	const UVdbVolumeBase* GetDensityVolume() const;
	
	UFUNCTION(BlueprintCallable, Category = Volume)
	UVdbVolumeBase* GetTemperatureVolume();
	const UVdbVolumeBase* GetTemperatureVolume() const;
	
	UFUNCTION(BlueprintCallable, Category = Volume)
	UVdbVolumeBase* GetColorVolume();
	const UVdbVolumeBase* GetColorVolume() const;

	UFUNCTION(BlueprintCallable, Category = Volume)
	UVdbVolumeBase* GetVelocityVolume();
	const UVdbVolumeBase* GetVelocityVolume() const;

	//----------------------------------------------------------------------------

	void BroadcastFrameChanged(uint32 Frame, bool Force = false);
	void BroadcastSubFrameChanged(float Value);
	void GetReferencedContentObjects(TArray<UObject*>& Objects) const;

	EVdbClass GetVdbClass() const;
	const FVolumeRenderInfos* GetRenderInfos(const UVdbVolumeBase* VdbVolumeStatic) const;

	const UVdbVolumeBase* GetMainVolume() const;

	TArray<const class UVdbVolumeBase*> GetConstVolumes() const;
	TArray<class UVdbVolumeBase*> GetVolumes();

	UFUNCTION(BlueprintCallable, Category = Volume)
	FVector3f GetVolumeSize() const;

	UFUNCTION(BlueprintCallable, Category = Volume)
	FVector3f GetVolumeOffset() const;

	UFUNCTION(BlueprintCallable, Category = Volume)
	FVector3f GetVolumeUvScale() const;

	DECLARE_DELEGATE(FOnVdbChanged)
	FOnVdbChanged OnVdbChanged;
	
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFrameChanged, uint32);
	FOnFrameChanged OnFrameChanged;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSubFrameChanged, float);
	FOnSubFrameChanged OnSubFrameChanged;

	void SetTargetFrameIndex(uint32 Frame) { TargetFrameIndex = Frame; }
	uint32 GetCurrFrameIndex() const { return CurrFrameIndex; }

private:

	uint32 CurrFrameIndex = 0;
	uint32 TargetFrameIndex = 0;

public:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};
