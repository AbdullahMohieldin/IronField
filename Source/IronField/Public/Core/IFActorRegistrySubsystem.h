#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IFActorRegistrySubsystem.generated.h"

namespace IFRegistryRoles
{
	inline const FName Player(TEXT("Player"));
	inline const FName Stronghold(TEXT("Stronghold"));
	inline const FName WaveManager(TEXT("WaveManager"));
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActorRoleRegistered, FName, ActorRole, AActor*, Actor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActorRoleUnregistered, FName, ActorRole, AActor*, Actor);
DECLARE_DYNAMIC_DELEGATE_OneParam(FIFOnActorRegisteredDynamicCallback, AActor*, Actor);

/**
 * World subsystem managing role-keyed singleton actor registrations (e.g. Player, Stronghold, WaveManager).
 * Decouples actor lookup from concrete subsystem types and concrete class dependencies.
 */
UCLASS()
class IRONFIELD_API UIFActorRegistrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "IronField|Registry|Events")
	FOnActorRoleRegistered OnActorRegistered;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Registry|Events")
	FOnActorRoleUnregistered OnActorUnregistered;

	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "IronField|Registry")
	void RegisterActor(FName ActorRole, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "IronField|Registry")
	void UnregisterActor(FName ActorRole, AActor* Actor);

	UFUNCTION(BlueprintPure, Category = "IronField|Registry")
	AActor* GetActor(FName ActorRole) const;

	UFUNCTION(BlueprintPure, Category = "IronField|Registry")
	bool HasActor(FName ActorRole) const;

	/** Native C++ callback: executed immediately if already registered, or on registration. Exactly once. */
	void WhenRegistered(FName ActorRole, const UObject* ContextObject, TFunction<void(AActor*)> Callback);

	/** Blueprint / Dynamic callback: executed immediately if already registered, or on registration. Exactly once. */
	UFUNCTION(BlueprintCallable, Category = "IronField|Registry", meta = (DefaultToSelf = "ContextObject"))
	void WhenRegistered(FName ActorRole, UObject* ContextObject, FIFOnActorRegisteredDynamicCallback Callback);

	template <typename TActor>
	TActor* GetActor(FName ActorRole) const
	{
		return Cast<TActor>(GetActor(ActorRole));
	}

	template <typename TActor>
	void RegisterActor(FName ActorRole, TActor* Actor)
	{
		RegisterActor(ActorRole, Cast<AActor>(Actor));
	}

private:
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<AActor>> RegisteredActors;

	struct FIFNativeRegistrationCallback
	{
		TWeakObjectPtr<const UObject> ContextObject;
		TFunction<void(AActor*)> Callback;
	};

	struct FIFDynamicRegistrationCallback
	{
		TWeakObjectPtr<const UObject> ContextObject;
		FIFOnActorRegisteredDynamicCallback Callback;
	};

	TMap<FName, TArray<FIFNativeRegistrationCallback>> PendingNativeCallbacks;
	TMap<FName, TArray<FIFDynamicRegistrationCallback>> PendingDynamicCallbacks;
};
