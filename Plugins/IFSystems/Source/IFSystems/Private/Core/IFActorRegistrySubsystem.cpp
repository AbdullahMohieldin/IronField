#include "Core/IFActorRegistrySubsystem.h"

#include "Core/IFLog.h"

void UIFActorRegistrySubsystem::Deinitialize()
{
	PendingNativeCallbacks.Empty();
	PendingDynamicCallbacks.Empty();
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

	if (TArray<FIFNativeRegistrationCallback>* const Callbacks = PendingNativeCallbacks.Find(ActorRole))
	{
		TArray<FIFNativeRegistrationCallback> LocalCallbacks = MoveTemp(*Callbacks);
		PendingNativeCallbacks.Remove(ActorRole);
		for (const FIFNativeRegistrationCallback& Item : LocalCallbacks)
		{
			if (Item.ContextObject.IsValid() || Item.ContextObject.Get() == nullptr)
			{
				if (Item.Callback)
				{
					Item.Callback(Actor);
				}
			}
		}
	}

	if (TArray<FIFDynamicRegistrationCallback>* const DynamicCallbacks = PendingDynamicCallbacks.Find(ActorRole))
	{
		TArray<FIFDynamicRegistrationCallback> LocalCallbacks = MoveTemp(*DynamicCallbacks);
		PendingDynamicCallbacks.Remove(ActorRole);
		for (const FIFDynamicRegistrationCallback& Item : LocalCallbacks)
		{
			if (Item.ContextObject.IsValid() || Item.ContextObject.Get() == nullptr)
			{
				Item.Callback.ExecuteIfBound(Actor);
			}
		}
	}
}

void UIFActorRegistrySubsystem::WhenRegistered(FName ActorRole, const UObject* ContextObject, TFunction<void(AActor*)> Callback)
{
	if (ActorRole.IsNone() || !Callback)
	{
		return;
	}

	if (AActor* const Existing = GetActor(ActorRole))
	{
		Callback(Existing);
		return;
	}

	PendingNativeCallbacks.FindOrAdd(ActorRole).Add({ ContextObject, MoveTemp(Callback) });
}

void UIFActorRegistrySubsystem::WhenRegistered(FName ActorRole, UObject* ContextObject, FIFOnActorRegisteredDynamicCallback Callback)
{
	if (ActorRole.IsNone() || !Callback.IsBound())
	{
		return;
	}

	if (AActor* const Existing = GetActor(ActorRole))
	{
		Callback.Execute(Existing);
		return;
	}

	PendingDynamicCallbacks.FindOrAdd(ActorRole).Add({ ContextObject, MoveTemp(Callback) });
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
