// Copyright Roberto Ostinelli, 2021. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SimpleProceduralWalkInterface.generated.h"


// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class USimpleProceduralWalkInterface : public UInterface
{
	GENERATED_BODY()
};

class SIMPLEPROCEDURALWALK_API ISimpleProceduralWalkInterface
{
	GENERATED_BODY()

public:
	/** Called when a foot steps on the ground */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Simple Procedural Walk")
		void OnFootDown(int32 LegIndex, FName TipBone, FVector FootLocation);

	/** Called when a feet group ends a step */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Simple Procedural Walk")
		void OnGroupDown(int32 GroupIndex, FVector AverageFeetLocation);

	/** Called when a foot leaves the ground */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Simple Procedural Walk")
		void OnFootUp(int32 LegIndex, FName TipBone, FVector FootLocation);

	/** Called when a feet group starts a step */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Simple Procedural Walk")
		void OnGroupUp(int32 GroupIndex, FVector AverageFeetLocation);

	/** Called when the pawn lands */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Simple Procedural Walk")
		void OnPawnLanded(FVector Location);
};
