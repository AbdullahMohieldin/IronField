#include "Pickup/IFHealPickup.h"

#include "Character/IFBaseCharacter.h"
#include "Character/IFPlayerCharacter.h"
#include "Core/IFFeedbackUtils.h"
#include "Core/IFLog.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Stats/IFHealthComponent.h"
#include "TimerManager.h"

namespace
{
	constexpr float PickupCollisionRadius = 100.f;
}

AIFHealPickup::AIFHealPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.033f;
	PrimaryActorTick.bStartWithTickEnabled = true;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->SetSphereRadius(PickupCollisionRadius);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionComponent->SetCollisionObjectType(ECC_WorldDynamic);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	CollisionComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	CollisionComponent->SetGenerateOverlapEvents(true);
	RootComponent = CollisionComponent;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AIFHealPickup::BeginPlay()
{
	Super::BeginPlay();

	BaseLocation = GetActorLocation();
	AccumulatedTime = 0.f;
	CollisionComponent->OnComponentBeginOverlap.AddDynamic(this, &AIFHealPickup::OnOverlapBegin);
	StartLifeTimer();
}

void AIFHealPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LifeTimerHandle);
	}
	if (CollisionComponent)
	{
		CollisionComponent->OnComponentBeginOverlap.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

void AIFHealPickup::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AccumulatedTime += DeltaTime;

	// Offset from the spawn anchor so the bob oscillates in place instead of drifting.
	const float BobOffset = FMath::Sin(AccumulatedTime * BobFrequency * 2.f * PI) * BobAmplitude;
	FVector Location = BaseLocation;
	Location.Z += BobOffset;
	SetActorLocation(Location);

	FRotator Rotation = GetActorRotation();
	Rotation.Yaw += SpinSpeed * DeltaTime;
	SetActorRotation(Rotation);
}

bool AIFHealPickup::TryHealActor(AActor* Actor)
{
	if (!Actor || Actor == this)
	{
		return false;
	}

	// Only heal the player character
	AIFPlayerCharacter* const Player = Cast<AIFPlayerCharacter>(Actor);
	if (!Player)
	{
		return false;
	}

	UIFHealthComponent* const Health = Player->GetHealthComponent();
	if (!Health || Health->IsDead() || Health->GetHealthPercent() >= 1.f)
	{
		return false;
	}

	Health->ApplyHealing(HealAmount);
	UE_LOG(LogIronField, Log, TEXT("[IF-Heal] Pickup healed %s for %.0f HP."), *GetNameSafe(Player), HealAmount);
	return true;
}

void AIFHealPickup::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (TryHealActor(OtherActor))
	{
		OnPickedUp(OtherActor);

		if (PickupSound)
		{
			IFFeedbackUtils::PlayAtLocation(GetWorld(), PickupSound, nullptr, GetActorLocation());
		}

		DestroyPickup();
	}
}

void AIFHealPickup::OnPickedUp_Implementation(AActor* Picker)
{
}

void AIFHealPickup::StartLifeTimer()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().SetTimer(LifeTimerHandle, this, &AIFHealPickup::DestroyPickup, LifeSeconds, false);
	}
}

void AIFHealPickup::DestroyPickup()
{
	Destroy();
}