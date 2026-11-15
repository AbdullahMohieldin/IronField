#pragma once

#include "CoreMinimal.h"
#include "Combat/IFMeleeCombatComponent.h"
#include "IFPlayerCombatComponent.generated.h"

UCLASS(ClassGroup = (IronField), meta = (BlueprintSpawnableComponent))
class IRONFIELD_API UIFPlayerCombatComponent : public UIFMeleeCombatComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "IronField|Combat")
	void RequestAttack();

	void OpenComboWindow();
	void CloseComboWindow();

	virtual void HandleOwnerDeath() override;
	virtual void HandleOwnerRevived() override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleStepStarted() override;
	virtual bool TryBufferedAdvance() override;

private:
	void HandleFallbackComboWindow();
	void ClearComboWindowState();

	bool bAttackBuffered = false;
	bool bComboWindowOpen = false;
	bool bComboWindowNotifySeen = false;

	FTimerHandle FallbackComboWindowHandle;
};