#include "Building/IFStronghold.h"

#include "Combat/IFTeamTypes.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/IFFeedbackUtils.h"
#include "Core/IFActorRegistrySubsystem.h"
#include "Engine/World.h"
#include "Stats/IFHealthComponent.h"

namespace
{
	// Sole lose condition: survives ~50 melee hits @16 / ~80 mage hits @10.
	// Pure HP pool with no regen. BP can still override via the health component.
	constexpr float StrongholdDefaultMaxHealth = 800.f;
}

AIFStronghold::AIFStronghold()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCanEverAffectNavigation(false);

	HealthComponent = CreateDefaultSubobject<UIFHealthComponent>(TEXT("HealthComponent"));

	HealthComponent->SetMaxHealth(StrongholdDefaultMaxHealth);
}

void AIFStronghold::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent)
	{
		HealthComponent->OnHealthDepleted.AddDynamic(this, &AIFStronghold::HandleDeath);
		HealthComponent->OnHealthChanged.AddDynamic(this, &AIFStronghold::HandleHealthChanged);
		LastHealthPercent = HealthComponent->GetHealthPercent();
	}

	if (UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Registry->RegisterActor(IFRegistryRoles::Stronghold, this);
		}
	}
}

void AIFStronghold::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HealthComponent)
	{
		HealthComponent->OnHealthDepleted.RemoveAll(this);
		HealthComponent->OnHealthChanged.RemoveAll(this);
	}

	if (UWorld* const World = GetWorld())
	{
		if (UIFActorRegistrySubsystem* const Registry = World->GetSubsystem<UIFActorRegistrySubsystem>())
		{
			Registry->UnregisterActor(IFRegistryRoles::Stronghold, this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AIFStronghold::HandleDeath()
{
	HandleDestruction();
}

void AIFStronghold::HandleHealthChanged(float Percent)
{
	if (Percent >= LastHealthPercent)
	{
		LastHealthPercent = Percent;
		return;
	}

	LastHealthPercent = Percent;
	PlayHitFeedback();
}

void AIFStronghold::PlayHitFeedback()
{
	IFFeedbackUtils::PlayAtLocation(GetWorld(), HitSound, HitVFX, GetActorLocation());
}

void AIFStronghold::HandleDestruction()
{
	OnStrongholdDestroyed.Broadcast(this);

	// Disable the visible mesh's collision when the stronghold is destroyed.
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(false);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	SetActorEnableCollision(false);
}

ETeamAttitude::Type AIFStronghold::GetTeamAttitudeTowards(const AActor& Other) const
{
	return IFTeamUtils::GetAttitudeTowards(this, &Other);
}
