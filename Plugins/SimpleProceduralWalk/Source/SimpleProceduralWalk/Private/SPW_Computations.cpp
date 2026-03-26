// Copyright Roberto Ostinelli, 2021. All Rights Reserved.

#include "AnimNode_SPW.h"
#include "Async/Async.h"
#include "Curves/CurveFloat.h"
#include "SimpleProceduralWalkInterface.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"
#include "Kismet/KismetSystemLibrary.h"

// constants
static const int FRAMES_TO_SKIP_ON_INIT = 2;
static const float STEP_PERCENT_AT_BEGINNING = .15f;
static const float STEP_PERCENT_AT_END = .85f;
static const float SPEED_THRESHOLD_MIN = 2.f;


/*
 * INITIALIZE
 */
void FAnimNode_SPW::Initialize_Computations()
{
	// init legs
	int32 FeetDataSize = Legs.Num();
	LegsData.SetNum(FeetDataSize);

	// init groups
	int32 FeetGroupsSize = LegGroups.Num();
	GroupsData.SetNum(FeetGroupsSize);

	// init feet groups
	for (int GroupIndex = 0; GroupIndex < LegGroups.Num(); GroupIndex++)
	{
		for (int LegIndex : LegGroups[GroupIndex].LegIndices)
		{
			LegsData[LegIndex].GroupIndex = GroupIndex;
		}
	}

	// solver
	RadiusCheck = RadiusCheckMultiplier * FMath::Max(StepDistanceForward, StepDistanceRight);

	UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Computations initialized."));
}

/*
 * TICK
 */
void FAnimNode_SPW::Evaluate_Computations()
{
	if (SkippedFrames < FRAMES_TO_SKIP_ON_INIT)
	{
		// skip frame(s)
		++SkippedFrames;
		return;
	}
	if (SkippedFrames == FRAMES_TO_SKIP_ON_INIT)
	{
		// init feet data after first frames (so that actor is correctly positioned in the world)
		for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
		{
			FSimpleProceduralWalk_Leg Leg = Legs[LegIndex];

			UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Initializing %s bone data."), *Leg.TipBone.BoneName.ToString());

			// get relative parent bone position
			FVector ParentBoneRelLocationWithOffsets = (SkeletalMeshComponent->GetSocketTransform(Leg.ParentBone.BoneName, RTS_Actor)).GetLocation() + Leg.Offset;

			// compute relative foot position, we assume that feet are located at the edge of the model
			// (we can use Z since in actor space)
			FVector TipBoneRelLocation = ParentBoneRelLocationWithOffsets;
			TipBoneRelLocation.Z = -OwnerHalfHeight;

			// save feet length
			LegsData[LegIndex].Length = ParentBoneRelLocationWithOffsets.Z - TipBoneRelLocation.Z;

			// save relative position
			LegsData[LegIndex].TipBoneOriginalRelLocation = TipBoneRelLocation;

			// save in world space
			FVector TipBoneLocation = (FTransform(FRotator(0.f), TipBoneRelLocation, FVector(1.f)) * OwnerPawn->GetActorTransform()).GetLocation();
			LegsData[LegIndex].FootTarget = TipBoneLocation;
			LegsData[LegIndex].FootLocation = TipBoneLocation;

			if (bDebug)
			{
				AsyncTask(ENamedThreads::GameThread, [=]() {
					DrawDebugSphere(WorldContext, TipBoneLocation, 12.f, 12, FColor::Purple, false, 5.f);
				});
			}

			// Forward / Backward
			if (FMath::IsNearlyEqual(ParentBoneRelLocationWithOffsets.X, 0.f, 0.001f))
			{
				LegsData[LegIndex].bIsForward = true;
				LegsData[LegIndex].bIsBackwards = true;
			}
			else
			{
				LegsData[LegIndex].bIsForward = ParentBoneRelLocationWithOffsets.X > 0;
				LegsData[LegIndex].bIsBackwards = ParentBoneRelLocationWithOffsets.X < 0;
			}

			// Right / Left
			if (FMath::IsNearlyEqual(ParentBoneRelLocationWithOffsets.Y, 0.f, 0.001f))
			{
				LegsData[LegIndex].bIsRight = true;
				LegsData[LegIndex].bIsLeft = true;
			}
			else
			{
				LegsData[LegIndex].bIsRight = ParentBoneRelLocationWithOffsets.Y > 0;
				LegsData[LegIndex].bIsLeft = ParentBoneRelLocationWithOffsets.Y < 0;
			}
		}

		// done
		SkippedFrames = FRAMES_TO_SKIP_ON_INIT + 1;
		bIsInitialized = true;
	}

	// common
	UpdatePawnVariables();
	SetSupportCompDeltas();

	// walk
	SetFeetTargetLocations();

	if (bIsFalling)
	{
		// falling -> compute only feet locations
		ComputeFeet();
	}
	else
	{
		// on ground
		SetCurrentGroupUnplanted();
		ComputeFeet();
		SetGroupsPlanted();
	}

	// body
	ComputeBodyTransform();

	// debug
	DebugShow();
}

/*
 * -> UPDATE VARIABLES
 */
