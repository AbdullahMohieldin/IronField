#pragma once

#include "CoreMinimal.h"
#include "Combat/IFTeamTypes.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Character.h"
#include "IFBaseCharacter.generated.h"

class UIFCombatComponent;
class UIFHealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCharacterDied, AIFBaseCharacter*, Character);

UCLASS()
class IRONFIELD_API AIFBaseCharacter : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AIFBaseCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// Subobject name of the combat slot; subclasses swap its class via SetDefaultSubobjectClass.
	static const FName CombatComponentName;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Character|Events")
	FOnCharacterDied OnCharacterDied;

	UFUNCTION(BlueprintPure, Category = "IronField|Character|Components")
	UIFHealthComponent* GetHealthComponent() const { return HealthComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Character|Components")
	UIFCombatComponent* GetCombatComponent() const { return CombatComponent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Character|State")
	bool IsDead() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Character|State")
	bool IsAlive() const { return !IsDead(); }

	/** Returns the cached horizontal push direction from the most recent incoming damage source. */
	UFUNCTION(BlueprintPure, Category = "IronField|Character|Combat")
	FVector GetLastDamageDirection() const { return LastDamageDirection; }

	/** Temporarily slows actor-local time dilation for crisp impact feel without altering global world time. */
	void ApplyLocalHitstop(float DurationSeconds, float TimeDilation = 0.05f);

	/** Restores actor-local time dilation to 1.0 and cancels any pending hitstop timer. */
	void ClearLocalHitstop();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;

	// IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override { TeamID = InTeamID; }
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamID; }
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	virtual void Landed(const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Team")
	FGenericTeamId TeamID = FGenericTeamId(IFTeamIds::Neutral);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Character|Animation", meta = (ClampMin = "0.0"))
	float DeathMontageBlendOutTime = 0.15f;

	virtual void OnDeathStarted() {}

	// Bound to UIFHealthComponent::OnDeath. Soldiers override this to tear down AI after
	// the shared death work (movement stop, collision, broadcast) has run.
	UFUNCTION()
	virtual void HandleDeath(AActor* DeadActor);

	// Restores collision, movement, and the death gate so the character can live (and die) again.
	void RestoreAliveState();

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Character|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFHealthComponent> HealthComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Character|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIFCombatComponent> CombatComponent;

	void StopMovementForDeath();
	void DisableCollisionForDeath();

	// Death re-entrancy guard; cleared by RestoreAliveState so a revived character can die again.
	bool bHasDied = false;

	// Live collision state captured in BeginPlay and restored verbatim on revive.
	FName CapsuleCollisionProfile;
	ECollisionEnabled::Type CapsuleCollisionEnabled = ECollisionEnabled::QueryAndPhysics;
	TEnumAsByte<ECollisionResponse> SavedCapsulePawnResponse = ECR_Block;
	FName MeshCollisionProfile;
	ECollisionEnabled::Type MeshCollisionEnabled = ECollisionEnabled::NoCollision;
	bool bCapsuleAffectsNavigation = true;
	bool bMeshAffectsNavigation = false;
	bool bSavedOrientRotationToMovement = false;
	bool bSavedUseControllerDesiredRotation = false;

	FVector LastDamageDirection = FVector::ZeroVector;
	FTimerHandle HitstopTimerHandle;
	bool bInLocalHitstop = false;
};