#pragma once

#include "CoreMinimal.h"
#include "Castle/IFStructure.h"
#include "Core/IFInteractable.h"
#include "IFGateDoor.generated.h"

class AIFGateDoor;
class UGeometryCollection;
class USceneComponent;
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

	void SetHostGate(AIFStructure* InHostGate);

	// IIFInteractable
	virtual bool CanInteract(AActor* Interactor) const override;
	virtual void Interact(AActor* Interactor) override;
	virtual FText GetInteractPromptText(AActor* Interactor) const override;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void TriggerDestructionFeedback() override;
	virtual void GatherStructureAssetPaths(TArray<FSoftObjectPath>& OutAssetPaths) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<USceneComponent> LeftDoorPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<UStaticMeshComponent> LeftDoorMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<USceneComponent> RightDoorPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|GateDoor|Components")
	TObjectPtr<UStaticMeshComponent> RightDoorMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Config", meta = (ClampMin = "10.0", ClampMax = "135.0"))
	float OpenAngleDegrees = 90.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Config", meta = (ClampMin = "0.05"))
	float SwingDurationSeconds = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Destruction")
	TSoftObjectPtr<UGeometryCollection> LeftDoorDestructionGC;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|GateDoor|Destruction")
	TSoftObjectPtr<UGeometryCollection> RightDoorDestructionGC;

private:
	void ApplyDoorLeafYaw(float Alpha);

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|GateDoor|State", meta = (AllowPrivateAccess = "true"))
	bool bIsGateOpen = false;

	float CurrentOpenAlpha = 0.f;
	float TargetOpenAlpha = 0.f;
};
