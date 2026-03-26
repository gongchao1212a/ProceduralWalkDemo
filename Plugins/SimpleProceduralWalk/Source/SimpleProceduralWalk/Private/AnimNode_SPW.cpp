// Copyright Roberto Ostinelli, 2021. All Rights Reserved.

#include "AnimNode_SPW.h"
#include "SPW.h"
#include "Animation/AnimInstanceProxy.h"
#include "GameFramework/Pawn.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Curves/CurveFloat.h"
#include "SimpleProceduralWalkInterface.h"
#include "Async/Async.h"

// log
DEFINE_LOG_CATEGORY(LogSimpleProceduralWalk);


FAnimNode_SPW::FAnimNode_SPW() : Super()
, bDebug(false)
, SkeletalMeshForwardAxis(ESimpleProceduralWalk_MeshForwardAxis::Y)
, BodyBone()
, Legs()
, LegGroups()
, StepHeight(20.f)
, StepDistanceForward(50.f)
, StepDistanceRight(30.f)
, StepSequencePercent(1.f)
, StepSlopeReductionMultiplier(.75f)
, MinStepDuration(0.15f)
, MinDistanceToUnplant(5.f)
, FixFeetTargetsAfterPercent(.5f)
, FeetTipBonesRotationInterpSpeed(15.f)
, BodyBounceMultiplier(.5f)
, BodySlopeMultiplier(.5f)
, BodyLocationInterpSpeed(10.f)
, BodyZOffset()
, bBodyRotateOnAcceleration(true)
, bBodyRotateOnFeetLocations(true)
, BodyRotationInterpSpeed(2.5f)
, BodyAccelerationRotationMultiplier(.1f)
, BodyFeetLocationsRotationMultiplier(.75f)
, MaxBodyRotation(FRotator(45.f, 0.f, 45.f))
, SolverType(ESimpleProceduralWalk_SolverType::ADVANCED)
, RadiusCheckMultiplier(1.5f)
, DistanceCheckMultiplier(1.2f)
, bStartFromTail()
, Precision(1.f)
, MaxIterations(10)
, TraceChannel()
, TraceLength(350.f)
, bTraceComplex(true)
, TraceZOffset(50.f)
{
#if WITH_EDITOR
	static ConstructorHelpers::FObjectFinder<UCurveFloat> SpeedCurveObjectFinder(TEXT("/SimpleProceduralWalk/Curves/Curve_StepSpeed.Curve_StepSpeed"));
	SpeedCurve = SpeedCurveObjectFinder.Object;
	check(SpeedCurve != nullptr);

	static ConstructorHelpers::FObjectFinder<UCurveFloat> HeightCurveObjectFinder(TEXT("/SimpleProceduralWalk/Curves/Curve_StepHeight.Curve_StepHeight"));
	HeightCurve = HeightCurveObjectFinder.Object;
	check(HeightCurve != nullptr);
#endif
}

void FAnimNode_SPW::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);

	DebugData.AddDebugItem(DebugLine);
	ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_SPW::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Entering InitializeBoneReferences."));

	Super::InitializeBoneReferences(RequiredBones);

	// init body bone
	BodyBone.Initialize(RequiredBones);
	UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("Body bone %s initialized."), *BodyBone.BoneName.ToString());

	// bones
	ParentBones.Reset();
	TipBones.Reset();
	EffectorTargets.Reset();

	for (FSimpleProceduralWalk_Leg Leg : Legs)
	{
		if (Leg.ParentBone.Initialize(RequiredBones))
		{
			// CCDIK exclude the parent bone from the solver, so in order to keep a simple UX in selecting the bones,
			// we have to add the parent's parent here.
			// NB: the fact that the parent bone is NOT root is ensured by the validation in the AnimGraphNode.
			const FCompactPoseBoneIndex ParentParentIndex = RequiredBones.GetParentBoneIndex(Leg.ParentBone.GetCompactPoseIndex(RequiredBones));
			FBoneReference ParentParentBone = RequiredBones.GetReferenceSkeleton().GetBoneName(ParentParentIndex.GetInt());

			if (ParentParentBone.Initialize(RequiredBones))
			{
				ParentBones.Emplace(ParentParentBone);
				UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("%s bone's parent initialized."), *Leg.ParentBone.BoneName.ToString());
			}
			else
			{
				UE_LOG(LogSimpleProceduralWalk, Error, TEXT("Could not initialize %s bone's parent."), *Leg.ParentBone.BoneName.ToString());
			}

			// init effector target
			FBoneSocketTarget EffectorTarget = FBoneSocketTarget(ParentParentBone.BoneName);
			EffectorTarget.InitializeBoneReferences(RequiredBones);
			EffectorTargets.Emplace(EffectorTarget);
		}
		else
		{
			UE_LOG(LogSimpleProceduralWalk, Error, TEXT("Could not initialize bone %s."), *Leg.ParentBone.BoneName.ToString());
		}

		if (Leg.TipBone.Initialize(RequiredBones))
		{
			TipBones.Emplace(Leg.TipBone);
			UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("%s bone initialized."), *Leg.TipBone.BoneName.ToString());
		}
		else
		{
			UE_LOG(LogSimpleProceduralWalk, Error, TEXT("Could not initialize bone %s."), *Leg.TipBone.BoneName.ToString());
		}
	}
}

