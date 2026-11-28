#include "UI/IFTacticalHUD.h"

#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"

void AIFTacticalHUD::SetMarqueeSelectionRect(bool bInActive, const FVector2D& InStartScreenPos, const FVector2D& InCurrentScreenPos)
{
	bIsMarqueeActive = bInActive;
	MarqueeStartScreenPos = InStartScreenPos;
	MarqueeCurrentScreenPos = InCurrentScreenPos;
}

void AIFTacticalHUD::SetFormationPreview(bool bInActive, const TArray<FIFFormationSlot>& InSlots)
{
	bIsFormationPreviewActive = bInActive && !InSlots.IsEmpty();
	if (bIsFormationPreviewActive)
	{
		FormationPreviewSlots = InSlots;
	}
	else
	{
		FormationPreviewSlots.Reset();
	}
}

void AIFTacticalHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!IsValid(Canvas))
	{
		return;
	}

	if (bIsMarqueeActive)
	{
		DrawMarqueeBox();
	}

	if (bIsFormationPreviewActive)
	{
		DrawFormationFacingArrows();
	}
}

void AIFTacticalHUD::DrawMarqueeBox()
{
	const float MinX = FMath::Min(MarqueeStartScreenPos.X, MarqueeCurrentScreenPos.X);
	const float MinY = FMath::Min(MarqueeStartScreenPos.Y, MarqueeCurrentScreenPos.Y);
	const float MaxX = FMath::Max(MarqueeStartScreenPos.X, MarqueeCurrentScreenPos.X);
	const float MaxY = FMath::Max(MarqueeStartScreenPos.Y, MarqueeCurrentScreenPos.Y);
	const float Width = MaxX - MinX;
	const float Height = MaxY - MinY;

	if (Width < 2.f && Height < 2.f)
	{
		return;
	}

	DrawRect(MarqueeFillColor, MinX, MinY, Width, Height);

	DrawLine(MinX, MinY, MaxX, MinY, MarqueeBorderColor, MarqueeBorderThickness);
	DrawLine(MaxX, MinY, MaxX, MaxY, MarqueeBorderColor, MarqueeBorderThickness);
	DrawLine(MaxX, MaxY, MinX, MaxY, MarqueeBorderColor, MarqueeBorderThickness);
	DrawLine(MinX, MaxY, MinX, MinY, MarqueeBorderColor, MarqueeBorderThickness);
}

void AIFTacticalHUD::DrawFormationFacingArrows()
{
	const APlayerController* const PC = GetOwningPlayerController();
	if (!IsValid(PC))
	{
		return;
	}

	constexpr float VerticalLift = 12.f;
	for (const FIFFormationSlot& Slot : FormationPreviewSlots)
	{
		const FVector FacingDir = FRotator(0.f, Slot.FacingYaw, 0.f).Vector();
		const FVector BaseWorld = Slot.Location + FVector(0.f, 0.f, VerticalLift);
		const FVector TipWorld = BaseWorld + FacingDir * FormationFacingArrowWorldLength;

		FVector2D BaseScreen = FVector2D::ZeroVector;
		FVector2D TipScreen = FVector2D::ZeroVector;
		if (!PC->ProjectWorldLocationToScreen(BaseWorld, BaseScreen, true)
			|| !PC->ProjectWorldLocationToScreen(TipWorld, TipScreen, true))
		{
			continue;
		}

		const FVector2D Delta = TipScreen - BaseScreen;
		if (Delta.SizeSquared() < 4.f)
		{
			continue;
		}

		DrawLine(BaseScreen.X, BaseScreen.Y, TipScreen.X, TipScreen.Y, FormationFacingArrowColor, FormationFacingArrowThickness);

		const FVector2D Dir2D = Delta.GetSafeNormal();
		const FVector2D Perp2D(-Dir2D.Y, Dir2D.X);
		const float HeadSize = FMath::Min(FormationFacingChevronScreenSize, Delta.Size() * 0.65f);
		const FVector2D WingBase = TipScreen - Dir2D * HeadSize;
		const FVector2D LeftWing = WingBase + Perp2D * (HeadSize * 0.65f);
		const FVector2D RightWing = WingBase - Perp2D * (HeadSize * 0.65f);

		DrawLine(TipScreen.X, TipScreen.Y, LeftWing.X, LeftWing.Y, FormationFacingArrowColor, FormationFacingArrowThickness);
		DrawLine(TipScreen.X, TipScreen.Y, RightWing.X, RightWing.Y, FormationFacingArrowColor, FormationFacingArrowThickness);
	}
}