void FAnimNode_SPW::UpdatePawnVariables()
{
	FVector PawnVelocity = OwnerPawn->GetVelocity();

	// Speed
	Speed = PawnVelocity.Size();
	if (Speed <= SPEED_THRESHOLD_MIN)
	{
		Speed = 0;
		PawnVelocity = FVector(0.f);
	}

	// %
	PawnVelocity.Normalize();
	ForwardPercent = UKismetMathLibrary::MapRangeClamped(
		UKismetMathLibrary::DegAcos(FVector::DotProduct(OwnerPawn->GetActorForwardVector(), PawnVelocity))
		, 0.f, 180.f
		, 1.f, -1.f);
	RightPercent = UKismetMathLibrary::MapRangeClamped(
		UKismetMathLibrary::DegAcos(FVector::DotProduct(OwnerPawn->GetActorRightVector(), PawnVelocity))
		, 0.f, 180.f
		, 1.f, -1.f);

	// Rotation
	YawDelta = UKismetMathLibrary::NormalizedDeltaRotator(OwnerPawn->GetActorRotation(), PreviousRotation).Yaw;
	PreviousRotation = OwnerPawn->GetActorRotation();

	// Current step length
	CurrentStepLength =
		(
			// portion of step forward
			abs(ForwardPercent * StepDistanceForward)
			// portion of step right
			+ abs(RightPercent * StepDistanceRight)
			// portion of step right based on angular speed
			+ abs(StepDistanceRight * FMath::Clamp(YawDelta / 360, -1.f, 1.f))
			)
		// reduce distance due to slope
		* GetReductionSlopeMultiplier();

	// Current step duration
	float SpeedWithAngular = Speed + abs(YawDelta);
	if (SpeedWithAngular > 5)	// Avoid unnatural step durations
	{
		CurrentStepDuration = CurrentStepLength / SpeedWithAngular;
	}
	else
	{
		CurrentStepDuration = MinStepDuration;
	}

	// Acceleration
	ForwardAcceleration = ((ForwardPercent * Speed) - (PreviousForwardPercent * PreviousSpeed)) / WorldDeltaSeconds;
	RightAcceleration = ((RightPercent * Speed) - (PreviousRightPercent * PreviousSpeed)) / WorldDeltaSeconds;
	PreviousSpeed = Speed;
	PreviousForwardPercent = ForwardPercent;
	PreviousRightPercent = RightPercent;
}

/*
 * -> DELTAS FOR MOVING / ROTATING PLATFORMS
 */
void FAnimNode_SPW::SetSupportCompDeltas()
{
	for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
	{
		if (IsValid(LegsData[LegIndex].SupportComp))
		{
			// compute world locations
			FVector PreviousLocation = (FTransform(FRotator(0.f), LegsData[LegIndex].RelLocationToSupportComp, FVector(1.f)) * LegsData[LegIndex].SupportCompPreviousTransform).GetLocation();
			FVector NewLocation = (FTransform(FRotator(0.f), LegsData[LegIndex].RelLocationToSupportComp, FVector(1.f)) * LegsData[LegIndex].SupportComp->GetComponentTransform()).GetLocation();

			// save delta
			LegsData[LegIndex].SupportCompDelta = NewLocation - PreviousLocation;

			// save previous transform
			LegsData[LegIndex].SupportCompPreviousTransform = LegsData[LegIndex].SupportComp->GetComponentTransform();
		}
		else
		{
			// set to 0
			LegsData[LegIndex].SupportCompDelta = FVector(0.f);
		}
	}
}

/*
 * -> FEET TARGETS
 */
void FAnimNode_SPW::SetFeetTargetLocations()
{
	for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
	{
		SetFootTargetLocation(LegIndex);
	}
}