bool FAnimNode_SPW::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("IsValidToEvaluate"));

	if (BodyBone.BoneIndex != INDEX_NONE)
	{
		if (!BodyBone.IsValidToEvaluate(RequiredBones))
		{
			UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("IsValidToEvaluate: %s is not valid"), *BodyBone.BoneName.ToString());
			return false;
		}
	}

	for (int BoneIndex = 0; BoneIndex < ParentBones.Num(); BoneIndex++)
	{
		if (!ParentBones[BoneIndex].IsValidToEvaluate(RequiredBones))
		{
			UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("IsValidToEvaluate: parent bone %s is not valid"), *ParentBones[BoneIndex].BoneName.ToString());
			return false;
		}
		if (!TipBones[BoneIndex].IsValidToEvaluate(RequiredBones))
		{
			UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("IsValidToEvaluate: tip bone %s is not valid"), *TipBones[BoneIndex].BoneName.ToString());
			return false;
		}
		if (!RequiredBones.BoneIsChildOf(TipBones[BoneIndex].BoneIndex, ParentBones[BoneIndex].BoneIndex))
		{
			UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("IsValidToEvaluate: tip bone %s is not child of parent bone %s")
				, *TipBones[BoneIndex].BoneName.ToString()
				, *ParentBones[BoneIndex].BoneName.ToString());
			return false;
		}
	}

	if (!IsValid(SkeletalMeshComponent))
	{
		UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("IsValidToEvaluate: SkeletalMeshComponent is not valid."));
		return false;
	}

	if (!IsValid(SkeletalMeshComponent->SkeletalMesh))
	{
		UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("IsValidToEvaluate: SkeletalMesh is not valid."));
		return false;
	}

	if (Precision <= 0)
	{
		UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("IsValidToEvaluate: Precision is not valid."));
		return false;
	}

	if (bHasErrors)
	{
		UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("2222"));
		return false;
	}

	UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("IsValidToEvaluate is true."));
	return true;
}

void FAnimNode_SPW::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Entering Initialize_AnyThread."));

	Super::Initialize_AnyThread(Context);

	// set common
	SkeletalMeshComponent = Context.AnimInstanceProxy->GetSkelMeshComponent();
	WorldContext = SkeletalMeshComponent->GetWorld();

	// owner
	AActor* SkeletalMeshOwner = SkeletalMeshComponent->GetOwner();

	// set is playing & is preview
	if (!WorldContext->IsPlayInEditor() && WorldContext->IsEditorWorld())
	{
		bIsPlaying = false;
		bIsEditorAnimPreview = !SkeletalMeshOwner->IsA(APawn::StaticClass());
	}
	else
	{
		bIsPlaying = true;
		bIsEditorAnimPreview = false;
	}
	UE_LOG(LogSimpleProceduralWalk, Log, TEXT("Is playing: %d, is in editor: %d"), bIsPlaying, bIsEditorAnimPreview);

	if (bIsPlaying)
	{
		// get pawn
		OwnerPawn = Cast<APawn>(SkeletalMeshOwner);
		if (!IsValid(OwnerPawn))
		{
			bHasErrors = true;
			UE_LOG(LogSimpleProceduralWalk, Error, TEXT("Owner actor must be a Pawn / Character."));
		}

		if (Legs.Num() == 0)
		{
			bHasErrors = true;
			UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("No legs have been specified, so animation is disabled."));
		}

		if (LegGroups.Num() == 0)
		{
			bHasErrors = true;
			UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("No leg groups have been specified, so animation is disabled."));
		}

		// get half height
		OwnerHalfHeight = ((OwnerPawn->GetActorLocation() - SkeletalMeshComponent->GetComponentLocation()) * OwnerPawn->GetActorUpVector()).Size();
		UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("OwnerHalfHeight: %f"), OwnerHalfHeight);

		// check & init
		if (Legs.Num() > 0 && LegGroups.Num() > 0)
		{
			UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Initializing computations."));
			Initialize_Computations();
			UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Initializing CCDIK."));
			Initialize_CCDIK();
		}
		else
		{
			// flag
			bHasErrors = true;
			UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("No legs or groups have been specified, so animation is disabled."));
		}
	}
	else
	{
		UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("111111111"));
	}
}

