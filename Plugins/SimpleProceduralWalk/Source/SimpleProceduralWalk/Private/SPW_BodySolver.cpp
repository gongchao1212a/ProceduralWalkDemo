// Copyright Epic Games, Inc. and Roberto Ostinelli, 2021. All Rights Reserved.

#include "AnimNode_SPW.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimInstanceProxy.h"
#include "FABRIK.h"


void FAnimNode_SPW::Evaluate_BodySolver(FComponentSpacePoseContext& Output)
{
	if (bIsInitialized && BodyBone.BoneIndex != INDEX_NONE && !bIsFalling)
	{
		const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();

		FCompactPoseBoneIndex CompactPoseBoneToModify = BodyBone.GetCompactPoseIndex(BoneContainer);
		FTransform NewBoneTM = Output.Pose.GetComponentSpaceTransform(CompactPoseBoneToModify);
		FTransform ComponentTransform = Output.AnimInstanceProxy->GetComponentTransform();

		// \/ location
		NewBoneTM.AddToTranslation(CurrentBodyRelLocation);

		// \/ rotation
		FRotator BoneRotation;

		// switch on skeletal axis
		switch (SkeletalMeshForwardAxis)
		{
		case ESimpleProceduralWalk_MeshForwardAxis::X:
			BoneRotation = CurrentBodyRelRotation;
			break;
		case ESimpleProceduralWalk_MeshForwardAxis::NX:
			BoneRotation = FRotator(-CurrentBodyRelRotation.Pitch, 0.f, -CurrentBodyRelRotation.Roll);
			break;
		case ESimpleProceduralWalk_MeshForwardAxis::Y:
			BoneRotation = FRotator(CurrentBodyRelRotation.Roll, 0.f, -CurrentBodyRelRotation.Pitch);
			break;
		case ESimpleProceduralWalk_MeshForwardAxis::NY:
			BoneRotation = FRotator(-CurrentBodyRelRotation.Roll, 0.f, CurrentBodyRelRotation.Pitch);
			break;
		}

		const FQuat BoneQuat(BoneRotation);
		NewBoneTM.SetRotation(BoneQuat * NewBoneTM.GetRotation());

		// merge
		TArray<FBoneTransform> TempTransforms;
		TempTransforms.Add(FBoneTransform(BodyBone.GetCompactPoseIndex(BoneContainer), NewBoneTM));
		Output.Pose.LocalBlendCSBoneTransforms(TempTransforms, 1.f);
	}
}
