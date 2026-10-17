#include "Core/IFActorRegistrySubsystem.h"

#include "Core/IFLog.h"

void UIFActorRegistrySubsystem::Deinitialize()
{
	RegisteredActors.Empty();
	Super::Deinitialize();
}

void UIFActorRegistrySubsystem::RegisterActor(FName ActorRole, AActor* Actor)
{
	if (ActorRole.IsNone() || !Actor)
	{
		return;
	}

	TObjectPtr<AActor>& Slot = RegisteredActors.FindOrAdd(ActorRole);
	if (Slot && Slot != Actor)
	{
		UE_LOG(LogIronField, Warning,
			TEXT("[IF-Registry] Role '%s' already has actor '%s' registered. Overwriting with '%s'."),
			*ActorRole.ToString(), *GetNameSafe(Slot.Get()), *GetNameSafe(Actor));
	}

	Slot = Actor;
	OnActorRegistered.Broadcast(ActorRole, Actor);
}

void UIFActorRegistrySubsystem::UnregisterActor(FName ActorRole, AActor* Actor)
{
	if (ActorRole.IsNone() || !Actor)
	{
		return;
	}

	if (TObjectPtr<AActor>* const FoundSlot = RegisteredActors.Find(ActorRole))
	{
		if (*FoundSlot == Actor)
		{
			RegisteredActors.Remove(ActorRole);
			OnActorUnregistered.Broadcast(ActorRole, Actor);
		}
	}
}

AActor* UIFActorRegistrySubsystem::GetActor(FName ActorRole) const
{
	if (const TObjectPtr<AActor>* const Found = RegisteredActors.Find(ActorRole))
	{
		AActor* const Actor = Found->Get();
		return IsValid(Actor) ? Actor : nullptr;
	}
	return nullptr;
}

bool UIFActorRegistrySubsystem::HasActor(FName ActorRole) const
{
	return GetActor(ActorRole) != nullptr;
}