void FAnimNode_SPW::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("Entering EvaluateSkeletalControl_AnyThread."));

	Super::EvaluateSkeletalControl_AnyThread(Output, OutBoneTransforms);

	// set common
	SkeletalMeshComponent = Output.AnimInstanceProxy->GetSkelMeshComponent();
	WorldContext = SkeletalMeshComponent->GetWorld();

	if (bIsPlaying)
	{
		// falling events
		if (bIsInitialized)
		{
			if (!IsValid(OwnerPawn->GetMovementBase()))
			{
				/* -> not standing on a base -> falling */
				if (!bIsFalling)
				{
					/* -> triggered once after starting to fall */
					UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("Pawn started falling."));
					// reset feet targets & locations
					ResetFeetTargetsAndLocations();
					// track falling state
					bIsFalling = true;
				}
			}
			else
			{
				/* -> not falling */
				if (bIsFalling)
				{
					/* -> triggered once after landing on ground */
					UE_LOG(LogSimpleProceduralWalk, Warning, TEXT("Pawn landed."));
					// reset falling state
					bIsFalling = false;
					// reset feet targets & locations
					ResetFeetTargetsAndLocations();
					// interface
					CallLandedInterfaces();
				}
			}
		}

		// compute procedurals
		Evaluate_Computations();

		// body
		Evaluate_BodySolver(Output);

		// legs
		Evaluate_CCDIKSolver(Output);
	}
	else if (bIsEditorAnimPreview)
	{
		EditorDebugShow(SkeletalMeshComponent->GetOwner());
	}
}

void FAnimNode_SPW::UpdateInternal(const FAnimationUpdateContext& Context)
{
	UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("Entering UpdateInternal."));

	Super::UpdateInternal(Context);

	WorldDeltaSeconds = Context.GetDeltaTime();
}

void FAnimNode_SPW::CallLandedInterfaces()
{
	UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Calling OnLanded interfaces."));
	// pawn
	if (OwnerPawn->GetClass()->ImplementsInterface(USimpleProceduralWalkInterface::StaticClass()))
	{
		CallLandedInterface(OwnerPawn);
	}
	// anim instance
	if (SkeletalMeshComponent->GetAnimInstance()->GetClass()->ImplementsInterface(USimpleProceduralWalkInterface::StaticClass()))
	{
		CallLandedInterface(SkeletalMeshComponent->GetAnimInstance());
	}
}

void FAnimNode_SPW::CallLandedInterface(UObject* InterfaceOwner)
{
	FVector Location = OwnerPawn->GetActorLocation();

	AsyncTask(ENamedThreads::GameThread, [=]() {
		ISimpleProceduralWalkInterface::Execute_OnPawnLanded(InterfaceOwner, Location);
	});
}

#if WITH_EDITOR
void FAnimNode_SPW::CCDIK_ResizeRotationLimitPerJoints(int32 LegIndex, int32 NewSize)
{
	if (NewSize == 0)
	{
		Legs[LegIndex].RotationLimitPerJoints.Reset();
	}
	else if (Legs[LegIndex].RotationLimitPerJoints.Num() != NewSize)
	{
		int32 StartIndex = Legs[LegIndex].RotationLimitPerJoints.Num();
		Legs[LegIndex].RotationLimitPerJoints.SetNum(NewSize);
		for (int32 Index = StartIndex; Index < Legs[LegIndex].RotationLimitPerJoints.Num(); ++Index)
		{
			Legs[LegIndex].RotationLimitPerJoints[Index] = 30.f;
		}
	}
}
#endif 
