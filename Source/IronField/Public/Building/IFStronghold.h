#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "IFStronghold.generated.h"

class UIFHealthComponent;
class UStaticMeshComponent;
class USoundBase;
class UNiagaraSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStrongholdDestroyed, AIFStronghold*, Stronghold);

UCLASS()
/**
 * A damageable world mesh.  Its visible mesh is also its only hit target, so
 * melee weapons and projectiles use the same normal damage path as any actor
 * with a UIFHealthComponent.
 */
class IRONFIELD_API AIFStronghold : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AIFStronghold();

	// IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override { TeamID = InTeamID; }
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamID; }
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	UFUNCTION(BlueprintPure, Category = "IronField|Stronghold")
	UIFHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UPROPERTY(BlueprintAssignable, Category = "IronField|Stronghold")
	FOnStrongholdDestroyed OnStrongholdDestroyed;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Team")
	FGenericTeamId TeamID = FGenericTeamId(0);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Stronghold")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Stronghold")
	TObjectPtr<UIFHealthComponent> HealthComponent;

	// Empty until assigned in Blueprint; guarded at play time.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stronghold|Feedback")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Stronghold|Feedback")
	TObjectPtr<UNiagaraSystem> HitVFX;

private:
	UFUNCTION()
	void HandleDeath();

	UFUNCTION()
	void HandleHealthChanged(float Percent);

	void HandleDestruction();
	void PlayHitFeedback();

	float LastHealthPercent = 1.f;
};
