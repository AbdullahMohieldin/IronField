#include "Castle/IFTowerProjectile.h"

#include "Castle/IFStructure.h"
#include "Character/IFBaseCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IFGameTypes.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"

AIFTowerProjectile::AIFTowerProjectile()
{
	PrimaryActorTick.bCanEverTick = false;

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->InitSphereRadius(18.f);
	CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(IFCollision::PawnDawn, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(IFCollision::PawnDusk, ECR_Overlap);
	CollisionSphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(IFCollision::Damageable, ECR_Block);
	CollisionSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionSphere->SetGenerateOverlapEvents(true);
	CollisionSphere->SetCanEverAffectNavigation(false);
	SetRootComponent(CollisionSphere);

	ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
	ProjectileMesh->SetupAttachment(CollisionSphere);
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	ProjectileMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	ProjectileMesh->SetCanEverAffectNavigation(false);
	ProjectileMesh->SetCastShadow(false);

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->InitialSpeed = ProjectileSpeed;
	ProjectileMovement->MaxSpeed = ProjectileSpeed;
	ProjectileMovement->ProjectileGravityScale = 0.f;
	ProjectileMovement->bInitialVelocityInLocalSpace = false;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = false;

	InitialLifeSpan = MaxLifeSpanSeconds;
}

void AIFTowerProjectile::InitializeProjectile(
	AIFStructure* InOwningTower,
	FGenericTeamId InTeamID,
	float InDamage,
	const FVector& InLaunchDirection)
{
	OwningTower = InOwningTower;
	TeamID = InTeamID;
	Damage = FMath::Max(0.f, InDamage);

	if (IsValid(InOwningTower) && IsValid(CollisionSphere))
	{
		CollisionSphere->IgnoreActorWhenMoving(InOwningTower, true);
	}

	const FVector Dir = InLaunchDirection.GetSafeNormal();
	if (IsValid(ProjectileMovement) && !Dir.IsNearlyZero())
	{
		ProjectileMovement->InitialSpeed = ProjectileSpeed;
		ProjectileMovement->MaxSpeed = ProjectileSpeed;
		ProjectileMovement->Velocity = Dir * ProjectileSpeed;
	}
}

void AIFTowerProjectile::BeginPlay()
{
	Super::BeginPlay();

	SetLifeSpan(MaxLifeSpanSeconds);

	if (IsValid(CollisionSphere))
	{
		CollisionSphere->OnComponentBeginOverlap.AddUniqueDynamic(this, &AIFTowerProjectile::HandleSphereOverlap);
		CollisionSphere->OnComponentHit.AddUniqueDynamic(this, &AIFTowerProjectile::HandleSphereHit);
	}
}

ETeamAttitude::Type AIFTowerProjectile::GetTeamAttitudeTowards(const AActor& Other) const
{
	return IFTeamUtils::SolveAttitude(TeamID, IFTeamUtils::GetActorTeamId(&Other));
}

void AIFTowerProjectile::HandleSphereOverlap(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	int32,
	bool,
	const FHitResult&)
{
	if (bHasImpacted || !IsValid(OtherActor) || OtherActor == this || OtherActor == OwningTower.Get())
	{
		return;
	}

	if (!IFTeamUtils::AreHostile(this, OtherActor))
	{
		return;
	}

	if (const AIFBaseCharacter* const HitChar = Cast<AIFBaseCharacter>(OtherActor))
	{
		if (HitChar->IsDead())
		{
			return;
		}
	}

	bHasImpacted = true;

	// Pass the projectile itself as DamageCauser so Shield Block and directional hit-reacts resolve from the incoming bolt's direction.
	AController* const InstigatorController = OwningTower.IsValid() ? OwningTower->GetInstigatorController() : nullptr;
	UGameplayStatics::ApplyDamage(OtherActor, Damage, InstigatorController, this, UDamageType::StaticClass());
	Destroy();
}

void AIFTowerProjectile::HandleSphereHit(
	UPrimitiveComponent*,
	AActor* OtherActor,
	UPrimitiveComponent*,
	FVector,
	const FHitResult&)
{
	if (bHasImpacted || OtherActor == OwningTower.Get())
	{
		return;
	}

	bHasImpacted = true;
	Destroy();
}
