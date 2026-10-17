#include "Combat/IFProjectile.h"

#include "Combat/IFCombatTargetingUtils.h"
#include "Core/IFFeedbackUtils.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "NiagaraComponent.h"

AIFProjectile::AIFProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	SetRootComponent(CollisionSphere);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	CollisionSphere->SetGenerateOverlapEvents(true);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->UpdatedComponent = CollisionSphere;
	ProjectileMovement->bRotationFollowsVelocity = true;

	// InitializeProjectile sets Velocity in world space; never reinterpret it as local.
	ProjectileMovement->bInitialVelocityInLocalSpace = false;

	VFXComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("VFXComponent"));
	VFXComponent->SetupAttachment(RootComponent);
	VFXComponent->bAutoActivate = true;

	CollisionSphere->OnComponentBeginOverlap.AddDynamic(this, &AIFProjectile::HandleSphereBeginOverlap);
}

void AIFProjectile::BeginPlay()
{
	Super::BeginPlay();
	SetLifeSpan(LifeSpanSeconds);
}

void AIFProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CollisionSphere)
	{
		CollisionSphere->OnComponentBeginOverlap.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AIFProjectile::InitializeProjectile(const FIFProjectileSpawnArgs& Args)
{
	ProjectileInstigator = Args.Instigator;
	Damage = Args.Damage;
	DamageTypeClass = Args.DamageTypeClass;

	if (ProjectileInstigator && CollisionSphere)
	{
		CollisionSphere->IgnoreActorWhenMoving(ProjectileInstigator, true);
	}

	// Rotation and velocity share Args.LaunchDirection; nothing re-derives it.
	if (!Args.LaunchDirection.IsNearlyZero())
	{
		SetActorRotation(Args.LaunchDirection.Rotation());
	}

	// Speed knobs are the EditDefaultsOnly properties; tune ProjectileSpeed,
	// not the movement component, which this overwrites.
	if (ProjectileMovement)
	{
		ProjectileMovement->InitialSpeed = ProjectileSpeed;
		ProjectileMovement->MaxSpeed = ProjectileSpeed;
		ProjectileMovement->ProjectileGravityScale = ProjectileGravityScale;
		ProjectileMovement->Velocity = Args.LaunchDirection * ProjectileSpeed;
	}
}

void AIFProjectile::HandleSphereBeginOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	HandleImpact(OtherActor);
}

void AIFProjectile::HandleImpact(AActor* OtherActor)
{
	if (bHasHit || !OtherActor || OtherActor == this || OtherActor == ProjectileInstigator)
	{
		return;
	}

	if (Cast<AIFProjectile>(OtherActor))
	{
		return;
	}

	if (!IFCombatTargetingUtils::GetValidAttackTargetHealth(ProjectileInstigator, OtherActor))
	{
		// Pawns pass through (no friendly fire, corpses never eat shots);
		// anything else is geometry and must consume the projectile.
		if (!Cast<APawn>(OtherActor))
		{
			bHasHit = true;
			Destroy();
		}
		return;
	}

	bHasHit = true;
	IFCombatTargetingUtils::DeliverDamage(OtherActor, ProjectileInstigator, Damage, DamageTypeClass);
	PlayHitFeedback();
	Destroy();
}

void AIFProjectile::PlayHitFeedback() const
{
	IFFeedbackUtils::PlayAtLocation(GetWorld(), HitSound, HitVFX, GetActorLocation());
}
