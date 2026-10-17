#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Wave/IFWaveSpawnable.h"
#include "IFBaseCharacter.generated.h"

class UAnimInstance;
class UIFCombatComponent;
class UIFHealthComponent;
class USoundBase;
class UNiagaraSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterDied, AIFBaseCharacter*, Character);

UCLASS()
class IRONFIELD_API AIFBaseCharacter : public ACharacter, public IGenericTeamAgentInterface, public IIFWaveSpawnable
{
	GENERATED_BODY()

public:
	// IIFWaveSpawnable
	virtual void SetSpawnContext(const FIFSpawnContext& InContext) override;
	virtual const FIFSpawnContext& GetSpawnContext() const override { return SpawnContext; }

	// IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override { TeamID = InTeamID; }
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamID; }
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Character|Events")
	FOnCharacterDied OnCharacterDied;

	AIFBaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "IronField|Character|Components")
	UIFHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Character|Components")
	UIFCombatComponent* GetCombatComponent() const { return CombatComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Character|State")
	bool IsDead() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Character|State")
	bool IsAttacking() const;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Landed(const FHitResult& Hit) override;

protected:
	UPROPERTY(Transient)
	FIFSpawnContext SpawnContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Team")
	FGenericTeamId TeamID = FGenericTeamId(0);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Character|Animation")
	float DeathMontageBlendOutTime = 0.15f;

	// Empty until assigned in Blueprint; guarded at play time.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Character|Feedback")
	TObjectPtr<USoundBase> DeathSound;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Character|Feedback")
	TObjectPtr<UNiagaraSystem> DeathVFX;

	virtual void OnDeathStarted() {}

	UAnimInstance* GetMeshAnimInstance() const;

	/** Restores collision, movement, and the death gate so the character can live (and die) again. */
	void RestoreAliveState();

	void StopMovementForDeath();
	void DisableCollisionForDeath();

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Character|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Character|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFCombatComponent> CombatComponent;

	UFUNCTION()
	void HandleDeath();

	void BindGameplayDelegates();
	void UnbindGameplayDelegates();
	void PlayDeathFeedback() const;

	// Death re-entrancy guard; cleared by RestoreAliveState so a revived character can die again.
	bool bHasDied = false;

	// Live settings captured in BeginPlay and restored verbatim on revive.
	FName CapsuleCollisionProfile;
	FName MeshCollisionProfile;
	bool bSavedOrientRotationToMovement = false;
	bool bSavedUseControllerDesiredRotation = false;
};
