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

THIRD_PARTY_INCLUDES_START

#ifdef _MSC_VER
#pragma warning (push)
#pragma warning (disable : 4146) // unary minus operator applied to unsigned type, result still unsigned
#pragma warning (disable : 6297) // Arithmetic overflow:  32-bit value is shifted, then cast to 64-bit value.  Results might not be an expected value.
#endif

#ifndef M_PI
#define M_PI    3.14159265358979323846
#define LOCAL_M_PI 1
#endif

#ifndef M_PI_2
#define M_PI_2  1.57079632679489661923 // pi/2
#define LOCAL_M_PI_2 1
#endif

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/GridHandle.h>
#include <openvdb/openvdb.h>

THIRD_PARTY_INCLUDES_END

struct FVdbGridInfo;

namespace VdbFileUtils
{
	TArray<TSharedPtr<FVdbGridInfo> > ParseVdbFromFile(const FString& Path);

	nanovdb::GridHandle<> LoadVdbFromFile(const FString& Filepath, const FName& GridName);

	openvdb::GridBase::Ptr OpenVdb(const FString& Path, const FName& GridName);

	nanovdb::GridHandle<> LoadVdb(const FString& Path, const FName& GridName, nanovdb::GridType Type);
};