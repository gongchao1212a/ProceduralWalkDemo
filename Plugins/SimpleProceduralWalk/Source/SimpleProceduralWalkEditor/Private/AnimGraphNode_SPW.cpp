// Copyright Roberto Ostinelli, 2021. All Rights Reserved.

#include "AnimGraphNode_SPW.h"
#include "SimpleProceduralWalkEditor.h"
#include "Kismet2/CompilerResultsLog.h"

#define LOCTEXT_NAMESPACE "A3Nodes"


FText UAnimGraphNode_SPW::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("SPW_Title", "Simple Procedural Walk");
}

FText UAnimGraphNode_SPW::GetTooltipText() const
{
	return LOCTEXT("SPW_Tooltip", "Solve feet positions with Simple Procedural Walk Inverse Kinematics.");
}

void UAnimGraphNode_SPW::ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog)
{
	// check feet data existence
	if (Node.Legs.Num() == 0)
	{
		MessageLog.Warning(TEXT("@@ No legs have been entered."), this);
	}
	else
	{
		// check feet bones
		for (int LegIndex = 0; LegIndex < Node.Legs.Num(); LegIndex++)
		{
			FSimpleProceduralWalk_Leg Leg = Node.Legs[LegIndex];

			if (Leg.ParentBone.BoneName == NAME_None || Leg.TipBone.BoneName == NAME_None)
			{
				MessageLog.Error(TEXT("@@ Invalid bone name(s) specified in leg with index @@."), this, *FString::FromInt(LegIndex));
			}
			else
			{
				// check parent is not root
				if (ForSkeleton->GetReferenceSkeleton().GetParentIndex(ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Leg.ParentBone.BoneName)) == INDEX_NONE)
				{
					MessageLog.Error(TEXT("@@ Parent bone of leg with index @@ cannot be set to root bone @@."), this
						, *FString::FromInt(LegIndex)
						, *Leg.ParentBone.BoneName.ToString());
				}

				// check tip is descendant of parent
				if (!ForSkeleton->GetReferenceSkeleton().BoneIsChildOf(
					ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Leg.TipBone.BoneName),
					ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Leg.ParentBone.BoneName)))
				{
					MessageLog.Error(TEXT("@@ Bone @@ is not child of @@."), this
						, *Leg.TipBone.BoneName.ToString()
						, *Leg.ParentBone.BoneName.ToString());
				}
			}
		}
	}

	// check feet groups
	if (Node.LegGroups.Num() == 0)
	{
		MessageLog.Warning(TEXT("@@ No groups have been entered."), this);
	}
	else
	{
		TArray<bool> FeetFoundInGroup;
		FeetFoundInGroup.SetNum(Node.Legs.Num());

		for (int GroupIndex = 0; GroupIndex < Node.LegGroups.Num(); GroupIndex++)
		{
			// check if feet group contains feet IDs
			if (Node.LegGroups[GroupIndex].LegIndices.Num() == 0)
			{
				MessageLog.Error(TEXT("@@ Group with index @@ exists but it contains no leg indices."), this, *FString::FromInt(GroupIndex));
			}
			else
			{
				// check if feet IDs have valid indices
				for (int LegIndex : Node.LegGroups[GroupIndex].LegIndices)
				{
					if (LegIndex >= Node.Legs.Num())
					{
						MessageLog.Error(TEXT("@@ Group with index @@ contains an invalid foot index: @@."), this, *FString::FromInt(GroupIndex), *FString::FromInt(LegIndex));
					}
					else
					{
						// foot has been found
						FeetFoundInGroup[LegIndex] = true;
					}
				}
			}
		}

		for (int LegIndex = 0; LegIndex < FeetFoundInGroup.Num(); LegIndex++)
		{
			if (!FeetFoundInGroup[LegIndex])
			{
				MessageLog.Warning(TEXT("@@ Leg with index @@ was not found in any group."), this, *FString::FromInt(LegIndex));
			}
		}
	}

	// check body
	if (ForSkeleton->GetReferenceSkeleton().FindBoneIndex(Node.BodyBone.BoneName) == INDEX_NONE &&
		(Node.BodyBounceMultiplier > 0 || Node.BodySlopeMultiplier > 0 || Node.bBodyRotateOnAcceleration || Node.bBodyRotateOnFeetLocations))
	{
		MessageLog.Warning(TEXT("@@ You've set the body to be animated but an invalid Body Bone is specified."), this);
	}

	Super::ValidateAnimNodeDuringCompilation(ForSkeleton, MessageLog);
}

void UAnimGraphNode_SPW::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FName PropertyName = (PropertyChangedEvent.Property != NULL) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(FBoneReference, BoneName))
	{
		USkeleton* Skeleton = GetAnimBlueprint()->TargetSkeleton;

		for (int LegIndex = 0; LegIndex < Node.Legs.Num(); LegIndex++)
		{
			if (Node.Legs[LegIndex].ParentBone.BoneName != NAME_None && Node.Legs[LegIndex].TipBone.BoneName != NAME_None)
			{
				const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
				const int32 RootBoneIndex = RefSkeleton.FindBoneIndex(Node.Legs[LegIndex].ParentBone.BoneName);
				const int32 TipBoneIndex = RefSkeleton.FindBoneIndex(Node.Legs[LegIndex].TipBone.BoneName);

				if (TipBoneIndex != INDEX_NONE && RootBoneIndex != INDEX_NONE)
				{
					const int32 Depth = RefSkeleton.GetDepthBetweenBones(TipBoneIndex, RootBoneIndex);
					if (Depth >= 0)
					{
						Node.CCDIK_ResizeRotationLimitPerJoints(LegIndex, Depth + 1);
					}
					else
					{
						Node.CCDIK_ResizeRotationLimitPerJoints(LegIndex, 0);
					}
				}
				else
				{
					Node.CCDIK_ResizeRotationLimitPerJoints(LegIndex, 0);
				}
			}
			else
			{
				Node.CCDIK_ResizeRotationLimitPerJoints(LegIndex, 0);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
