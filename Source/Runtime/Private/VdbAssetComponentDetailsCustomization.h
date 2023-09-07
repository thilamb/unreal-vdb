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

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UVdbAssetComponent;

class FVdbAssetComponentDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	void ForceRefresh();

	struct SNameType
	{
		FString Name;
		FString Type;
	};
	typedef TSharedPtr<FVdbAssetComponentDetails::SNameType> SNameTypePtr;

private:

	static TArray<TSharedPtr<FString>> GridNamesStrings;
	static TArray<TSharedPtr<SNameType>> GridNamesTypes;
	TSharedRef<ITableRow> HandleGenerateRowCombo(TSharedPtr<FString> SpecifierName, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> HandleGenerateRowList(TSharedPtr<SNameType> NameType, const TSharedRef<STableViewBase>& OwnerTable);

	FText GetCurrentDensityGridName() const;
	FText GetCurrentTemperatureGridName() const;
	FText GetCurrentColorGridName() const;
	void OnDensityGridSelected(TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo);
	void OnTemperatureGridSelected(TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo);
	void OnColorGridSelected(TSharedPtr<FString> SpecifierName, ESelectInfo::Type SelectInfo);

	UVdbAssetComponent* CurrentComponent = nullptr;
	IDetailLayoutBuilder* LayoutBuilder = nullptr;
};

#endif
