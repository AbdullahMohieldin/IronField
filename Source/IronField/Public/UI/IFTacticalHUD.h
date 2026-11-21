#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Tactical/IFTacticalTypes.h"
#include "IFTacticalHUD.generated.h"

/**
 * Lightweight HUD drawing the 2D marquee selection box and formation facing chevrons
 * during active Tactical Command Mode drags.
 */
UCLASS()
class IRONFIELD_API AIFTacticalHUD : public AHUD
{
	GENERATED_BODY()

public:
	void SetMarqueeSelectionRect(bool bInActive, const FVector2D& InStartScreenPos, const FVector2D& InCurrentScreenPos);
	void SetFormationPreview(bool bInActive, const TArray<FIFFormationSlot>& InSlots);

	virtual void DrawHUD() override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|HUD")
	FLinearColor MarqueeFillColor = FLinearColor(0.15f, 0.65f, 1.f, 0.16f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|HUD")
	FLinearColor MarqueeBorderColor = FLinearColor(0.35f, 0.82f, 1.f, 0.92f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|HUD", meta = (ClampMin = "0.5"))
	float MarqueeBorderThickness = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|HUD")
	FLinearColor FormationFacingArrowColor = FLinearColor(0.95f, 0.78f, 0.30f, 0.95f);

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|HUD", meta = (ClampMin = "0.5"))
	float FormationFacingArrowThickness = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|HUD", meta = (ClampMin = "10.0"))
	float FormationFacingArrowWorldLength = 54.f;

	UPROPERTY(EditDefaultsOnly, Category = "IronField|Tactical|HUD", meta = (ClampMin = "2.0"))
	float FormationFacingChevronScreenSize = 6.f;

private:
	void DrawMarqueeBox();
	void DrawFormationFacingArrows();

	bool bIsMarqueeActive = false;
	FVector2D MarqueeStartScreenPos = FVector2D::ZeroVector;
	FVector2D MarqueeCurrentScreenPos = FVector2D::ZeroVector;

	bool bIsFormationPreviewActive = false;
	TArray<FIFFormationSlot> FormationPreviewSlots;
};
