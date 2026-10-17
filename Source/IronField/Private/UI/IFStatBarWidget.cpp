#include "UI/IFStatBarWidget.h"

#include "Components/ProgressBar.h"
#include "Core/IFLog.h"

UIFStatBarWidget::UIFStatBarWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Engine quirk: UUserWidget suppresses NativeTick unless bHasScriptImplementedTick is true or TickFrequency is explicitly overridden.
	bHasScriptImplementedTick = true;
}

void UIFStatBarWidget::SetTargetPercent(float NewTargetPercent)
{
	TargetPercent = FMath::Clamp(NewTargetPercent, 0.f, 1.f);

	if (!bHasReceivedTarget)
	{
		bHasReceivedTarget = true;
		CurrentPercent = TargetPercent;
		if (ProgressBar)
		{
			ProgressBar->SetPercent(CurrentPercent);
		}
	}
}

void UIFStatBarWidget::SetFillColor(FLinearColor NewColor)
{
	FillColor = NewColor;
	if (ProgressBar)
	{
		ProgressBar->SetFillColorAndOpacity(FillColor);
	}
}

void UIFStatBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!ProgressBar)
	{
		UE_LOG(LogIronField, Warning, TEXT("[IF-UI] ProgressBar BindWidget is missing; the stat bar will not update."));
		return;
	}

	ProgressBar->SetFillColorAndOpacity(FillColor);
	ProgressBar->SetPercent(CurrentPercent);
}

void UIFStatBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!ProgressBar || FMath::IsNearlyEqual(CurrentPercent, TargetPercent))
	{
		return;
	}

	CurrentPercent = FMath::FInterpTo(CurrentPercent, TargetPercent, InDeltaTime, InterpSpeed);
	ProgressBar->SetPercent(CurrentPercent);
}