void FAnimNode_SPW::SetFootTargetLocation(int32 LegIndex)
{
	// get foot data
	FSimpleProceduralWalk_Leg Leg = Legs[LegIndex];

	// Parent Bone Location
	FVector ParentBoneLocation = SkeletalMeshComponent->GetSocketLocation(Leg.ParentBone.BoneName);

	// Forward offset (based on forward speed & optional offset)
	FVector ForwardOffset = OwnerPawn->GetActorForwardVector() * ((StepDistanceForward * ForwardPercent) + Leg.Offset.X);

	// Right offset (based on right speed & optional offset)
	FVector RightOffset = OwnerPawn->GetActorRightVector() * ((StepDistanceRight * RightPercent) + Leg.Offset.Y);

	// Locations
	FVector StartLocationWithoutZOffset = ParentBoneLocation + ForwardOffset + RightOffset;
	FVector StartLocation = StartLocationWithoutZOffset + OwnerPawn->GetActorUpVector() * TraceZOffset;
	FVector EndLocation = StartLocationWithoutZOffset - OwnerPawn->GetActorUpVector() * TraceLength;

	// init hit
	bool bIsHit = false;
	bool bIsFootHoldHit;
	FHitResult Hit;

	// prepare ignore actors
	TArray<AActor*> ActorsToIgnore;
	ActorsToIgnore.Add(OwnerPawn);

	// line hit
	bIsHit = UKismetSystemLibrary::LineTraceSingle(WorldContext
		, StartLocation
		, EndLocation
		, TraceChannel
		, bTraceComplex
		, ActorsToIgnore
		, EDrawDebugTrace::None
		, Hit
		, true
	);

	if (SolverType == ESimpleProceduralWalk_SolverType::BASIC)
	{
		// ---------- \/ BASIC ----------
		if (bDebug)
		{
			FTransform DebugTransform = FTransform(OwnerPawn->GetActorRotation(), Hit.ImpactPoint, FVector(1.f));

			// line
			AsyncTask(ENamedThreads::GameThread, [=]() {
				// draw line
				DrawDebugLine(WorldContext, StartLocation, EndLocation, (bIsHit ? FColor::Green : FColor::Red));
				// hit point
				if (bIsHit)
				{
					DrawDebugSolidBox(WorldContext, FBox(FVector(-2.f, -2.f, 0.f), FVector(2.f, 2.f, 2.f)), FColor::Green, DebugTransform);
				}
			});
		}
	}
	else
	{
		// ---------- \/ ADVANCED ----------
		bool bIsUsingBasic;

		// distance between start location (without traceZoffset) and impact point
		float ZDistanceToLineHit = (StartLocationWithoutZOffset - Hit.ImpactPoint).Size();

		// should we also foot hold hit?
		bool bIsTooDistant = ZDistanceToLineHit > (LegsData[LegIndex].Length * DistanceCheckMultiplier);

		if (!bIsHit || bIsTooDistant)
		{
			/* -> no hit or hit too distant -> do sphere trace */
			TArray<FHitResult> FootHoldHits;

			bIsFootHoldHit = UKismetSystemLibrary::SphereTraceMulti(WorldContext
				, StartLocation
				, EndLocation
				, RadiusCheck
				, TraceChannel
				, bTraceComplex
				, ActorsToIgnore
				, EDrawDebugTrace::None
				, FootHoldHits
				, true);

			if (FootHoldHits.Num() > 0)
			{
				// at least 1 hit
				FHitResult FootHoldBestHit;

				// filter based on:
				//   . distance < line trace distance
				//   . hit normals not perpendicular to pawn's up vector (i.e. walls are less appealing)
				float MinZ = (TraceLength + TraceZOffset) * 2;
				for (FHitResult FootHoldHit : FootHoldHits)
				{
					// compute distance
					float ZDistanceToFootHoldHit = (StartLocationWithoutZOffset - FootHoldHit.ImpactPoint).Size();
					if (ZDistanceToFootHoldHit < ZDistanceToLineHit)
					{
						/* -> it's closer than line hit */
						// check min z distance weighted by surface normals
						float CurrentHitZ =
							// distance
							((StartLocationWithoutZOffset - FootHoldHit.ImpactPoint) * OwnerPawn->GetActorUpVector()).Size()
							// weighted by 1 - dot product (so 1 means parallel to up vector, i.e. not a wall)
							* (1.f - FVector::DotProduct(FootHoldHit.ImpactNormal, OwnerPawn->GetActorUpVector()));

						if (CurrentHitZ < MinZ)
						{
							/* -> save */
							MinZ = CurrentHitZ;
							FootHoldBestHit = FootHoldHit;
						}
					}
				}

				if (FootHoldBestHit.bBlockingHit)
				{
					/* -> use foothold */
					bIsUsingBasic = false;
					bIsHit = true;
					Hit = FootHoldBestHit;
				}
				else
				{
					/* -> no valid foothold hits, keep single line result */
					bIsUsingBasic = true;
				}
			}
			else
			{
				/* -> if no foothold hits, keep single line result */
				bIsUsingBasic = true;
			}
		}
		else
		{
			/* -> keep foot result */
			bIsUsingBasic = true;
		}

		if (bDebug)
		{
			FVector DebugCapsuleCenter = FMath::Lerp(StartLocation, EndLocation, .5f);
			float DebugCapsuleHalfHeight = FVector::Distance(StartLocation, EndLocation) / 2;
			FRotator Rot = UKismetMathLibrary::MakeRotationFromAxes(OwnerPawn->GetActorForwardVector()
				, OwnerPawn->GetActorRightVector()
				, OwnerPawn->GetActorUpVector());
			FQuat DebugCapsuleRotator = FQuat(Rot);

			FTransform DebugHitTransform = FTransform(OwnerPawn->GetActorRotation(), Hit.ImpactPoint, FVector(1.f));

			AsyncTask(ENamedThreads::GameThread, [=]() {
				// line
				DrawDebugLine(WorldContext, StartLocation, EndLocation, bIsUsingBasic ? (bIsHit ? FColor::Green : FColor::Red) : FColor::Silver);
				// draw foothold
				DrawDebugCapsule(WorldContext, DebugCapsuleCenter, DebugCapsuleHalfHeight, RadiusCheck, DebugCapsuleRotator
					, bIsUsingBasic ? FColor::Silver : (bIsHit ? FColor::Green : FColor::Red), false, -1.f, 0, .5f);
				// hit point
				if (bIsHit)
				{
					DrawDebugSolidBox(WorldContext, FBox(FVector(-2.f, -2.f, 0.f), FVector(2.f, 2.f, 2.f)), FColor::Green, DebugHitTransform);
				}
			});
		}
	}

	// init rotation
	FRotator TargetFootRotationCS;

	// result
	if (bIsHit)
	{
		UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("HIT for %s at %s on component %s")
			, *Leg.ParentBone.BoneName.ToString()
			, *Hit.ImpactPoint.ToString()
			, *UKismetSystemLibrary::GetDisplayName(Hit.GetComponent()));

		// get desired foot rotation
		if (
			/* -> leg is planted */
			!IsLegUnplanted(LegIndex)
			// leg is unplanted but it's at the beginning or the end of the step
			|| (IsLegUnplanted(LegIndex) && (GetLegStepPercent(LegIndex) < STEP_PERCENT_AT_BEGINNING || GetLegStepPercent(LegIndex) > STEP_PERCENT_AT_END))
			)
		{
			// get hit rotation from normals
			FRotator TargetFootRotationWorld = UKismetMathLibrary::MakeRotFromZX(Hit.ImpactNormal, SkeletalMeshComponent->GetForwardVector());
			TargetFootRotationCS = UKismetMathLibrary::InverseTransformRotation(SkeletalMeshComponent->GetComponentTransform(), TargetFootRotationWorld);
		}
		else
		{
			// no added rotation
			TargetFootRotationCS = FRotator(0.f, 0.f, 0.f);
		}

		// set target
		FVector FootTarget = Hit.ImpactPoint + FVector(0, 0, Leg.Offset.Z);

		if (IsLegUnplanted(LegIndex))
		{
			/* -> leg is un planted */
			if (GetLegStepPercent(LegIndex) < FixFeetTargetsAfterPercent)
			{
				/* -> not too far along the step, update target */
				LegsData[LegIndex].FootTarget = FootTarget;
			}
			else
			{
				/* -> too far along the step, do not update target to avoid jiggling */
				// add moving platform to target
				LegsData[LegIndex].FootTarget += LegsData[LegIndex].SupportCompDelta;
			}
		}
		else
		{
			/* -> leg is planted */
			// update target
			LegsData[LegIndex].FootTarget = FootTarget;
		}
	}
	else
	{
		UE_LOG(LogSimpleProceduralWalk, VeryVerbose, TEXT("NO HIT for %s"), *Leg.ParentBone.BoneName.ToString());

		// set target to original foot location in world space
		FVector FootTarget = (FTransform(FRotator(0.f), LegsData[LegIndex].TipBoneOriginalRelLocation, FVector(1.f)) * OwnerPawn->GetActorTransform()).GetLocation();
		LegsData[LegIndex].FootTarget = FootTarget;

		// no rotation
		TargetFootRotationCS = FRotator(0.f, 0.f, 0.f);
	}

	// interp & save
	LegsData[LegIndex].FootTargetRotation = FMath::RInterpTo(LegsData[LegIndex].FootTargetRotation, TargetFootRotationCS, WorldDeltaSeconds, FeetTipBonesRotationInterpSpeed);

	// set IK enabled
	LegsData[LegIndex].bEnableIK = bIsHit;

	// save last hit
	LegsData[LegIndex].LastHit = Hit;
}

