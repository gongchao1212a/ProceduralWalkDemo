// Copyright Roberto Ostinelli, 2021. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "Kismet/KismetSystemLibrary.h"
#include "SPW.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSimpleProceduralWalk, Log, All);


USTRUCT()
struct SIMPLEPROCEDURALWALK_API FSimpleProceduralWalk_Leg
{
	GENERATED_USTRUCT_BODY()

public:
	/** The beginning bone of the leg (the upperhand / the calf). */
	UPROPERTY(EditAnyWhere, Category = "Skeletal Control")
		FBoneReference ParentBone;
	/** The end bone of the leg (the hand / foot). */
	UPROPERTY(EditAnyWhere, Category = "Skeletal Control")
		FBoneReference TipBone;
	/** The tip bone offset. */
	UPROPERTY(EditAnyWhere, Category = "Skeletal Control")
		FVector Offset = FVector(0.f);
	/** Should rotation limits be enabled? */
	UPROPERTY(EditAnyWhere, Category = "Skeletal Control", meta = (DefaultValue = "false"))
		bool bEnableRotationLimits = false;
	/**
	 * Symmetry rotation limits per joint.
	 * Index 0 matches with parent bone, and the last index matches with tip bone.
	 */
	UPROPERTY(EditAnywhere, EditFixedSize, Category = "Skeletal Control")
		TArray<float> RotationLimitPerJoints;
};

USTRUCT()
struct SIMPLEPROCEDURALWALK_API FSimpleProceduralWalk_LegGroup
{
	GENERATED_USTRUCT_BODY()

public:
	/** The list of the indices of the legs that belong to the group. */
	UPROPERTY(EditAnyWhere, Category = "Walk Cycle")
		TArray<int32> LegIndices;
};

USTRUCT()
struct SIMPLEPROCEDURALWALK_API FSimpleProceduralWalk_RotationLimitsPerJoint
{
	GENERATED_USTRUCT_BODY()

public:
	UPROPERTY(EditAnyWhere, Category = "Skeletal Control")
		TArray<float> RotationLimits;
};

USTRUCT()
struct SIMPLEPROCEDURALWALK_API FSimpleProceduralWalk_LegData
{
	GENERATED_USTRUCT_BODY()

public:
	FVector FootLocation = FVector(0.f);
	FVector FootTarget = FVector(0.f);
	FRotator FootTargetRotation = FRotator(0.f);
	FVector FootUnplantLocation = FVector(0.f);
	FVector TipBoneOriginalRelLocation = FVector(0.f);
	int32 GroupIndex = 0.f;
	bool bIsForward = false;
	bool bIsBackwards = false;
	bool bIsRight = false;
	bool bIsLeft = false;
	float Length = 0.f;
	bool bEnableIK = false;
	// support
	FHitResult LastHit;
	UPrimitiveComponent* SupportComp = nullptr;
	FTransform SupportCompPreviousTransform = FTransform(FRotator(0.f), FVector(0.f), FVector(1.f));
	FVector SupportCompDelta = FVector(0.f);
	FVector RelLocationToSupportComp = FVector(0.f);
};

USTRUCT()
struct SIMPLEPROCEDURALWALK_API FSimpleProceduralWalk_LegGroupData
{
	GENERATED_USTRUCT_BODY()

public:
	bool bIsUnplanted = false;
	float StepPercent = 0.f;
};

UENUM(BlueprintType)
enum class ESimpleProceduralWalk_MeshForwardAxis : uint8
{
	X = 0 UMETA(DisplayName = "X"),
	NX = 1 UMETA(DisplayName = "-X"),
	Y = 2 UMETA(DisplayName = "Y"),
	NY = 3 UMETA(DisplayName = "-Y"),
};

UENUM(BlueprintType)
enum class ESimpleProceduralWalk_SolverType : uint8
{
	BASIC = 0 UMETA(DisplayName = "Basic"),
	ADVANCED = 1 UMETA(DisplayName = "Advanced"),
};
