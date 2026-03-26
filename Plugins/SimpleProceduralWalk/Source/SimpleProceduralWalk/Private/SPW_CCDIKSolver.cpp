// Copyright Epic Games, Inc. and Roberto Ostinelli, 2021. All Rights Reserved.

#include "SPW_CCDIKSolver.h"
#include "AnimNode_SPW.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimInstanceProxy.h"


void FAnimNode_SPW::Initialize_CCDIK()
{
	// resize
	FeetRotationLimitsPerJoints.SetNum(Legs.Num());

	for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
	{
		FeetRotationLimitsPerJoints[LegIndex].RotationLimits = Legs[LegIndex].RotationLimitPerJoints;

		// (the fact that this bone has root is checked during saving)
		FeetRotationLimitsPerJoints[LegIndex].RotationLimits.Insert(0.f, 0);
	}
}

void FAnimNode_SPW::Evaluate_CCDIKSolver(FComponentSpacePoseContext& Output)
{
	if (bIsInitialized)
	{
		// container
		const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();

		// partial transforms
		TArray<FBoneTransform> TempTransforms;

		for (int LegIndex = 0; LegIndex < Legs.Num(); LegIndex++)
		{
			TempTransforms.Reset();

			// do not perform IK if it's disabled
			if (!LegsData[LegIndex].bEnableIK)
			{
				continue;
			}

			// Update EffectorLocation if it is based off a bone position
			FVector EffectorLocation(LegsData[LegIndex].FootLocation);
			
			FTransform CSEffectorTransform = CCDIK_GetTargetTransform(Output.AnimInstanceProxy->GetComponentTransform()
				, Output.Pose
				, EffectorTargets[LegIndex]
				, EffectorLocation);
			FVector const CSEffectorLocation = CSEffectorTransform.GetLocation();

			// Gather all bone indices between root and tip.
			TArray<FCompactPoseBoneIndex> BoneIndices;

			{
				const FCompactPoseBoneIndex RootIndex = ParentBones[LegIndex].GetCompactPoseIndex(BoneContainer);
				FCompactPoseBoneIndex BoneIndex = TipBones[LegIndex].GetCompactPoseIndex(BoneContainer);
				do
				{
					BoneIndices.Insert(BoneIndex, 0);
					BoneIndex = Output.Pose.GetPose().GetParentBoneIndex(BoneIndex);
				} while (BoneIndex != RootIndex);
				BoneIndices.Insert(BoneIndex, 0);
			}

			// Gather transforms
			int32 const NumTransforms = BoneIndices.Num();
			TempTransforms.AddUninitialized(NumTransforms);

			// Gather chain links. These are non zero length bones.
			TArray<FSPW_CCDIKChainLink> Chain;
			Chain.Reserve(NumTransforms);
			// Start with Root Bone
			{
				const FCompactPoseBoneIndex& RootBoneIndex = BoneIndices[0];
				const FTransform& LocalTransform = Output.Pose.GetLocalSpaceTransform(RootBoneIndex);
				const FTransform& BoneCSTransform = Output.Pose.GetComponentSpaceTransform(RootBoneIndex);

				TempTransforms[0] = FBoneTransform(RootBoneIndex, BoneCSTransform);
				Chain.Add(FSPW_CCDIKChainLink(BoneCSTransform, LocalTransform, 0));
			}

			// Go through remaining transforms
			for (int32 TransformIndex = 1; TransformIndex < NumTransforms; TransformIndex++)
			{
				const FCompactPoseBoneIndex& BoneIndex = BoneIndices[TransformIndex];

				const FTransform& LocalTransform = Output.Pose.GetLocalSpaceTransform(BoneIndex);
				const FTransform& BoneCSTransform = Output.Pose.GetComponentSpaceTransform(BoneIndex);
				FVector const BoneCSPosition = BoneCSTransform.GetLocation();

				TempTransforms[TransformIndex] = FBoneTransform(BoneIndex, BoneCSTransform);

				// Calculate the combined length of this segment of skeleton
				float const BoneLength = FVector::Dist(BoneCSPosition, TempTransforms[TransformIndex - 1].Transform.GetLocation());

				if (!FMath::IsNearlyZero(BoneLength))
				{
					Chain.Add(FSPW_CCDIKChainLink(BoneCSTransform, LocalTransform, TransformIndex));
				}
				else
				{
					// Mark this transform as a zero length child of the last link.
					// It will inherit position and delta rotation from parent link.
					FSPW_CCDIKChainLink & ParentLink = Chain[Chain.Num() - 1];
					ParentLink.ChildZeroLengthTransformIndices.Add(TransformIndex);
				}
			}

			// solve
			bool bBoneLocationUpdated = SolveCCDIK(Chain
				, CSEffectorLocation
				, Legs[LegIndex].bEnableRotationLimits
				, FeetRotationLimitsPerJoints[LegIndex].RotationLimits);

			// If we moved some bones, update bone transforms.
			if (bBoneLocationUpdated)
			{
				int32 NumChainLinks = Chain.Num();

				// First step: update bone transform positions from chain links.
				for (int32 LinkIndex = 0; LinkIndex < NumChainLinks; LinkIndex++)
				{
					FSPW_CCDIKChainLink const & ChainLink = Chain[LinkIndex];
					TempTransforms[ChainLink.TransformIndex].Transform = ChainLink.Transform;

					// If there are any zero length children, update position of those
					int32 const NumChildren = ChainLink.ChildZeroLengthTransformIndices.Num();
					for (int32 ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++)
					{
						TempTransforms[ChainLink.ChildZeroLengthTransformIndices[ChildIndex]].Transform = ChainLink.Transform;
					}
				}
			}

			// rotate tip bone
			FCompactPoseBoneIndex CompactPoseBoneToModify = Legs[LegIndex].TipBone.GetCompactPoseIndex(BoneContainer);
			FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();
			int32 const TipBoneTransformIndex = TempTransforms.Num() - 1;

			// convert to Bone Space.
			FAnimationRuntime::ConvertCSTransformToBoneSpace(ComponentTransform, Output.Pose, TempTransforms[TipBoneTransformIndex].Transform, CompactPoseBoneToModify, BCS_ComponentSpace);

			const FQuat BoneQuat(LegsData[LegIndex].FootTargetRotation);
			TempTransforms[TipBoneTransformIndex].Transform.SetRotation(BoneQuat * TempTransforms[TipBoneTransformIndex].Transform.GetRotation());

			// convert back to Component Space.
			FAnimationRuntime::ConvertBoneSpaceTransformToCS(ComponentTransform, Output.Pose, TempTransforms[TipBoneTransformIndex].Transform, CompactPoseBoneToModify, BCS_ComponentSpace);

			// merge before looping
			Output.Pose.LocalBlendCSBoneTransforms(TempTransforms, 1.f);
		}
	}
}