/*
 * -> UNPLANT
 */
void FAnimNode_SPW::SetCurrentGroupUnplanted()
{
	if (GroupsData[CurrentGroupIndex].bIsUnplanted)
	{
		// exit if group is already unplanted
		return;
	}

	// is any foot in current group distant enough to unplant?
	bool bIsAtLeastOneFootFarEnough = false;
	for (int LegIndex : LegGroups[CurrentGroupIndex].LegIndices)
	{
		if (FVector::Dist(LegsData[LegIndex].FootLocation, LegsData[LegIndex].FootTarget) >= MinDistanceToUnplant)
		{
			bIsAtLeastOneFootFarEnough = true;
			break;
		}
	}

	if (!bIsAtLeastOneFootFarEnough)
	{
		return;
	}

	// is previous group far enough along the step percentage?
	int PreviousGroupIndex = CurrentGroupIndex - 1;
	if (PreviousGroupIndex < 0)
	{
		PreviousGroupIndex = LegGroups.Num() - 1;
	}

	if (GroupsData[PreviousGroupIndex].bIsUnplanted)
	{
		// previous group is unplanted
		if (GroupsData[PreviousGroupIndex].StepPercent < StepSequencePercent)
		{
			return;
		}
	}

	/* UNPLANT GROUP! */
	UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Unplanting group with index %d"), CurrentGroupIndex);

	// set group as unplanted
	GroupsData[CurrentGroupIndex].bIsUnplanted = true;

	// reset step %
	GroupsData[CurrentGroupIndex].StepPercent = 0.f;

	// loop group feet
	for (int LegIndex : LegGroups[CurrentGroupIndex].LegIndices)
	{
		// set feet unplant locations
		LegsData[LegIndex].FootUnplantLocation = LegsData[LegIndex].FootLocation;

		// save support comp & data
		SetSupportComponentData(LegIndex, LegsData[LegIndex].FootUnplantLocation);
	}

	// call interface events
	CallStepInterfaces(CurrentGroupIndex, false);

	// set next group that will check to unplant
	SetNextCurrentGroupIndex();
}

/*
 * -> MOVE FEET
 */
void FAnimNode_SPW::ComputeFeet()
{
	for (int GroupIndex = 0; GroupIndex < LegGroups.Num(); GroupIndex++)
	{
		if (bIsFalling)
		{
			// instantly update locations for all feet in group
			for (int LegIndex : LegGroups[GroupIndex].LegIndices)
			{
				LegsData[LegIndex].FootLocation = LegsData[LegIndex].FootTarget;
			}
		}
		else
		{
			if (GroupsData[GroupIndex].bIsUnplanted)
			{
				/* -> foot is unplanted */
				// increment group step %
				GroupsData[GroupIndex].StepPercent = FMath::Clamp(GroupsData[GroupIndex].StepPercent + (WorldDeltaSeconds / CurrentStepDuration), 0.f, 1.f);

				// get curve data
				float InterpSpeed = SpeedCurve->GetFloatValue(GroupsData[GroupIndex].StepPercent);
				float RelativeZ = HeightCurve->GetFloatValue(GroupsData[GroupIndex].StepPercent) * StepHeight;

				// animate all feet in group
				for (int LegIndex : LegGroups[GroupIndex].LegIndices)
				{
					LegsData[LegIndex].FootLocation =
						// interp location vector
						FMath::Lerp(LegsData[LegIndex].FootUnplantLocation, LegsData[LegIndex].FootTarget, InterpSpeed)
						// add height
						+ RelativeZ * OwnerPawn->GetActorUpVector();

					// add moving platform delta
					LegsData[LegIndex].FootUnplantLocation += LegsData[LegIndex].SupportCompDelta;
				}
			}
			else
			{
				/* -> foot is planted */
				for (int LegIndex : LegGroups[GroupIndex].LegIndices)
				{
					// check if too far
					float FootDistanceFromLocation = FVector::Dist(
						// foot location
						LegsData[LegIndex].FootLocation
						// actual socket
						, SkeletalMeshComponent->GetSocketLocation(Legs[LegIndex].TipBone.BoneName));

					if (FootDistanceFromLocation <= (MinDistanceToUnplant * DistanceCheckMultiplier))
					{
						/* -> foot not too far, add support movement */
						LegsData[LegIndex].FootLocation += LegsData[LegIndex].SupportCompDelta;
					}
				}
			}
		}
	}
}

