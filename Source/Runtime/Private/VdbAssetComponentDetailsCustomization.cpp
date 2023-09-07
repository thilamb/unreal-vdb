// Copyright Thibault Lambert

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#if WITH_EDITOR

#include "VdbAssetComponentDetailsCustomization.h"
#include "VdbAssetComponent.h"

#include "Widgets/SWidget.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "VdbAssetComponentDetailsCustomization"

TArray<TSharedPtr<FString>> FVdbAssetComponentDetails::GridNamesStrings;
TArray<TSharedPtr<FVdbAssetComponentDetails::SNameType>> FVdbAssetComponentDetails::GridNamesTypes;

class SGridWidgetRow : public SMultiColumnTableRow<FVdbAssetComponentDetails::SNameTypePtr>
{
	SLATE_BEGIN_ARGS(SGridWidgetRow) { }
	SLATE_ARGUMENT(FVdbAssetComponentDetails::SNameTypePtr, Item)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		NameType = InArgs._Item;

		SMultiColumnTableRow<FVdbAssetComponentDetails::SNameTypePtr>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (InColumnName == TEXT("GridName"))
		{
			return SNew(SBox).Padding(FMargin(4.0f, 0.0f)).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(NameType->Name))
				];
		}
		else if (InColumnName == TEXT("GridType"))
		{
			return SNew(SBox).Padding(FMargin(4.0f, 0.0f)).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(FText::FromString(NameType->Type))
				];
		}
		else
		{
			ensure(false);
			return SNew(SBorder);
		}
	}

private:
	FVdbAssetComponentDetails::SNameTypePtr NameType;
};

TSharedRef<IDetailCustomization> FVdbAssetComponentDetails::MakeInstance()
{
	return MakeShareable(new FVdbAssetComponentDetails);
}

TSharedRef<ITableRow> FVdbAssetComponentDetails::HandleGenerateRowList(TSharedPtr<FVdbAssetComponentDetails::SNameType> NameType, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SGridWidgetRow, OwnerTable)
		.Item(NameType);
}

TSharedRef<ITableRow> FVdbAssetComponentDetails::HandleGenerateRowCombo(TSharedPtr<FString> SpecifierName, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow< TSharedPtr<FString> >, OwnerTable)
		.Content()
		[
			SNew(STextBlock)
			.Text(FText::FromString(*SpecifierName.Get()))
		];
}

FText GetGridName(UVdbAssetComponent* Component, int32 GridIndex)
{
	if (Component && GridIndex != -1 && Component->VdbAsset && GridIndex < Component->VdbAsset->VdbVolumes.Num())
	{
		return FText::FromString(Component->VdbAsset->VdbVolumes[GridIndex]->GetName());
	}
	return FText::FromString("");
}

FText FVdbAssetComponentDetails::GetCurrentDensityGridName() const
{
	return GetGridName(CurrentComponent, CurrentComponent->DensityGridIndex);
}
FText FVdbAssetComponentDetails::GetCurrentTemperatureGridName() const
{
	return GetGridName(CurrentComponent, CurrentComponent->TemperatureGridIndex);
}
FText FVdbAssetComponentDetails::GetCurrentColorGridName() const
{
	return GetGridName(CurrentComponent, CurrentComponent->ColorGridIndex);
}

void OnGridSelected(UVdbAssetComponent* Component, int32& GridIndex, TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo)
{
	int32 NewIndex = -1;
	if (Component && Component->VdbAsset)
	{
		int32 Idx = 0;
		for (const UVdbVolumeBase* Grid : Component->VdbAsset->VdbVolumes)
		{
			if (Grid->GetName().Equals(*SpecifierName) )
			{
				NewIndex = Idx;
				break;
			}
			++Idx;
		}
	}
	GridIndex = NewIndex;
}

void FVdbAssetComponentDetails::OnDensityGridSelected(TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo)
{
	OnGridSelected(CurrentComponent, CurrentComponent->DensityGridIndex, SpecifierName, SelectInfo);

	// We have to do this manually to trigger component and actor update
	FPropertyChangedEvent ChangedEvent(UVdbAssetComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UVdbAssetComponent, DensityGridIndex)));
	CurrentComponent->PostEditChangeProperty(ChangedEvent);
}
void FVdbAssetComponentDetails::OnTemperatureGridSelected(TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo)
{
	OnGridSelected(CurrentComponent, CurrentComponent->TemperatureGridIndex, SpecifierName, SelectInfo);

	// We have to do this manually to trigger component and actor update
	FPropertyChangedEvent ChangedEvent(UVdbAssetComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UVdbAssetComponent, TemperatureGridIndex)));
	CurrentComponent->PostEditChangeProperty(ChangedEvent);
}
void FVdbAssetComponentDetails::OnColorGridSelected(TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo)
{
	OnGridSelected(CurrentComponent, CurrentComponent->ColorGridIndex, SpecifierName, SelectInfo);

	// We have to do this manually to trigger component and actor update
	FPropertyChangedEvent ChangedEvent(UVdbAssetComponent::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UVdbAssetComponent, ColorGridIndex)));
	CurrentComponent->PostEditChangeProperty(ChangedEvent);
}

void FVdbAssetComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	LayoutBuilder = &DetailBuilder;
	CurrentComponent = nullptr;

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	if (Objects.Num() != 1)
	{
		return;
	}
	CurrentComponent = CastChecked<UVdbAssetComponent>(Objects[0]);

	DetailBuilder.HideProperty(DetailBuilder.GetProperty("VdbAsset"));
	DetailBuilder.HideProperty(DetailBuilder.GetProperty("DensityGridIndex"));
	DetailBuilder.HideProperty(DetailBuilder.GetProperty("TemperatureGridIndex"));
	DetailBuilder.HideProperty(DetailBuilder.GetProperty("ColorGridIndex"));

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("Volume", LOCTEXT("FunctionDetailsGrids", "Volume"));

	TArray<UVdbVolumeBase*> VdbAsset;
	if (CurrentComponent->VdbAsset)
	{
		VdbAsset = CurrentComponent->VdbAsset->VdbVolumes;
	}

	GridNamesTypes.Empty();
	for (UVdbVolumeBase* Grid : VdbAsset)
	{
		TSharedPtr<SNameType> NameType(new SNameType{ Grid->GetName(), Grid->GetType() });
		GridNamesTypes.Add(NameType);
	};

	GridNamesStrings.Empty();
	GridNamesStrings.Add(TSharedPtr<FString>(new FString("")));
	for (UVdbVolumeBase* Grid : VdbAsset)
	{
		GridNamesStrings.Add(TSharedPtr<FString>(new FString(Grid->GetName())));
	};

	// Re-enable VdbAsset display, so that it displays first
	Category.AddProperty(DetailBuilder.GetProperty("VdbAsset"));

	// Display list of grids
	if (!VdbAsset.IsEmpty())
	{
		Category.AddCustomRow(LOCTEXT("VdbGrids", "VdbAsset"))
			.WholeRowContent()
			[
				SNew(SBox)
				[
					SNew(SListView<TSharedPtr<SNameType>>)
					.ItemHeight(24)
					.ListItemsSource(&GridNamesTypes)
					.OnGenerateRow(this, &FVdbAssetComponentDetails::HandleGenerateRowList)
					.HeaderRow
					(
						SNew(SHeaderRow)
						+ SHeaderRow::Column("GridName").DefaultLabel(LOCTEXT("GridName", "Grid Name")).FillWidth(0.25f)
						+ SHeaderRow::Column("GridType").DefaultLabel(LOCTEXT("GridType", "Grid Type")).FillWidth(0.1f)
					)
				]
			];
	}

	// Density Grid
	Category.AddCustomRow(LOCTEXT("DensityGrid", "Density Grid"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DensityGrid", "Density Grid"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SComboButton)
			.ContentPadding(0)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &FVdbAssetComponentDetails::GetCurrentDensityGridName)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.MenuContent()
			[
				SNew(SListView<TSharedPtr<FString> >)
				.ListItemsSource(&GridNamesStrings)
				.OnGenerateRow(this, &FVdbAssetComponentDetails::HandleGenerateRowCombo)
				.OnSelectionChanged(this, &FVdbAssetComponentDetails::OnDensityGridSelected)
			]
		];

	// Temperature Grid
	Category.AddCustomRow(LOCTEXT("TemperatureGrid", "Temperature Grid"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TemperatureGrid", "Temperature Grid"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SComboButton)
			.ContentPadding(0)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &FVdbAssetComponentDetails::GetCurrentTemperatureGridName)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.MenuContent()
			[
				SNew(SListView<TSharedPtr<FString> >)
				.ListItemsSource(&GridNamesStrings)
				.OnGenerateRow(this, &FVdbAssetComponentDetails::HandleGenerateRowCombo)
				.OnSelectionChanged(this, &FVdbAssetComponentDetails::OnTemperatureGridSelected)
			]
		];

	// Color Grid
	Category.AddCustomRow(LOCTEXT("ColorGrid", "Color Grid"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ColorGrid", "Color Grid"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SComboButton)
			.ContentPadding(0)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Text(this, &FVdbAssetComponentDetails::GetCurrentColorGridName)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.MenuContent()
			[
				SNew(SListView<TSharedPtr<FString> >)
				.ListItemsSource(&GridNamesStrings)
				.OnGenerateRow(this, &FVdbAssetComponentDetails::HandleGenerateRowCombo)
				.OnSelectionChanged(this, &FVdbAssetComponentDetails::OnColorGridSelected)
			]
		];

	CurrentComponent->OnVdbChanged.BindSP(this, &FVdbAssetComponentDetails::ForceRefresh);
}

void FVdbAssetComponentDetails::ForceRefresh()
{
	if (LayoutBuilder)
	{
		LayoutBuilder->ForceRefreshDetails();
	}
}

#undef LOCTEXT_NAMESPACE

#endif
