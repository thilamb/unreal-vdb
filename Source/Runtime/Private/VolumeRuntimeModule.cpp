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

#include "VolumeRuntimeModule.h"
#include "Actors/VdbAssetComponentDetailsCustomization.h"
#include "Rendering/VdbVolumeRendering.h"
#include "VdbCommon.h"

#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "FVolumeRuntimeModule"

void FVolumeRuntimeModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SparseVolumetrics"));

	FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/VdbVolume"), PluginShaderDir);

	RegisterVolumeTrackHandler(&VdbVolumeSequenceTrackHandler);
	RegisterDetailsCustomizations();
}

void FVolumeRuntimeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UnregisterVolumeTrackHandler(&VdbVolumeSequenceTrackHandler);

	if (VdbMaterialRenderExtension) VdbMaterialRenderExtension->Release();
}

FVolumeRuntimeModule::TRenderExtensionPtr FVolumeRuntimeModule::GetRenderExtension(UTextureRenderTarget2D* DefaultRenderTarget)
{
	static const FName ModuleName = "VolumeRuntime";
	auto& ModuleInterface = FModuleManager::LoadModuleChecked<FVolumeRuntimeModule>(ModuleName);
	return ModuleInterface.GetOrCreateRenderExtension(DefaultRenderTarget);
}

void FVolumeRuntimeModule::RegisterDetailsCustomizations()
{
#if WITH_EDITOR
	auto& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Register our customization to be used by a class 'UMyClass' or 'AMyClass'. Note the prefix must be dropped.
	PropertyModule.RegisterCustomClassLayout(
		"VdbAssetComponent",
		FOnGetDetailCustomizationInstance::CreateStatic(&FVdbAssetComponentDetails::MakeInstance)
	);

	PropertyModule.NotifyCustomizationModuleChanged();
#endif
}

FVolumeRuntimeModule::TRenderExtensionPtr FVolumeRuntimeModule::GetOrCreateRenderExtension(UTextureRenderTarget2D* DefaultRenderTarget)
{
	if (!VdbMaterialRenderExtension.IsValid())
	{
		VdbMaterialRenderExtension = FSceneViewExtensions::NewExtension<FVdbVolumeRendering>();
		VdbMaterialRenderExtension->Init(DefaultRenderTarget);
	}
	return VdbMaterialRenderExtension;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVolumeRuntimeModule, VolumeRuntime)
