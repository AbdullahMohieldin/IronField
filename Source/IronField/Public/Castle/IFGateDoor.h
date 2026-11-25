#pragma once

#include "CoreMinimal.h"
#include "Castle/IFStructure.h"
#include "Core/IFInteractable.h"
#include "IFGateDoor.generated.h"

class AIFGateDoor;
class AIFPlayerController;
class UGeometryCollection;
class USceneComponent;
class USphereComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGateDoorStateChanged, AIFGateDoor*, GateDoor, bool, bIsOpen);

/**
 * Double-leaf destructible castle gate door mounted inside a Gate archway (SM_Gate_Single).
 * Zero idle tick: ticks only during the 0.35s open/close swing interpolation.
 */
UCLASS()
class IRONFIELD_API AIFGateDoor : public AIFStructure, public IIFInteractable
{
	GENERATED_BODY()

public:
	AIFGateDoor();

	UPROPERTY(BlueprintAssignable, Category = "IronField|GateDoor|Events")
	FOnGateDoorStateChanged OnGateDoorStateChanged;

	UFUNCTION(BlueprintCallable, Category = "IronField|GateDoor")
	void ToggleGateOpen();

	UFUNCTION(BlueprintCallable, Category = "IronField|GateDoor")
	void SetGateOpen(bool bNewOpen);

	UFUNCTION(BlueprintPure, Category = "IronField|GateDoor")
	bool IsGateOpen() const { return bIsGateOpen; }

	UFUNCTION(BlueprintPure, Category = "IronField|GateDoor")
	AIFStructure* GetHostGate() const { return GetHostStructure(); }

	UFUNCTION(BlueprintPure, Category = "IronField|GateDoor")
	float GetInteractionRadiusCm() const { return InteractionRadiusCm; }

	void SetHostGate(AIFStructure* InHostGate);

	// IIFInteractable
	virtual bool CanInteract(AActor* Interactor) const override;
	virtual void Interact(AActor* Interactor) override;
	virtual FText GetInteractPromptText(AActor* Interactor) const override;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnTeamChanged(FGenericTeamId PreviousTeam, FGenericTeamId NewTeam) override;
	virtual void OnStructureDeactivated() override;
	virtual void TriggerDestructionFeedback() override;
	virtual bool ShouldStripPrimitiveCollision(const UPrimitiveComponent* Prim) const override;
	virtual void GatherStructureAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<USceneComponent> LeftDoorPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<UStaticMeshComponent> LeftDoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<USceneComponent> RightDoorPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<UStaticMeshComponent> RightDoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<USphereComponent> InteractionZone;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Config", meta = (ClampMin = "10.0", ClampMax = "135.0"))
	float OpenAngleDegrees = 90.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Config", meta = (ClampMin = "0.05"))
	float SwingDurationSeconds = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Config", meta = (ClampMin = "80.0"))
	float InteractionRadiusCm = 280.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Destruction")
	TSoftObjectPtr<UGeometryCollection> LeftDoorDestructionGC;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Destruction")
	TSoftObjectPtr<UGeometryCollection> RightDoorDestructionGC;

private:
	UFUNCTION()
	void HandleInteractionBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleInteractionEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	void ApplyDoorLeafYaw(float Alpha);
	void UnregisterFromNearbyControllers();

	TWeakObjectPtr<AIFPlayerController> RegisteredController;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|GateDoor|State", meta = (AllowPrivateAccess = "true"))
	bool bIsGateOpen = false;

	float CurrentOpenAlpha = 0.f;
	float TargetOpenAlpha = 0.f;
};