/*
 * -> PLANT
 */
void FAnimNode_SPW::SetGroupsPlanted()
{
	for (int GroupIndex = 0; GroupIndex < LegGroups.Num(); GroupIndex++)
	{
		if (GroupsData[GroupIndex].bIsUnplanted)
		{
			// group not planted
			if (GroupsData[GroupIndex].StepPercent == 1.f)
			{
				/* group has reached end of step -> PLANT GROUP! */
				UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Planting group with index %d"), GroupIndex);

				// set original feet components
				for (int LegIndex : LegGroups[GroupIndex].LegIndices)
				{
					// save support comp & data
					SetSupportComponentData(LegIndex, LegsData[LegIndex].FootLocation);
				}

				// set group as planted
				GroupsData[GroupIndex].bIsUnplanted = false;

				// call interface events
				CallStepInterfaces(GroupIndex, true);
			}
		}
	}
}

void FAnimNode_SPW::ComputeBodyTransform()
{
	FVector AverageFeetTargetsForward;
	FVector AverageFeetTargetsBackwards;
	FVector AverageFeetTargetsRight;
	FVector AverageFeetTargetsLeft;

	GetAverageFeetTargets(&AverageFeetTargetsForward
		, &AverageFeetTargetsBackwards
		, &AverageFeetTargetsRight
		, &AverageFeetTargetsLeft);

	// debug
	if (bDebug && bIsPlaying)
	{
		FVector AverageFeetTargetsForwardWorld = (FTransform(FRotator(0.f), AverageFeetTargetsForward, FVector(1.f)) * OwnerPawn->GetActorTransform()).GetLocation();
		FVector AverageFeetTargetsBackwardsWorld = (FTransform(FRotator(0.f), AverageFeetTargetsBackwards, FVector(1.f)) * OwnerPawn->GetActorTransform()).GetLocation();
		FVector AverageFeetTargetsRightdWorld = (FTransform(FRotator(0.f), AverageFeetTargetsRight, FVector(1.f)) * OwnerPawn->GetActorTransform()).GetLocation();
		FVector AverageFeetTargetsLeftWorld = (FTransform(FRotator(0.f), AverageFeetTargetsLeft, FVector(1.f)) * OwnerPawn->GetActorTransform()).GetLocation();

		AsyncTask(ENamedThreads::GameThread, [=]() {
			DrawDebugSphere(WorldContext, AverageFeetTargetsForwardWorld, 5.f, 12, FColor::FromHex("0013FF"));
			DrawDebugSphere(WorldContext, AverageFeetTargetsBackwardsWorld, 5.f, 12, FColor::FromHex("0013FF"));
			DrawDebugSphere(WorldContext, AverageFeetTargetsRightdWorld, 5.f, 12, FColor::FromHex("00C5FF"));
			DrawDebugSphere(WorldContext, AverageFeetTargetsLeftWorld, 5.f, 12, FColor::FromHex("00C5FF"));
		});
	}

	ComputeBodyRotation(AverageFeetTargetsForward, AverageFeetTargetsBackwards, AverageFeetTargetsRight, AverageFeetTargetsLeft);
	ComputeBodyLocation(AverageFeetTargetsForward, AverageFeetTargetsBackwards, AverageFeetTargetsRight, AverageFeetTargetsLeft);

	if (bDebug && bIsPlaying)
	{
		float MeshBoxSize = SkeletalMeshComponent->SkeletalMesh->GetBounds().BoxExtent.Size();
		FTransform DebugBoxTransform = FTransform(
			OwnerPawn->GetActorRotation() + CurrentBodyRelRotation
			, OwnerPawn->GetActorLocation() + CurrentBodyRelLocation
			, FVector(1.f));

		AsyncTask(ENamedThreads::GameThread, [=]() {
			DrawDebugCoordinateSystem(WorldContext, DebugBoxTransform.GetLocation(), DebugBoxTransform.Rotator(), MeshBoxSize * 1.5, false, -1.f, 0, 1.f);
		});
	}
}

/*
 * -> BODY ROTATION
 */
