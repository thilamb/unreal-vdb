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

#include "VdbToDynamicMeshActor.h"

#include "VdbCommon.h"
#include "VdbVolumeBase.h"
#include "VdbAssetComponent.h"
#include "VdbToVolumeTextureComponent.h"
#include "VdbSequenceComponent.h"

AVdbToDynamicMeshActor::AVdbToDynamicMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AssetComponent = CreateDefaultSubobject<UVdbAssetComponent>(TEXT("AssetComponent"));

	VdbToTexComponent = CreateDefaultSubobject<UVdbToVolumeTextureComponent>(TEXT("VolumeToTextureComponent"));
	VdbToTexComponent->SetVdbAssets(AssetComponent);

	SequenceComponent = CreateDefaultSubobject<UVdbSequenceComponent>(TEXT("SequenceComponent"));
	SequenceComponent->SetVdbAssets(AssetComponent);

	AssetComponent->OnFrameChanged.AddUObject(this, &AVdbToDynamicMeshActor::UpdateDynamicMeshInternal);
}

#if WITH_EDITOR
bool AVdbToDynamicMeshActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	Super::GetReferencedContentObjects(Objects);

	AssetComponent->GetReferencedContentObjects(Objects);

	return true;
}
#endif

void AVdbToDynamicMeshActor::UpdateDynamicMeshInternal(uint32 FrameIndex)
{
	UpdateDynamicMesh();
}
