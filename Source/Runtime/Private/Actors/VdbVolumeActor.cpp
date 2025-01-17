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

#include "VdbVolumeActor.h"

#include "VdbCommon.h"
#include "VdbVolumeBase.h"
#include "VdbAssetComponent.h"
#include "VdbMaterialComponent.h"
#include "VdbSequenceComponent.h"

AVdbVolumeActor::AVdbVolumeActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AssetComponent = CreateDefaultSubobject<UVdbAssetComponent>(TEXT("AssetComponent"));

	MaterialComponent = CreateDefaultSubobject<UVdbMaterialComponent>(TEXT("MaterialComponent"));
	MaterialComponent->SetVdbAssets(AssetComponent);

	SeqComponent = CreateDefaultSubobject<UVdbSequenceComponent>(TEXT("SequenceComponent"));
	SeqComponent->SetVdbAssets(AssetComponent);

	RootComponent = MaterialComponent;
}

#if WITH_EDITOR
bool AVdbVolumeActor::GetReferencedContentObjects(TArray<UObject*>& Objects) const
{
	Super::GetReferencedContentObjects(Objects);

	AssetComponent->GetReferencedContentObjects(Objects);

	return true;
}
#endif