void FAnimNode_SPW::ComputeBodyRotation(FVector AverageFeetTargetsForward
	, FVector AverageFeetTargetsBackwards
	, FVector AverageFeetTargetsRight
	, FVector AverageFeetTargetsLeft)
{
	// Compute body rotation
	float PitchFromFeetLocations = 0.f;
	float RollFromFeetLocations = 0.f;
	float PitchFromAcceleration = 0.f;
	float RollFromAcceleration = 0.f;

	if (bBodyRotateOnFeetLocations)
	{
		// rotation based on feet targets
		PitchFromFeetLocations = UKismetMathLibrary::DegAtan(
			(AverageFeetTargetsForward.Z - AverageFeetTargetsBackwards.Z)
			/ (AverageFeetTargetsForward.X - AverageFeetTargetsBackwards.X)
		);

		RollFromFeetLocations = -UKismetMathLibrary::DegAtan(
			(AverageFeetTargetsRight.Z - AverageFeetTargetsLeft.Z)
			/ (AverageFeetTargetsRight.Y - AverageFeetTargetsLeft.Y)
		);
	}

	// save inclination multipliers

	// map range clamped to StepSlopeReductionMultiplier -> 1
	ReduceSlopeMultiplierPitch = UKismetMathLibrary::MapRangeClamped(
		// abs cos so 0 deg = 1 and +/-90 deg = 0
		abs(FGenericPlatformMath::Cos(FMath::RadiansToDegrees(PitchFromFeetLocations)))
		, 0.f, 1.f
		, (1 - StepSlopeReductionMultiplier), 1.f);

	// map range clamped to StepSlopeReductionMultiplier -> 1
	ReduceSlopeMultiplierRoll = UKismetMathLibrary::MapRangeClamped(
		// abs cos so 0 deg = 1 and +/-90 deg = 0
		abs(FGenericPlatformMath::Cos(FMath::RadiansToDegrees(RollFromFeetLocations)))
		, 0.f, 1.f
		, (1 - StepSlopeReductionMultiplier), 1.f);

	if (bBodyRotateOnAcceleration)
	{
		// rotation based on acceleration
		PitchFromAcceleration = ForwardAcceleration * BodyAccelerationRotationMultiplier * -.2f;
		RollFromAcceleration = RightAcceleration * BodyAccelerationRotationMultiplier * .2f;
	}

	// add & save
	float BodyPitch = FMath::ClampAngle(PitchFromFeetLocations + PitchFromAcceleration, -MaxBodyRotation.Pitch, MaxBodyRotation.Pitch);
	float BodyRoll = FMath::ClampAngle(RollFromFeetLocations + RollFromAcceleration, -MaxBodyRotation.Roll, MaxBodyRotation.Roll);
	FRotator TargetBodyRelRotation = FRotator(BodyPitch, 0.f, BodyRoll);

	// interp rotation
	CurrentBodyRelRotation = FMath::RInterpTo(CurrentBodyRelRotation, TargetBodyRelRotation, WorldDeltaSeconds, BodyRotationInterpSpeed);
}

/*
 * -> BODY LOCATION
 */
void FAnimNode_SPW::ComputeBodyLocation(FVector AverageFeetTargetsForward
	, FVector AverageFeetTargetsBackwards
	, FVector AverageFeetTargetsRight
	, FVector AverageFeetTargetsLeft)
{
	// get average feet locations
	TArray<FVector> FeetLocations;
	for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
	{
		FeetLocations.Add(LegsData[LegIndex].FootLocation);
	}
	FVector AverageFeetLocation = UKismetMathLibrary::GetVectorArrayAverage(FeetLocations);

	// feet locations relative to actor
	FVector AverageFeetRelLocation = UKismetMathLibrary::InverseTransformLocation(OwnerPawn->GetActorTransform(), AverageFeetLocation);

	// Z reduction due to slope
	float ReduceZForFeetLocations = FMath::Clamp(
		UKismetMathLibrary::FMax(
			// forward feet difference
			abs(AverageFeetTargetsForward.Z - AverageFeetTargetsBackwards.Z) * BodySlopeMultiplier
			// right feet difference
			, abs(AverageFeetTargetsRight.Z - AverageFeetTargetsLeft.Z) * BodySlopeMultiplier)
		, 0.f, OwnerHalfHeight);

	// compute body Z position
	float BodyZPosition =
		// init body position based on average feet location (dampened with multiplier)
		(AverageFeetRelLocation.Z + OwnerHalfHeight) * BodyBounceMultiplier
		// reduce due to being on slope
		- ReduceZForFeetLocations
		// add body custom offset
		+ BodyZOffset;

	FVector TargetBodyRelLocation = FVector(0.f, 0.f, BodyZPosition);

	// interpolate
	CurrentBodyRelLocation = FMath::VInterpTo(CurrentBodyRelLocation, TargetBodyRelLocation, WorldDeltaSeconds, BodyLocationInterpSpeed);
}

void FAnimNode_SPW::GetAverageFeetTargets(FVector* AverageFeetTargetsForward
	, FVector* AverageFeetTargetsBackwards
	, FVector* AverageFeetTargetsRight
	, FVector* AverageFeetTargetsLeft)
{
	// create foot forward / backwards / right / left location arrays
	TArray<FVector> FeetTargetsForward;
	TArray<FVector> FeetTargetsBackwards;
	TArray<FVector> FeetTargetsRight;
	TArray<FVector> FeetTargetsLeft;

	for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
	{
		// get local target transform
		FVector FTarget = UKismetMathLibrary::InverseTransformLocation(OwnerPawn->GetActorTransform(), LegsData[LegIndex].FootTarget);
		// add to front / backwards
		if (LegsData[LegIndex].bIsForward)
		{
			FeetTargetsForward.Add(FTarget);
		}
		if (LegsData[LegIndex].bIsBackwards)
		{
			FeetTargetsBackwards.Add(FTarget);
		}

		// add to right / left
		if (LegsData[LegIndex].bIsRight)
		{
			FeetTargetsRight.Add(FTarget);
		}
		if (LegsData[LegIndex].bIsLeft)
		{
			FeetTargetsLeft.Add(FTarget);
		}
	}

	*AverageFeetTargetsForward = UKismetMathLibrary::GetVectorArrayAverage(FeetTargetsForward);
	*AverageFeetTargetsBackwards = UKismetMathLibrary::GetVectorArrayAverage(FeetTargetsBackwards);
	*AverageFeetTargetsRight = UKismetMathLibrary::GetVectorArrayAverage(FeetTargetsRight);
	*AverageFeetTargetsLeft = UKismetMathLibrary::GetVectorArrayAverage(FeetTargetsLeft);
}

