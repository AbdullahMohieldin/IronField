#pragma once

#include "CoreMinimal.h"
#include "Castle/IFCastleTypes.h"
#include "Combat/IFTeamTypes.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"
#include "IFTerritory.generated.h"

class AIFBaseCharacter;
class AIFStructure;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPrimitiveComponent;
class USceneComponent;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UWidgetComponent;
struct FStreamableHandle;

/**
 * Square battlefield Territory zone supporting free structure placement,
 * Zero-Idle-Tick Peaceful Flag Capture (Method A — preserves structures),
 * and Violent Keep Destruction (Method B — scorched-earth collapse).
 */
UCLASS()
class IRONFIELD_API AIFTerritory : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	AIFTerritory();

	UPROPERTY(BlueprintAssignable, Category = "IronField|Territory|Events")
	FOnTerritoryOwnershipChanged OnTerritoryOwnershipChanged;

	UPROPERTY(BlueprintAssignable, Category = "IronField|Territory|Events")
	FOnTerritoryCaptureProgressChanged OnCaptureProgressChanged;

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	FVector2D GetTerritoryHalfExtent() const { return TerritoryHalfExtent; }

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	bool ContainsPoint2D(const FVector& WorldLocation) const;

	// Exact 4-corner oriented box check in territory local space.
	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	bool ContainsOrientedBox2D(const FVector& WorldCenter, const FVector2D& HalfExtents, float YawDegrees) const;

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	FVector GetFlagWorldLocation() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	float GetFlagCaptureRadius() const { return FlagCaptureRadius; }

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	USphereComponent* GetFlagCaptureZone() const { return FlagCaptureZone; }

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	FTransform GetHeroRespawnTransform() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	bool HasActiveKeep() const;

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	AIFStructure* GetActiveKeep() const { return ActiveKeep.Get(); }

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	float GetCaptureProgress() const { return CaptureProgress; }

	UFUNCTION(BlueprintPure, Category = "IronField|Territory")
	EIFTerritoryCaptureState GetCaptureState() const { return CaptureState; }

	// Idempotent structure registration (safe regardless of BeginPlay order).
	void RegisterStructure(AIFStructure* Structure);
	void UnregisterStructure(AIFStructure* Structure);

	void GetOwnedStructures(TArray<AIFStructure*>& OutStructures) const;

	// Transfers territory ownership; bPreserveStructures=true flips all surviving buildings (Method A).
	void SetTerritoryOwner(FGenericTeamId NewTeamID, bool bPreserveStructures);

	// IGenericTeamAgentInterface
	virtual void SetGenericTeamId(const FGenericTeamId& InTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return TeamID; }
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<USceneComponent> TerritoryRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<UStaticMeshComponent> BorderNorth;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<UStaticMeshComponent> BorderSouth;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<UStaticMeshComponent> BorderEast;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<UStaticMeshComponent> BorderWest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<USphereComponent> FlagCaptureZone;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<UStaticMeshComponent> FlagPoleMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<UStaticMeshComponent> FlagBannerMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Components")
	TObjectPtr<UStaticMeshComponent> FlagZoneRingMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Layout")
	FVector2D TerritoryHalfExtent = FVector2D(1500.f, 1500.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Layout", meta = (ClampMin = "10.0"))
	float BorderThickness = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Layout")
	FVector FlagLocalOffset = FVector(0.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Territory|Layout")
	FVector HeroRespawnLocalOffset = FVector(-400.f, 0.f, 105.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IronField|Team")
	FGenericTeamId TeamID = FGenericTeamId(IFTeamIds::Neutral);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Capture", meta = (ClampMin = "100.0"))
	float FlagCaptureRadius = 220.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Capture", meta = (ClampMin = "1.0"))
	float FlagCaptureDurationSeconds = 12.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Capture", meta = (ClampMin = "0.05"))
	float FlagDecayRatePerSecond = 0.15f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Capture", meta = (ClampMin = "0.02"))
	float CaptureTickIntervalSeconds = 0.05f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	TSoftObjectPtr<UMaterialInterface> DawnBorderMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	TSoftObjectPtr<UMaterialInterface> DuskBorderMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	TSoftObjectPtr<UMaterialInterface> NeutralBorderMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	TSoftObjectPtr<UMaterialInterface> DawnFlagMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	TSoftObjectPtr<UMaterialInterface> DuskFlagMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	TSoftObjectPtr<UMaterialInterface> NeutralFlagMaterial;

	// Radial capture ring material driving CaptureProgress, BaseRingColor, FillRingColor, and IsContested.
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	TSoftObjectPtr<UMaterialInterface> FlagCaptureRingMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	FLinearColor DawnRingColor = FLinearColor(0.14f, 0.45f, 1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	FLinearColor DuskRingColor = FLinearColor(0.92f, 0.16f, 0.16f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Territory|Visuals")
	FLinearColor NeutralRingColor = FLinearColor(0.75f, 0.78f, 0.82f, 1.0f);

private:
	UFUNCTION()
	void HandleFlagZoneBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandleFlagZoneEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	UFUNCTION()
	void HandleRegisteredStructureDestroyed(AIFStructure* DestroyedStructure, AActor* KillerActor);

	void StartCaptureTimer();
	void StopCaptureTimer();
	void TickFlagCapture();
	void SetCaptureState(EIFTerritoryCaptureState NewState);
	void UpdatePerimeterGeometry();
	void ApplyFactionVisuals();
	void EnsureFlagRingMID();
	void UpdateFlagRingMaterial();
	FLinearColor GetTeamRingColor(FGenericTeamId InTeamID) const;
	void RequestTerritoryAssets();
	void HandleTerritoryAssetsLoaded();
	void DiscoverPrePlacedStructures();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlagRingMID;

	TArray<TWeakObjectPtr<AIFStructure>> OwnedStructures;
	TWeakObjectPtr<AIFStructure> ActiveKeep;
	TSet<TWeakObjectPtr<AIFBaseCharacter>> OverlappingCharacters;
	TSharedPtr<FStreamableHandle> TerritoryAssetsLoadHandle;

	FTimerHandle CaptureTimerHandle;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Territory|Capture", meta = (AllowPrivateAccess = "true"))
	float CaptureProgress = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "IronField|Territory|Capture", meta = (AllowPrivateAccess = "true"))
	EIFTerritoryCaptureState CaptureState = EIFTerritoryCaptureState::Idle;

	FGenericTeamId CapturingTeamID = FGenericTeamId(IFTeamIds::Neutral);
	bool bIsCollapsingTerritory = false;
};
