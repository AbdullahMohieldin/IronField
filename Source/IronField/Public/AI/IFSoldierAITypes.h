#pragma once

#include "CoreMinimal.h"
#include "IFSoldierAITypes.generated.h"

class AActor;

/** High-level behaviour selected by AIFSoldierAIController. */
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
	ReturnToAnchor
};

/** Coarse Behavior Tree branch mapped from EIFSoldierAction and mirrored to the Blackboard. */
UENUM(BlueprintType)
enum class EIFSoldierBranch : uint8
{
	Idle,
	Combat,
	Move
};

inline EIFSoldierBranch ResolveSoldierBranch(EIFSoldierAction Action)
{
	switch (Action)
	{
	case EIFSoldierAction::AttackTarget:
	case EIFSoldierAction::Defend:
		return EIFSoldierBranch::Combat;

	case EIFSoldierAction::MoveTo:
	case EIFSoldierAction::FollowActor:
	case EIFSoldierAction::ReturnToAnchor:
	case EIFSoldierAction::Advance:
		return EIFSoldierBranch::Move;

	case EIFSoldierAction::None:
	case EIFSoldierAction::Idle:
	default:
		return EIFSoldierBranch::Idle;
	}
}

/** Explicit player orders. Reaching a MoveToLocation re-anchors the soldier and hands off to autonomous defense. */
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
