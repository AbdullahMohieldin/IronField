#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IFHealPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class USoundBase;

UCLASS(Blueprintable, BlueprintType)
class IRONFIELD_API AIFHealPickup : public AActor
{
	GENERATED_BODY()

public:
	AIFHealPickup();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Pickup|Config", meta = (ClampMin = "0.0"))
	float HealAmount = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Pickup|Config", meta = (ClampMin = "0.1"))
	float LifeSeconds = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Pickup|Visuals", meta = (ClampMin = "0.0"))
	float BobAmplitude = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Pickup|Visuals", meta = (ClampMin = "0.0"))
	float BobFrequency = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Pickup|Visuals", meta = (ClampMin = "0.0"))
	float SpinSpeed = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Pickup|Assets")
	TObjectPtr<USoundBase> PickupSound;

protected:
	UFUNCTION(BlueprintNativeEvent, Category = "IronField|Pickup|Events")
	void OnPickedUp(AActor* Picker);
	virtual void OnPickedUp_Implementation(AActor* Picker);

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Pickup|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Pickup|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	FTimerHandle LifeTimerHandle;
	float AccumulatedTime = 0.f;

	// Spawn-time anchor; Tick offsets from this so the bob never accumulates drift.
	FVector BaseLocation = FVector::ZeroVector;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void StartLifeTimer();
	void DestroyPickup();

	bool TryHealActor(AActor* Actor);

	friend class FIFHealPickupFullHealthTest;
};