void FAnimNode_SPW::ResetFeetTargetsAndLocations()
{
	// trace
	SetFeetTargetLocations();

	// reset feet
	for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
	{
		FVector FootLocation = (FTransform(FRotator(0.f), LegsData[LegIndex].TipBoneOriginalRelLocation, FVector(1.f)) * OwnerPawn->GetActorTransform()).GetLocation();
		LegsData[LegIndex].FootLocation = FootLocation;
		LegsData[LegIndex].FootUnplantLocation = FootLocation;
	}

	// reset groups
	CurrentGroupIndex = 0;
	for (int GroupIndex = 0; GroupIndex < LegGroups.Num(); GroupIndex++)
	{
		GroupsData[GroupIndex].StepPercent = 0.f;
		GroupsData[GroupIndex].bIsUnplanted = false;
	}
}

/*
 * -> DEBUG INFO
 */
void FAnimNode_SPW::DebugShow()
{
	if (bDebug && bIsPlaying)
	{
		for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
		{
			AsyncTask(ENamedThreads::GameThread, [=]() {
				DrawDebugSphere(WorldContext, LegsData[LegIndex].FootLocation, 10.f, 12, FColor::White);
			});

			if (IsLegUnplanted(LegIndex))
			{
				AsyncTask(ENamedThreads::GameThread, [=]() {
					DrawDebugSphere(WorldContext, LegsData[LegIndex].FootUnplantLocation, 10.f, 12, FColor::Yellow);
				});
			}
		}
		/*
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9990, 2.f, FColor::Yellow,
				FString::Printf(TEXT("Speed: %f"), Speed));
			GEngine->AddOnScreenDebugMessage(9991, 2.f, FColor::Green,
				FString::Printf(TEXT("Forward%%: %f | Right%%: %f | YawDelta: %f"), ForwardPercent, RightPercent, YawDelta));
			GEngine->AddOnScreenDebugMessage(9992, 2.f, FColor::Blue,
				FString::Printf(TEXT("ForwardAcceleration: %f | RightAcceleration: %f"), ForwardAcceleration, RightAcceleration));
			GEngine->AddOnScreenDebugMessage(9993, 2.f, FColor::Purple,
				FString::Printf(TEXT("CurrentStepLength: %f | CurrentStepDuration: %f"), CurrentStepLength, CurrentStepDuration));
			GEngine->AddOnScreenDebugMessage(9994, 2.f, FColor::Red,
				FString::Printf(TEXT("ReduceSlopeMultiplierPitch: %f | ReduceSlopeMultiplierRoll: %f"), ReduceSlopeMultiplierPitch, ReduceSlopeMultiplierRoll));
			GEngine->AddOnScreenDebugMessage(9995, 2.f, FColor::White,
				FString::Printf(TEXT("CurrentBodyRelRotationPitch: %f | CurrentBodyRelRotationRoll: %f"), CurrentBodyRelRotation.Pitch, CurrentBodyRelRotation.Roll));
		}
		*/
	}
}

/*
 * -> EDITOR ONLY
 */
void FAnimNode_SPW::EditorDebugShow(AActor* SkeletalMeshOwner)
{
	if (bDebug)
	{
		// get editor rotation
		FRotator EditorPreviewRotation;

		switch (SkeletalMeshForwardAxis)
		{
		case ESimpleProceduralWalk_MeshForwardAxis::X:
			EditorPreviewRotation = FRotator(0.f, 0.f, 0.f);
			break;
		case ESimpleProceduralWalk_MeshForwardAxis::NX:
			EditorPreviewRotation = FRotator(0.f, 180.f, 0.f);
			break;
		case ESimpleProceduralWalk_MeshForwardAxis::Y:
			EditorPreviewRotation = FRotator(0.f, 90.f, 0.f);
			break;
		case ESimpleProceduralWalk_MeshForwardAxis::NY:
			EditorPreviewRotation = FRotator(0.f, -90.f, 0.f);
			break;
		}

		// draw coordinate system
		float MeshBoxSize = SkeletalMeshComponent->SkeletalMesh->GetBounds().BoxExtent.Size();

		AsyncTask(ENamedThreads::GameThread, [=]() {
			DrawDebugCoordinateSystem(WorldContext, FVector(0.f, 0.f, 0.f), EditorPreviewRotation, MeshBoxSize * 1.5, false, -1.f, 0, 1.f);
		});

		// loop feet
		for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
		{
			// get foot data
			FSimpleProceduralWalk_Leg Leg = Legs[LegIndex];

			// Parent Bone Location
			FVector ParentBoneLocation = SkeletalMeshComponent->GetSocketLocation(Leg.ParentBone.BoneName);

			// get offsets
			FVector ForwardOffset = EditorPreviewRotation.RotateVector(SkeletalMeshOwner->GetActorForwardVector() * Leg.Offset.X);
			FVector RightOffset = EditorPreviewRotation.RotateVector(SkeletalMeshOwner->GetActorRightVector() * Leg.Offset.Y);

			// Locations
			FVector StartLocation = ParentBoneLocation + ForwardOffset + RightOffset;
			FVector EndLocation = StartLocation - SkeletalMeshOwner->GetActorUpVector() * TraceLength;
			StartLocation += SkeletalMeshOwner->GetActorUpVector() * TraceZOffset;

			// init hit
			bool bIsHit = false;
			FHitResult Hit = FHitResult(ForceInit);

			// prepare ignore actors
			TArray<AActor*> ActorsToIgnore;
			ActorsToIgnore.Add(SkeletalMeshOwner);

			// line hit
			bIsHit = UKismetSystemLibrary::LineTraceSingle(WorldContext
				, StartLocation
				, EndLocation
				, TraceChannel
				, bTraceComplex
				, ActorsToIgnore
				, EDrawDebugTrace::None
				, Hit
				, true
			);

			FTransform DebugTransform = FTransform(SkeletalMeshOwner->GetActorRotation(), Hit.ImpactPoint, FVector(1.f));

			// line
			AsyncTask(ENamedThreads::GameThread, [=]() {
				// draw line
				DrawDebugLine(WorldContext, StartLocation, EndLocation, (bIsHit ? FColor::Green : FColor::Red));
				// hit point
				if (bIsHit)
				{
					DrawDebugSolidBox(WorldContext, FBox(FVector(-2.f, -2.f, 0.f), FVector(2.f, 2.f, 2.f)), FColor::Green, DebugTransform);
				}
			});
		}
	}
}

