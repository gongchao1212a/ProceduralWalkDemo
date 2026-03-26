// Copyright Roberto Ostinelli, 2021. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SPW.h"
#include "SPW_CCDIKSolver.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_SPW.generated.h"


USTRUCT()
struct SIMPLEPROCEDURALWALK_API FAnimNode_SPW : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()
	// 核心动画节点：负责足点采样、步态推进、身体姿态计算和 CCDIK 解算。

public:
	// ---------- \/ Debug ----------
	// 调试开关：开启后会绘制足点、射线等可视化信息。
	/** Should draw the debug elements? */
	UPROPERTY(EditAnywhere, Category = "Debug")
		bool bDebug = false;

	// ---------- \/ Skeletal Control ----------
	// 骨骼基础配置：身体骨和腿骨链定义。
	/**
	 * The forward axis of the Skeletal Mesh.
	 * With Debug enabled, ensure that the RED axis goes towards the front of your mesh.
	 */
	UPROPERTY(EditAnywhere, Category = "Skeletal Control")
		ESimpleProceduralWalk_MeshForwardAxis SkeletalMeshForwardAxis;

	/**
	 * The bone that defines the center of the body.
	 * This bone should ideally be placed at the center of the body, otherwise unoptimal animation may happen.
	 */
	UPROPERTY(EditAnywhere, Category = "Skeletal Control")
		FBoneReference BodyBone;

	/** Defines the legs to animate. */
	UPROPERTY(EditAnywhere, Category = "Skeletal Control")
		TArray<FSimpleProceduralWalk_Leg> Legs;

	// ---------- \/ Walk Cycle ----------
	// 步态参数：决定抬脚时机、步长、步高、步间节奏。
	/** Defines the leg groups (the legs in a group will unplant at the same time). */
	UPROPERTY(EditAnywhere, Category = "Walk Cycle")
		TArray<FSimpleProceduralWalk_LegGroup> LegGroups;

	/** How hight should the step be above the ground. */
	UPROPERTY(EditAnywhere, Category = "Walk Cycle", meta = (ClampMin = "0.0"))
		float StepHeight = 0.f;

	/** How far should the step move forward (and backwards) */
	UPROPERTY(EditAnywhere, Category = "Walk Cycle", meta = (ClampMin = "0.0"))
		float StepDistanceForward = 0.f;

	/** How far should the step move sideways */
	UPROPERTY(EditAnywhere, Category = "Walk Cycle", meta = (ClampMin = "0.0"))
		float StepDistanceRight = 0.f;

	/**
	 * Defines at which percentage of a step the next group of legs will unplant.
	 * With a value of 1, a group will wait for the previous group to finish the step before unplanting.
	 * Values between 0-1 will make a group unplant while the previous group is still unplanted.
	 * With a value of 0, all groups will unplant at the same time.
	 */
	UPROPERTY(EditAnywhere, Category = "Walk Cycle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float StepSequencePercent = 0.f;

	/**
	 * How much should the step distance be reduced based on slope inclination:
	 * 0: No reduction.
	 * 1: With a slope of 90 degrees the step is reduced to 0.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Walk Cycle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float StepSlopeReductionMultiplier = 0.f;

	/** The minimum step duration (steps should never take less than this amount of time). */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Walk Cycle", meta = (ClampMin = "0.0"))
		float MinStepDuration = 0.f;

	/** How far should the foot desired position be from the tip bone before a step is taken. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Walk Cycle", meta = (ClampMin = "0.0"))
		float MinDistanceToUnplant = 0.f;

	/** Do not adjust feet targets if the step is over this percentage. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Walk Cycle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float FixFeetTargetsAfterPercent = 0.f;

	/** The foot rotation interpolation speed. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Walk Cycle", meta = (ClampMin = "0.0"))
		float FeetTipBonesRotationInterpSpeed = 0.f;

	/** The curve that defines the foot acceleration evolution during a step. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Walk Cycle")
		UCurveFloat* SpeedCurve;

	/** The curve that defines the foot height evolution during a step. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Walk Cycle")
		UCurveFloat* HeightCurve;

	// ---------- \/ Body Location ----------
	// 身体位移：根据脚底目标位置估算身体上下浮动与坡度补偿。
	/** How much should the body bounce up and down while walking (0 disables it). */
	UPROPERTY(EditAnywhere, Category = "Body Location", meta = (ClampMin = "0.0"))
		float BodyBounceMultiplier = 0.f;

	/** How much should the body be lowered to the ground while on a slope. */
	UPROPERTY(EditAnywhere, Category = "Body Location", meta = (ClampMin = "0.0"))
		float BodySlopeMultiplier = 0.f;

	/** How fast should the body location movement be interpolated. */
	UPROPERTY(EditAnywhere, Category = "Body Location", meta = (ClampMin = "0.0"))
		float BodyLocationInterpSpeed = 0.f;

	/** Additional body offset along the Z axis. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Body Location")
		float BodyZOffset = 0.f;

	// ---------- \/ Body Rotation ----------
	// 身体旋转：由加速度和落脚高度差共同驱动 Pitch/Roll。
	/** Should the body rotate based on change of direction? */
	UPROPERTY(EditAnywhere, Category = "Body Rotation")
		bool bBodyRotateOnAcceleration = false;

	/** Should the body rotate based on feet locations? */
	UPROPERTY(EditAnywhere, Category = "Body Rotation")
		bool bBodyRotateOnFeetLocations = false;

	/** How fast should the body rotation movement be interpolated. */
	UPROPERTY(EditAnywhere, Category = "Body Rotation", meta = (ClampMin = "0.0", EditCondition = "bBodyRotateOnAcceleration || bBodyRotateOnFeetLocations"))
		float BodyRotationInterpSpeed = false;

	/** How much should the acceleration influence the body rotation. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Body Rotation", meta = (ClampMin = "0.0", EditCondition = "bBodyRotateOnAcceleration"))
		float BodyAccelerationRotationMultiplier = 0.f;

	/** How much should the feet locations influence the body rotation. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Body Rotation", meta = (ClampMin = "0.0", EditCondition = "bBodyRotateOnFeetLocations"))
		float BodyFeetLocationsRotationMultiplier = 0.f;

	/** Maximum body rotation, per axis: Roll (X), Pitch (Y), and Yaw (Z, ignored). */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Body Rotation", meta = (ClampMin = "0.0", EditCondition = "bBodyRotateOnAcceleration || bBodyRotateOnFeetLocations"))
		FRotator MaxBodyRotation = FRotator(0.f);

	// ---------- \/ Solver ----------
	// 足点求解策略：BASIC 仅垂直采样，ADVANCED 增加半径内候选点搜索。
	/** The ADVANCED solver type is more accurate to some world scenarios, but it's more expensive. */
	UPROPERTY(EditAnywhere, Category = "Solver")
		ESimpleProceduralWalk_SolverType SolverType;

	/** Specifies the radius within which to check for existing places where to plant feet. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Solver", meta = (ClampMin = "1.0", ClampMax = "3.0", EditCondition = "SolverType == ESimpleProceduralWalk_SolverType::ADVANCED"))
		float RadiusCheckMultiplier = 0.f;

	/**
	 * Specifies when the basic vertical location where to plant the foot should be abandoned and a location within a radius should be searched for instead.
	 * This is directly related to how much the leg can "extend" its Z axis when going from idle to walking.
	 */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Solver", meta = (ClampMin = "1.0", ClampMax = "3.0", EditCondition = "SolverType == ESimpleProceduralWalk_SolverType::ADVANCED"))
		float DistanceCheckMultiplier = 0.f;

	// ---------- \/ IK Solver ----------
	// IK 参数：控制 CCDIK 的收敛精度和性能开销。
	/** Start computations from tail. */
	UPROPERTY(EditAnywhere, Category = "IK Solver", meta = (ClampMin = "0.0"))
		bool bStartFromTail = false;

	/** Tolerance for final tip bone location delta. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "IK Solver", meta = (ClampMin = "0.0"))
		float Precision = 0.f;

	/** Maximum number of iterations allowed, to control performance. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "IK Solver", meta = (ClampMin = "0"))
		int32 MaxIterations = 0;

	// ---------- \/ Trace ----------
	// 地面检测：通过向下 Trace 获取足点目标。
	/**
	 * The trace channel.
	 * It is recommended to have a channel dedicated to feet placement so that, for instance, feet are not placed on top of grass or small foliage.
	 */
	UPROPERTY(EditAnywhere, Category = "Trace")
		TEnumAsByte<ETraceTypeQuery> TraceChannel;

	/** The length of the downwards trace. */
	UPROPERTY(EditAnywhere, Category = "Trace", meta = (ClampMin = "0.0"))
		float TraceLength = 0.f;

	/** Should the trace be complex? */
	UPROPERTY(EditAnywhere, Category = "Trace")
		bool bTraceComplex = false;

	/** Trace offset (from the foot Parent Bone). */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Trace")
		float TraceZOffset = 0.f;

public:
	// Constructor
	// 构造函数中会设置默认参数并加载默认曲线资源。
	FAnimNode_SPW();

	// FAnimNode_Base interface
	// 动画节点基础生命周期函数。
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;

	// FAnimNode_SkeletalControlBase interface
	// 骨骼控制生命周期：初始化骨骼引用、合法性检查、逐帧解算。
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;

	// from graph node: resize rotation limit array based on set up
	// 编辑器辅助：当骨骼链变化时，同步旋转限制数组长度。
	void CCDIK_ResizeRotationLimitPerJoints(int32 LegIndex, int32 NewSize);

private:
	// internals
	// 运行时状态标记。
	bool bHasErrors = false;
	bool bIsPlaying = false;
	bool bIsEditorAnimPreview = false;
	int32 SkippedFrames = 0;
	bool bIsInitialized = false;
	float WorldDeltaSeconds = 0.f;

	// References
	// 外部对象引用（世界与骨骼组件）。
	UWorld* WorldContext;
	USkeletalMeshComponent* SkeletalMeshComponent;

	// pawn
	// 与运动学相关的缓存值（速度、方向占比、加速度、步长等）。
	bool bIsFalling = false;
	float Speed = 0.f;
	float ForwardPercent = 0.f;
	float RightPercent = 0.f;
	float PreviousSpeed = 0.f;
	float PreviousForwardPercent = 0.f;
	float PreviousRightPercent = 0.f;
	float ForwardAcceleration = 0.f;
	float RightAcceleration = 0.f;
	FRotator PreviousRotation = FRotator(0.f);
	float YawDelta = 0.f;
	float CurrentStepLength = 0.f;
	float CurrentStepDuration = 0.f;

	// pawn data
	// 角色及体型基础信息。
	APawn* OwnerPawn;
	float OwnerHalfHeight;

	// legs
	// 每条腿的运行时数据（目标点、当前点、支撑组件等）。
	TArray<FSimpleProceduralWalk_LegData> LegsData;

	// groups
	// 腿组状态：当前组、组内步进百分比和是否抬脚。
	int32 CurrentGroupIndex = 0;
	TArray<FSimpleProceduralWalk_LegGroupData> GroupsData;

	// body
	// 身体相对旋转/位移与坡度系数。
	FRotator CurrentBodyRelRotation = FRotator(0.f);
	FVector CurrentBodyRelLocation = FVector(0.f);
	float ReduceSlopeMultiplierPitch = 1.f;
	float ReduceSlopeMultiplierRoll = 1.f;

	// IK
	// IK 运行时缓存：目标、父骨、末端骨、关节旋转限制。
	TArray<FBoneSocketTarget> EffectorTargets;
	TArray<FBoneReference> ParentBones;
	TArray<FBoneReference> TipBones;
	TArray<FSimpleProceduralWalk_RotationLimitsPerJoint> FeetRotationLimitsPerJoints;

	// ---------- \/ computations ----------
	// 计算主流程。
	void Initialize_Computations();
	void Evaluate_Computations();
	void UpdatePawnVariables();
	void SetSupportCompDeltas();
	// walk
	// 行走相关：足点追踪、抬脚、落脚、重置。
	void SetFeetTargetLocations();
	void SetFootTargetLocation(int32 LegIndex);
	void SetCurrentGroupUnplanted();
	void ComputeFeet();
	void SetGroupsPlanted();
	void ResetFeetTargetsAndLocations();
	// body
	// 身体姿态计算。
	void ComputeBodyTransform();
	void ComputeBodyRotation(FVector AverageFeetTargetsForward
		, FVector AverageFeetTargetsBackwards
		, FVector AverageFeetTargetsRight
		, FVector AverageFeetTargetsLeft);
	void ComputeBodyLocation(FVector AverageFeetTargetsForward
		, FVector AverageFeetTargetsBackwards
		, FVector AverageFeetTargetsRight
		, FVector AverageFeetTargetsLeft);
	void GetAverageFeetTargets(FVector* AverageFeetTargetsForward
		, FVector* AverageFeetTargetsBackwards
		, FVector* AverageFeetTargetsRight
		, FVector* AverageFeetTargetsLeft);

	// ix
	// 事件接口回调（抬脚/落脚/着地）。
	void CallStepInterfaces(int32 GroupIndex, bool bIsDown);
	void CallStepInterface(UObject* InterfaceOwner, int32 GroupIndex, bool bIsDown);
	void CallLandedInterfaces();
	void CallLandedInterface(UObject* InterfaceOwner);

	// helpers
	// 工具函数。
	void SetSupportComponentData(int32 LegIndex, FVector RefLocation);
	float GetReductionSlopeMultiplier();
	bool IsLegUnplanted(int32 LegIndex);
	float GetLegStepPercent(int32 LegIndex);
	void SetNextCurrentGroupIndex();

	// debug
	// 调试绘制（运行时与编辑器预览）。
	void DebugShow();
	void EditorDebugShow(AActor* SkeletalMeshOwner);

	// BODY
	// 将身体位姿结果写入骨骼。
	void Evaluate_BodySolver(FComponentSpacePoseContext& Output);

	// solver
	// ADVANCED 模式下的半径检查值缓存。
	float RadiusCheck;

	// CCDIK
	// CCDIK 初始化、执行和迭代求解。
	void Initialize_CCDIK();
	void Evaluate_CCDIKSolver(FComponentSpacePoseContext& Output);
	FTransform CCDIK_GetTargetTransform(const FTransform& InComponentTransform
		, FCSPose<FCompactPose>& MeshBases
		, FBoneSocketTarget& InTarget
		, const FVector& InOffset);

	bool SolveCCDIK(TArray<FSPW_CCDIKChainLink>& InOutChain
		, const FVector& TargetPosition
		, bool bEnableRotationLimit
		, const TArray<float>& RotationLimitPerJoints);
};