FTransform FAnimNode_SPW::CCDIK_GetTargetTransform(const FTransform& InComponentTransform, FCSPose<FCompactPose>& MeshBases, FBoneSocketTarget& InTarget, const FVector& InOffset)
{
	FTransform OutTransform;

	// parent bone space still goes through this way
	// if your target is socket, it will try find parents of joint that socket belongs to
	OutTransform.SetLocation(InOffset);
	FAnimationRuntime::ConvertBoneSpaceTransformToCS(InComponentTransform, MeshBases, OutTransform, InTarget.GetCompactPoseBoneIndex(), EBoneControlSpace::BCS_WorldSpace);

	return OutTransform;
}

bool FAnimNode_SPW::SolveCCDIK(TArray<FSPW_CCDIKChainLink>& InOutChain, const FVector& TargetPosition, bool bEnableRotationLimit, const TArray<float>& RotationLimitPerJoints)
{
	struct Local
	{
		static bool UpdateChainLink(TArray<FSPW_CCDIKChainLink>& Chain, int32 LinkIndex, const FVector& TargetPos, bool bInEnableRotationLimit, const TArray<float>& InRotationLimitPerJoints)
		{
			int32 const TipBoneLinkIndex = Chain.Num() - 1;

			ensure(Chain.IsValidIndex(TipBoneLinkIndex));
			FSPW_CCDIKChainLink& CurrentLink = Chain[LinkIndex];

			// update new tip pos
			FVector TipPos = Chain[TipBoneLinkIndex].Transform.GetLocation();

			FTransform& CurrentLinkTransform = CurrentLink.Transform;
			FVector ToEnd = TipPos - CurrentLinkTransform.GetLocation();
			FVector ToTarget = TargetPos - CurrentLinkTransform.GetLocation();

			ToEnd.Normalize();
			ToTarget.Normalize();

			float RotationLimitPerJointInRadian = FMath::DegreesToRadians(InRotationLimitPerJoints[LinkIndex]);
			float Angle = FMath::ClampAngle(FMath::Acos(FVector::DotProduct(ToEnd, ToTarget)), -RotationLimitPerJointInRadian, RotationLimitPerJointInRadian);
			bool bCanRotate = (FMath::Abs(Angle) > KINDA_SMALL_NUMBER) && (!bInEnableRotationLimit || RotationLimitPerJointInRadian > CurrentLink.CurrentAngleDelta);
			if (bCanRotate)
			{
				// check rotation limit first, if fails, just abort
				if (bInEnableRotationLimit)
				{
					if (RotationLimitPerJointInRadian < CurrentLink.CurrentAngleDelta + Angle)
					{
						Angle = RotationLimitPerJointInRadian - CurrentLink.CurrentAngleDelta;
						if (Angle <= KINDA_SMALL_NUMBER)
						{
							return false;
						}
					}

					CurrentLink.CurrentAngleDelta += Angle;
				}

				// continue with rotating toward to target
				FVector RotationAxis = FVector::CrossProduct(ToEnd, ToTarget);
				if (RotationAxis.SizeSquared() > 0.f)
				{
					RotationAxis.Normalize();
					// Delta Rotation is the rotation to target
					FQuat DeltaRotation(RotationAxis, Angle);

					FQuat NewRotation = DeltaRotation * CurrentLinkTransform.GetRotation();
					NewRotation.Normalize();
					CurrentLinkTransform.SetRotation(NewRotation);

					// if I have parent, make sure to refresh local transform since my current transform has changed
					if (LinkIndex > 0)
					{
						FSPW_CCDIKChainLink const & Parent = Chain[LinkIndex - 1];
						CurrentLink.LocalTransform = CurrentLinkTransform.GetRelativeTransform(Parent.Transform);
						CurrentLink.LocalTransform.NormalizeRotation();
					}

					// now update all my children to have proper transform
					FTransform CurrentParentTransform = CurrentLinkTransform;

					// now update all chain
					for (int32 ChildLinkIndex = LinkIndex + 1; ChildLinkIndex <= TipBoneLinkIndex; ++ChildLinkIndex)
					{
						FSPW_CCDIKChainLink& ChildIterLink = Chain[ChildLinkIndex];
						const FTransform LocalTransform = ChildIterLink.LocalTransform;
						ChildIterLink.Transform = LocalTransform * CurrentParentTransform;
						ChildIterLink.Transform.NormalizeRotation();
						CurrentParentTransform = ChildIterLink.Transform;
					}

					return true;
				}
			}

			return false;
		}
	};

	bool bBoneLocationUpdated = false;
	int32 const NumChainLinks = InOutChain.Num();

	// iterate
	{
		int32 const TipBoneLinkIndex = NumChainLinks - 1;

		// @todo optimize locally if no update, stop?
		bool bLocalUpdated = false;
		// check how far
		const FVector TargetPos = TargetPosition;
		FVector TipPos = InOutChain[TipBoneLinkIndex].Transform.GetLocation();
		float Distance = FVector::Dist(TargetPos, TipPos);
		int32 IterationCount = 0;
		while ((Distance > Precision) && (IterationCount++ < MaxIterations))
		{
			// iterate from tip to root
			if (bStartFromTail)
			{
				for (int32 LinkIndex = TipBoneLinkIndex - 1; LinkIndex > 0; --LinkIndex)
				{
					bLocalUpdated |= Local::UpdateChainLink(InOutChain, LinkIndex, TargetPos, bEnableRotationLimit, RotationLimitPerJoints);
				}
			}
			else
			{
				for (int32 LinkIndex = 1; LinkIndex < TipBoneLinkIndex; ++LinkIndex)
				{
					bLocalUpdated |= Local::UpdateChainLink(InOutChain, LinkIndex, TargetPos, bEnableRotationLimit, RotationLimitPerJoints);
				}
			}

			Distance = FVector::Dist(InOutChain[TipBoneLinkIndex].Transform.GetLocation(), TargetPosition);

			bBoneLocationUpdated |= bLocalUpdated;

			// no more update in this iteration
			if (!bLocalUpdated)
			{
				break;
			}
		}
	}

	return bBoneLocationUpdated;
}