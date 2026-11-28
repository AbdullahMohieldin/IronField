#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "IFCrowdSubsystem.generated.h"

class AActor;
class AIFBaseCharacter;
class AIFSoldierCharacter;

/**
 * Lightweight game-world crowd coordinator providing O(N) spatial-hashed friendly
 * soft-separation during combat/escort and deterministic surround approach points,
 * while leaving settled formation units completely motionless on their slots.
 */
UCLASS()
class IRONFIELD_API UIFCrowdSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr float HeroPartingDistCm = 110.f;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterCharacter(AIFBaseCharacter* Character);
	void UnregisterCharacter(AIFBaseCharacter* Character);

	FVector ComputeCombatApproachPoint(const AIFSoldierCharacter& Soldier, const AActor* Target) const;

private:
	struct FUnitSnapshot
	{
		AIFBaseCharacter* Character = nullptr;
		FVector Location = FVector::ZeroVector;
		uint8 TeamId = 255;
		bool bIsHero = false;
		bool bReceivesCombatPush = false;
		bool bReceivesHeroParting = false;
	};

	struct FAttackerGroupKey
	{
		TObjectKey<const AActor> TargetKey;
		uint8 TeamId = 255;

		bool operator==(const FAttackerGroupKey& Other) const
		{
			return TargetKey == Other.TargetKey && TeamId == Other.TeamId;
		}

		friend uint32 GetTypeHash(const FAttackerGroupKey& Key)
		{
			return HashCombineFast(GetTypeHash(Key.TargetKey), ::GetTypeHash(Key.TeamId));
		}
	};

	void EnsureAttackerGroupsBuilt() const;

	TArray<TWeakObjectPtr<AIFBaseCharacter>> RegisteredCharacters;

	// Persistent zero-allocation scratch buffers reused across frames.
	TArray<FUnitSnapshot> CachedUnits;
	TMap<FIntPoint, int32> SpatialCellHead;
	TArray<int32> NextUnitInCell;

	// Per-frame flat attacker groupings keyed by (Target, TeamId) for O(k) ComputeCombatApproachPoint.
	mutable uint64 CachedAttackerGroupFrame = MAX_uint64;
	mutable TArray<const AIFSoldierCharacter*> CachedAttackerSoldiers;
	mutable TMap<FAttackerGroupKey, int32> TargetAttackerHead;
	mutable TArray<int32> NextAttackerInGroup;
};
