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

USTRUCT(BlueprintType)
struct FIFShieldBlockConfig
{
	GENERATED_BODY()

	FIFShieldBlockConfig()
	{
		GuardAttack.Damage = 22.f;
		GuardAttack.StaminaCost = 12.f;
		GuardAttack.HitStartSocket = TEXT("FX_Sword_Bottom");
		GuardAttack.HitEndSocket = TEXT("FX_Sword_Top");
		GuardAttack.HitRadius = 18.f;
		GuardAttack.TraceSampleCount = 6;
		GuardAttack.FallbackComboWindowStartNormalized = 0.65f;
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block")
	TSoftObjectPtr<UAnimMontage> DeflectMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block")
	TSoftObjectPtr<UAnimMontage> GuardBreakMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block")
	FIFMeleeAttackStep GuardAttack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float MaxBlockAngleDegrees = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block", meta = (ClampMin = "0.0"))
	float BlockStaminaCostMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block", meta = (ClampMin = "0.0"))
	float MinBlockEntryStamina = 5.f;
};

USTRUCT(BlueprintType)
struct FIFSpinAttackConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin")
	TSoftObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin", meta = (ClampMin = "0.0"))
	float Damage = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin", meta = (ClampMin = "0.0"))
	float StaminaCost = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin", meta = (ClampMin = "0.0"))
	float LeapSlamTraceRadius = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin")
	FName HitStartSocket = TEXT("FX_Sword_Bottom");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin")
	FName HitEndSocket = TEXT("FX_Sword_Top");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Spin", meta = (ClampMin = "1", ClampMax = "16"))
	int32 TraceSampleCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Feedback")
	TSoftObjectPtr<UNiagaraSystem> HitVFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Feedback")
	TSoftObjectPtr<USoundBase> HitSound;
};

USTRUCT(BlueprintType)
struct FIFDirectionalHitReactConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|HitReact")
	TSoftObjectPtr<UAnimMontage> HitReactFront;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|HitReact")
	TSoftObjectPtr<UAnimMontage> HitReactBack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|HitReact")
	TSoftObjectPtr<UAnimMontage> HitReactLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|HitReact")
	TSoftObjectPtr<UAnimMontage> HitReactRight;
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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat")
	FIFShieldBlockConfig ShieldBlock;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat")
	FIFSpinAttackConfig SpinAttack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat")
	FIFDirectionalHitReactConfig DirectionalHitReacts;
};