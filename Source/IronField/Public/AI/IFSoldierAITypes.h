#pragma once

#include "CoreMinimal.h"
#include "IFSoldierAITypes.generated.h"

class AActor;

/** High-level behaviour selected by AIFSoldierAIController; drives BT branch selection. */
UENUM(BlueprintType)
enum class EIFSoldierAction : uint8
{
	None,
	Idle,
	MoveTo,
	FollowActor,
	AttackTarget,
	Defend,
	Advance,
	HoldPosition
};

/**
 * Explicit orders. Holding a position is not an order: IssueHoldPosition re-anchors
 * the soldier and lets normal Priority 2 self-defence take over at the new post.
 */
UENUM(BlueprintType)
enum class EIFSoldierOrderType : uint8
{
	None,
	MoveToLocation,
	AttackTarget,
	FollowActor
};

USTRUCT(BlueprintType)
struct FIFSoldierOrder
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Order")
	EIFSoldierOrderType Type = EIFSoldierOrderType::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Order")
	TWeakObjectPtr<AActor> TargetActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Order")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Order")
	float Radius = 75.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Order")
	float FacingYaw = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Order")
	bool bHasFacingYaw = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Soldier|Order")
	float IssuedTime = -1000.f;

	bool IsValid() const { return Type != EIFSoldierOrderType::None; }

	void Reset()
	{
		Type = EIFSoldierOrderType::None;
		TargetActor.Reset();
		Location = FVector::ZeroVector;
		Radius = 75.f;
		FacingYaw = 0.f;
		bHasFacingYaw = false;
		IssuedTime = -1000.f;
	}
};
