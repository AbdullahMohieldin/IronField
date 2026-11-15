#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "IFCombatDataAsset.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class USoundBase;

USTRUCT(BlueprintType)
struct FIFMeleeAttackStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat")
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat", meta = (ClampMin = "0.0"))
	float Damage = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat", meta = (ClampMin = "0.0"))
	float StaminaCost = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit")
	FName HitStartSocket = NAME_None;

	// Optional blade tip: when set and distinct, the sweep samples the full blade length.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit")
	FName HitEndSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit", meta = (ClampMin = "0.0"))
	float HitRadius = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit", meta = (ClampMin = "1", ClampMax = "16"))
	int32 TraceSampleCount = 4;

	// Opens the combo window by normalized montage time when no ComboWindow notify is authored.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Combo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackComboWindowStartNormalized = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Feedback")
	TSoftObjectPtr<UNiagaraSystem> HitVFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Feedback")
	TSoftObjectPtr<USoundBase> HitSound;
};

UCLASS(BlueprintType)
class IRONFIELD_API UIFCombatDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// Array length is the combo length; any pack works without C++ changes.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat", meta = (TitleProperty = "Montage"))
	TArray<FIFMeleeAttackStep> ComboSteps;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat", meta = (ClampMin = "0.0"))
	float ComboResetSeconds = 0.8f;
};