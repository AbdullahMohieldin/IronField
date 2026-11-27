#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "IFCombatDataAsset.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class UParticleSystem;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat")
	bool bCausesHeavyStagger = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit")
	FName HitStartSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit")
	FName HitEndSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit", meta = (ClampMin = "0.0"))
	float HitRadius = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Hit", meta = (ClampMin = "1", ClampMax = "16"))
	int32 TraceSampleCount = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Combo", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackComboWindowStartNormalized = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Feedback")
	TSoftObjectPtr<UNiagaraSystem> HitVFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Feedback")
	TSoftObjectPtr<UParticleSystem> HitCascadeVFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Feedback", meta = (ClampMin = "0.0", ClampMax = "0.15"))
	float HitstopSeconds = 0.f;

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
		GuardAttack.bCausesHeavyStagger = true;
		GuardAttack.HitstopSeconds = 0.03f;
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block", meta = (ClampMin = "0.1"))
	float FallbackGuardBreakDurationSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|Block")
	FVector BlockVFXOffset = FVector(55.f, 0.f, 35.f);
};

USTRUCT(BlueprintType)
struct FIFLeapSlamConfig
{
	GENERATED_BODY()

	FIFLeapSlamConfig()
	{
		Attack.Damage = 45.f;
		Attack.StaminaCost = 25.f;
		Attack.bCausesHeavyStagger = true;
		Attack.HitStartSocket = TEXT("FX_Sword_Bottom");
		Attack.HitEndSocket = TEXT("FX_Sword_Top");
		Attack.HitRadius = 50.f;
		Attack.TraceSampleCount = 4;
		Attack.HitstopSeconds = 0.04f;
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|LeapSlam")
	FIFMeleeAttackStep Attack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|LeapSlam", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 4.0f;
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat", meta = (TitleProperty = "Montage"))
	TArray<FIFMeleeAttackStep> ComboSteps;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat", meta = (ClampMin = "0.0"))
	float ComboResetSeconds = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|AI")
	TArray<float> AIComboContinuationChances;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|AI", meta = (ClampMin = "0.0"))
	float AIComboBreatherSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|AI")
	bool bRandomizeAIComboOpener = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|AI", meta = (ClampMin = "0.0"))
	float MaxStaminaWaitSeconds = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|HitReact", meta = (ClampMin = "0.0"))
	float HitReactCooldownSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|HitReact", meta = (ClampMin = "0.0"))
	float AdditiveFlinchCooldownSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|HitReact")
	bool bAllowHitReactDuringAttack = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "IronField|Combat|VFX")
	TSoftObjectPtr<UParticleSystem> BlockCascadeVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat")
	FIFShieldBlockConfig ShieldBlock;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat")
	FIFLeapSlamConfig LeapSlam;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Combat")
	FIFDirectionalHitReactConfig DirectionalHitReacts;
};