#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
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
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool IsTickable() const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void RegisterCharacter(AIFBaseCharacter* Character);
	void UnregisterCharacter(AIFBaseCharacter* Character);

	FVector ComputeCombatApproachPoint(const AIFSoldierCharacter& Soldier, const AActor* Target) const;

private:
	bool HasUnitsNeedingSeparation() const;

	TArray<TWeakObjectPtr<AIFBaseCharacter>> RegisteredCharacters;
	TMap<FIntPoint, TArray<int32>> SpatialGrid;
};
