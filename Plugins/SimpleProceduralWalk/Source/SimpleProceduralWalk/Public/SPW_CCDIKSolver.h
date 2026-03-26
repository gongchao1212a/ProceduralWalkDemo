// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BoneIndices.h"
#include "SPW_CCDIKSolver.generated.h"

/** Transient structure for CCDIK node evaluation */
USTRUCT()
struct FSPW_CCDIKChainLink
{
	GENERATED_USTRUCT_BODY()

public:
	/** Transform of bone in component space. */
	FTransform Transform = FTransform(FRotator(0.f), FVector(0.f), FVector(1.f));

	/** Transform of bone in local space. This is mutable as their component space changes or parents*/
	FTransform LocalTransform = FTransform(FRotator(0.f), FVector(0.f), FVector(1.f));

	/** Transform Index that this control will output */
	int32 TransformIndex = 0;

	/** Child bones which are overlapping this bone.
	 * They have a zero length distance, so they will inherit this bone's transformation. */
	TArray<int32> ChildZeroLengthTransformIndices;

	float CurrentAngleDelta = 0.f;

	FSPW_CCDIKChainLink()
		: TransformIndex(INDEX_NONE)
		, CurrentAngleDelta(0.f)
	{
	}

	FSPW_CCDIKChainLink(const FTransform& InTransform, const FTransform& InLocalTransform, const int32& InTransformIndex)
		: Transform(InTransform)
		, LocalTransform(InLocalTransform)
		, TransformIndex(InTransformIndex)
		, CurrentAngleDelta(0.f)
	{
	}
};