// ---------- \/ ix ----------
void FAnimNode_SPW::CallStepInterfaces(int32 GroupIndex, bool bIsDown)
{
	UE_LOG(LogSimpleProceduralWalk, Verbose, TEXT("Calling Step interfaces."));

	// pawn
	if (OwnerPawn->GetClass()->ImplementsInterface(USimpleProceduralWalkInterface::StaticClass()))
	{
		CallStepInterface(OwnerPawn, GroupIndex, bIsDown);
	}
	// anim instance
	if (SkeletalMeshComponent->GetAnimInstance()->GetClass()->ImplementsInterface(USimpleProceduralWalkInterface::StaticClass()))
	{
		CallStepInterface(SkeletalMeshComponent->GetAnimInstance(), GroupIndex, bIsDown);
	}
}

void FAnimNode_SPW::CallStepInterface(UObject* InterfaceOwner, int32 GroupIndex, bool bIsDown)
{
	TArray<FVector> GroupFeetLocations;

	// per foot event, loop feet in group
	for (int LegIndex : LegGroups[GroupIndex].LegIndices)
	{
		GroupFeetLocations.Add(LegsData[LegIndex].FootLocation);

		if (bIsDown)
		{
			AsyncTask(ENamedThreads::GameThread, [=]() {
				ISimpleProceduralWalkInterface::Execute_OnFootDown(InterfaceOwner, LegIndex, Legs[LegIndex].TipBone.BoneName, LegsData[LegIndex].FootLocation);
			});
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [=]() {
				ISimpleProceduralWalkInterface::Execute_OnFootUp(InterfaceOwner, LegIndex, Legs[LegIndex].TipBone.BoneName, LegsData[LegIndex].FootLocation);
			});
		}
	}

	// group event
	FVector AverageFeetLocation = UKismetMathLibrary::GetVectorArrayAverage(GroupFeetLocations);
	if (bIsDown)
	{
		AsyncTask(ENamedThreads::GameThread, [=]() {
			ISimpleProceduralWalkInterface::Execute_OnGroupDown(InterfaceOwner, GroupIndex, AverageFeetLocation);
		});
	}
	else
	{
		AsyncTask(ENamedThreads::GameThread, [=]() {
			ISimpleProceduralWalkInterface::Execute_OnGroupUp(InterfaceOwner, GroupIndex, AverageFeetLocation);
		});
	}
}

// ---------- \/ helpers ----------
void FAnimNode_SPW::SetSupportComponentData(int32 LegIndex, FVector RefLocation)
{
	// support component
	LegsData[LegIndex].SupportComp = LegsData[LegIndex].LastHit.GetComponent();
	if (IsValid(LegsData[LegIndex].SupportComp))
	{
		// store current component transform
		LegsData[LegIndex].SupportCompPreviousTransform = LegsData[LegIndex].SupportComp->GetComponentTransform();

		// store relative unplant location
		LegsData[LegIndex].RelLocationToSupportComp = UKismetMathLibrary::InverseTransformLocation(
			// component tranform
			LegsData[LegIndex].SupportComp->GetComponentTransform()
			// ref locations
			, RefLocation);
	}
}

float FAnimNode_SPW::GetReductionSlopeMultiplier()
{
	return abs(ForwardPercent) * ReduceSlopeMultiplierPitch + abs(RightPercent) * ReduceSlopeMultiplierRoll;
}

bool FAnimNode_SPW::IsLegUnplanted(int32 LegIndex)
{
	return GroupsData[LegsData[LegIndex].GroupIndex].bIsUnplanted;
}

float FAnimNode_SPW::GetLegStepPercent(int32 LegIndex)
{
	return GroupsData[LegsData[LegIndex].GroupIndex].StepPercent;
}

void FAnimNode_SPW::SetNextCurrentGroupIndex()
{
	CurrentGroupIndex += 1;
	if (CurrentGroupIndex == LegGroups.Num())
	{
		CurrentGroupIndex = 0;
	}
}
