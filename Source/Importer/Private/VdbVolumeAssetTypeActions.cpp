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

#include "VdbVolumeAssetTypeActions.h"
#include "VdbVolumeAsset.h"
#include "VdbAssetComponent.h"
#include "VdbVolumeBase.h"
#include "VdbToVolumeTextureFactory.h"

#include "EditorFramework/AssetImportData.h"
#include "Engine/VolumeTexture.h"
#include "ToolMenus.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "UObject/ConstructorHelpers.h"

#include "Widgets/SWidget.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FVdbVolumeAssetTypeActions::FVdbVolumeAssetTypeActions(EAssetTypeCategories::Type InAssetCategory)
	: MyAssetCategory(InAssetCategory)
{
}

FText FVdbVolumeAssetTypeActions::GetName() const
{
	return LOCTEXT("FVdbVolumeAssetTypeActionsName", "NanoVdb");
}

FColor FVdbVolumeAssetTypeActions::GetTypeColor() const
{
	return FColor::Silver;
}

UClass* FVdbVolumeAssetTypeActions::GetSupportedClass() const
{
	return UVdbVolumeAsset::StaticClass();
}

uint32 FVdbVolumeAssetTypeActions::GetCategories()
{
	return MyAssetCategory;
}

void FVdbVolumeAssetTypeActions::GetResolvedSourceFilePaths(const TArray<UObject*>& TypeAssets, TArray<FString>& OutSourceFilePaths) const
{
	for (auto& Asset : TypeAssets)
	{
		UVdbVolumeAsset* VdbVolume = CastChecked<UVdbVolumeAsset>(Asset);
		if (VdbVolume)
		{
			VdbVolume->GetAssetImportData()->ExtractFilenames(OutSourceFilePaths);
		}
	}
}

void FVdbVolumeAssetTypeActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	auto VdbVolumes = GetTypedWeakObjectPtrs<UVdbVolumeAsset>(InObjects);

	Section.AddSubMenu(
		"VdbVolume_CreateVolumeTexture",
		LOCTEXT("VdbVolume_CreateVolumeTexture", "Create Volume Texture"),
		LOCTEXT("VdbVolume_CreateVolumeTextureTooltip", "Creates a Volume texture and copies content from Vdb Volume."),
		FNewToolMenuDelegate::CreateSP(this, &FVdbVolumeAssetTypeActions::ToVolumeSubMenu, VdbVolumes),
		FUIAction(),
		EUserInterfaceActionType::Button,
		false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Texture2D")
	);
	Section.AddSubMenu(
		"VdbVolume_ChangeFrameRate",
		LOCTEXT("VdbVolume_ChangeFrameRate", "Change Frame Rate"),
		LOCTEXT("VdbVolume_ChangeFrameRateTooltip", "Modifies the number of sequence frames per seconds."),
		FNewToolMenuDelegate::CreateSP(this, &FVdbVolumeAssetTypeActions::FrameRateSubMenu, VdbVolumes),
		FUIAction(),
		EUserInterfaceActionType::Button,
		false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.MediaProfile") // MediaProfile
	);
}

