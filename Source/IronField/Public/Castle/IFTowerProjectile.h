#pragma once

#include "CoreMinimal.h"
#include "Combat/IFTeamTypes.h"
#include "GameFramework/Actor.h"
#include "IFTowerProjectile.generated.h"

class AIFStructure;
class UPrimitiveComponent;
class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;

/**
 * Lightweight ballistic projectile fired by AIFGuardTower.
 * Friendly-fire safe, ECC_Camera ignored, short lifespan, passes OwningTower as DamageCauser.
 */
UCLASS()
class IRONFIELD_API AIFTowerProjectile : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AIFTowerProjectile();

	void InitializeProjectile(
		AIFStructure* InOwningTower,
		FGenericTeamId InTeamID,
		float InDamage,
		const FVector& InLaunchDirection);

	UFUNCTION(BlueprintPure, Category = "IronField|Projectile")
	float GetProjectileSpeed() const { return ProjectileSpeed; }

	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override { TeamID = InTeamID; }
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamID; }
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Projectile|Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Projectile|Components")
	TObjectPtr<UStaticMeshComponent> ProjectileMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Projectile|Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Projectile", meta = (ClampMin = "100.0"))
	float ProjectileSpeed = 2200.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Projectile", meta = (ClampMin = "0.2"))
	float MaxLifeSpanSeconds = 2.5f;

private:
	UFUNCTION()
	void HandleSphereOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSphereHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		FVector NormalImpulse,
		const FHitResult& Hit);

	TWeakObjectPtr<AIFStructure> OwningTower;
	FGenericTeamId TeamID = FGenericTeamId(IFTeamIds::Neutral);
	float Damage = 20.f;
	bool bHasImpacted = false;
};
