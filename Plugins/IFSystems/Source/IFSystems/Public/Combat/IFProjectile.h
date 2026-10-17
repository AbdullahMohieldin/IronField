#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Actor.h"
#include "IFProjectile.generated.h"

class UDamageType;
class UProjectileMovementComponent;
class USphereComponent;
class USoundBase;
class UNiagaraSystem;
class UNiagaraComponent;

/** Single source of truth for a shot: spawn pose and flight come from one direction. */
USTRUCT()
struct IFSYSTEMS_API FIFProjectileSpawnArgs
{
	GENERATED_BODY()

	UPROPERTY()
	FVector SpawnLocation = FVector::ZeroVector;

	UPROPERTY()
	FVector LaunchDirection = FVector::ForwardVector;

	UPROPERTY()
	TObjectPtr<AActor> Instigator = nullptr;

	UPROPERTY()
	float Damage = 0.f;

	UPROPERTY()
	TSubclassOf<UDamageType> DamageTypeClass = nullptr;
};

UCLASS()
class IFSYSTEMS_API AIFProjectile : public AActor
{
	GENERATED_BODY()

public:
	AIFProjectile();

	void InitializeProjectile(const FIFProjectileSpawnArgs& Args);

	/** Unscaled collision radius, used to spawn clear of the shooter's collision. */
	float GetCollisionSphereRadius() const { return CollisionSphere ? CollisionSphere->GetUnscaledSphereRadius() : 0.f; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "IronField|Projectile|Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, Category = "IronField|Projectile|Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Projectile|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraComponent> VFXComponent;

	/** Fast enough that sprint can't outrun it, slow enough to stay dodgeable. */
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Projectile|Movement", meta = (ClampMin = "0.0"))
	float ProjectileSpeed = 900.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Projectile|Movement", meta = (ClampMin = "0.0"))
	float ProjectileGravityScale = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Projectile|Lifetime", meta = (ClampMin = "0.1"))
	float LifeSpanSeconds = 4.f;

	// Empty until assigned in Blueprint; guarded at play time.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Projectile|Feedback")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Projectile|Feedback")
	TObjectPtr<UNiagaraSystem> HitVFX;

private:
	UPROPERTY(Transient)
	TObjectPtr<AActor> ProjectileInstigator;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Projectile")
	float Damage = 0.f;

	UPROPERTY(VisibleInstanceOnly, Category = "IronField|Projectile")
	TSubclassOf<UDamageType> DamageTypeClass;

	bool bHasHit = false;

	UFUNCTION()
	void HandleSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void HandleImpact(AActor* OtherActor);
	void PlayHitFeedback() const;
};