void FVdbVolumeAssetTypeActions::ToVolumeSubMenu(UToolMenu* Menu, TArray<TWeakObjectPtr<UVdbVolumeAsset>> VdbVolumes)
{
	if (VdbVolumes.Num() == 1)
	{
		FToolMenuSection& Section = Menu->AddSection("Vdb Grids", LOCTEXT("VdbGridsList", "Vdb Grids"));

		auto VdbVolume = VdbVolumes[0];

		UVdbVolumeBase* Volume = VdbVolume->GetVdbVolume(0);
		bool IsSequence = Volume && Volume->IsSequence();
		if (IsSequence)
		{
			UVdbVolumeSequence* VolumeSeq = static_cast<UVdbVolumeSequence*>(Volume);
			const int Min = 0;
			const int Max = VolumeSeq->GetNbFrames() - 1;

			TSharedRef<SWidget> Widget =
				SNew(SBox)
				.WidthOverride(100)
				//.Padding(FMargin(5, 0, 0, 0))
				[
					SNew(SNumericEntryBox<uint32>)
					.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
					.AllowSpin(true)
					.MinDesiredValueWidth(Min)
					.MinValue(Min)
					.MinSliderValue(Min)
					.MaxSliderValue(Max)
					.MaxValue(Max)
					.OnValueChanged_Lambda([this](uint32 NewValue)
					{
						CurrentFrame = NewValue;
					})
					.Value_Lambda([this]()
					{
						return CurrentFrame;
					})
				];
			
			FText FrameIdx = FText::FromString("Frame");
			Menu->AddMenuEntry("FrameIndex", FToolMenuEntry::InitWidget("FrameIndex", Widget, FrameIdx, true));
		}

		for (UVdbVolumeBase* VolumeBase : VdbVolume->VdbVolumes)
		{
			Section.AddMenuEntry(
				NAME_None,
				FText::Format(LOCTEXT("PackageChunk", "{0}"), FText::FromString(VolumeBase->GetGridName())),
				FText(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &FVdbVolumeAssetTypeActions::ExecuteConvertToVolume, VolumeBase, IsSequence)
				)
			);
		}
	}
	else
	{
		Menu->AddMenuEntry("OnlySelectOneVdb",
			FToolMenuEntry::InitMenuEntry(
				"OnlySelectOneVdb",
				LOCTEXT("OnlySelectOneVdb", "ERROR: This action doesn't support multi-selection. Try again with a single VDB asset."),
				FText(), FSlateIcon(), FUIAction()
			)
		);
	}
}

void FVdbVolumeAssetTypeActions::ExecuteConvertToVolume(UVdbVolumeBase* VdbVolume, bool IsSequence)
{
	FString DefaultSuffix("_" + VdbVolume->GetGridName());
	if (IsSequence)
	{
		DefaultSuffix = DefaultSuffix + "_" + FString::FromInt(CurrentFrame) + "_";
	}

	// Determine the asset name
	FString Name;
	FString PackagePath;
	CreateUniqueAssetName(VdbVolume->GetOutermost()->GetName(), DefaultSuffix, PackagePath, Name);

	// Create the factory used to generate the asset
	UVdbToVolumeTextureFactory* Factory = NewObject<UVdbToVolumeTextureFactory>();
	Factory->InitialVdbVolume = VdbVolume;
	Factory->FrameIndex = CurrentFrame;
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().CreateNewAsset(Name, FPackageName::GetLongPackagePath(PackagePath), UVolumeTexture::StaticClass(), Factory);
}

void FVdbVolumeAssetTypeActions::FrameRateSubMenu(UToolMenu* Menu, TArray<TWeakObjectPtr<UVdbVolumeAsset>> VdbVolumes)
{
	TSharedRef<SWidget> MasterWidget = 
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SBox)
				.WidthOverride(100)
				[
					SNew(SNumericEntryBox<float>)
						.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
						.AllowSpin(true)
						.MinDesiredValueWidth(100)
						.MinValue(1)
						.MinSliderValue(12)
						.MaxSliderValue(60)
						.MaxValue(240)
						.OnValueChanged_Lambda([this](float NewValue)
							{
								ModifiedFrameRate = NewValue;
							})
						.Value_Lambda([this]()
							{
								return ModifiedFrameRate;
							})
				]
		]
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "Button")
				.TextStyle(FAppStyle::Get(), "DialogButtonText")
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Text(LOCTEXT("ApplyButtonLabel", "Apply"))
				.OnClicked_Lambda([this, VdbVolumes]()
					{
						ExecuteChangeFrameRate(VdbVolumes);
						return FReply::Handled();
					})
		];

	FToolMenuSection& Section = Menu->AddSection("Change Frame Rate", LOCTEXT("ChangeFrameRate", "Change Frame Rate"));
	Menu->AddMenuEntry("New Frame Rate:", FToolMenuEntry::InitWidget("FrameRate", MasterWidget, FText::FromString("Value"), true));
}

void FVdbVolumeAssetTypeActions::ExecuteChangeFrameRate(TArray<TWeakObjectPtr<UVdbVolumeAsset>> VdbVolumes)
{
	for (auto VdbVolume : VdbVolumes)
	{
		VdbVolume->ChangeFrameRate(ModifiedFrameRate);
		VdbVolume->MarkPackageDirty();
	}
}

#undef LOCTEXT_NAMESPACE
