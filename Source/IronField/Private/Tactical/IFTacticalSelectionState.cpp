#include "Tactical/IFTacticalSelectionState.h"

#include "AI/IFSoldierAIController.h"
#include "Castle/IFStructure.h"
#include "Character/IFSoldierCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

bool FIFTacticalSelectionState::ClearSelection(TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath)
{
	const bool bHadSelection = !SelectedSoldiers.IsEmpty();

	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			UnbindDeath(Soldier);
			Soldier->SetTacticalSelected(false, false);
		}
	}

	SelectedSoldiers.Reset();
	return bHadSelection;
}

void FIFTacticalSelectionState::ReplaceSelection(
	const TArray<AIFSoldierCharacter*>& NewSoldiers,
	bool bShowRingsNow,
	TFunctionRef<void(AIFSoldierCharacter*)> BindDeath,
	TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath)
{
	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			if (!NewSoldiers.Contains(Soldier))
			{
				UnbindDeath(Soldier);
				Soldier->SetTacticalSelected(false, false);
			}
		}
	}

	SelectedSoldiers.Reset();
	for (AIFSoldierCharacter* const Soldier : NewSoldiers)
	{
		if (IsValid(Soldier) && Soldier->IsAlive())
		{
			const bool bAlreadyAdded = SelectedSoldiers.ContainsByPredicate(
				[Soldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr) { return Ptr.Get() == Soldier; });
			if (!bAlreadyAdded)
			{
				SelectedSoldiers.Add(Soldier);
			}
			BindDeath(Soldier);
			Soldier->SetTacticalSelected(true, bShowRingsNow);
		}
	}
}

bool FIFTacticalSelectionState::AddToSelection(
	const TArray<AIFSoldierCharacter*>& SoldiersToAdd,
	bool bShowRingsNow,
	TFunctionRef<void(AIFSoldierCharacter*)> BindDeath)
{
	bool bChanged = false;
	for (AIFSoldierCharacter* const Soldier : SoldiersToAdd)
	{
		if (!IsValid(Soldier) || !Soldier->IsAlive())
		{
			continue;
		}

		const bool bAlreadySelected = SelectedSoldiers.ContainsByPredicate(
			[Soldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr) { return Ptr.Get() == Soldier; });
		if (!bAlreadySelected)
		{
			SelectedSoldiers.Add(Soldier);
			BindDeath(Soldier);
			Soldier->SetTacticalSelected(true, bShowRingsNow);
			bChanged = true;
		}
	}
	return bChanged;
}

bool FIFTacticalSelectionState::ToggleSoldierInSelection(
	AIFSoldierCharacter* Soldier,
	bool bShowRingsNow,
	TFunctionRef<void(AIFSoldierCharacter*)> BindDeath,
	TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath)
{
	if (!IsValid(Soldier) || !Soldier->IsAlive())
	{
		return false;
	}

	const int32 ExistingIndex = SelectedSoldiers.IndexOfByPredicate(
		[Soldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr) { return Ptr.Get() == Soldier; });
	if (ExistingIndex != INDEX_NONE)
	{
		UnbindDeath(Soldier);
		Soldier->SetTacticalSelected(false, false);
		SelectedSoldiers.RemoveAt(ExistingIndex);
	}
	else
	{
		SelectedSoldiers.Add(Soldier);
		BindDeath(Soldier);
		Soldier->SetTacticalSelected(true, bShowRingsNow);
	}

	return true;
}

bool FIFTacticalSelectionState::HandleSelectedSoldierDied(
	AIFSoldierCharacter* DeadSoldier,
	TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath)
{
	if (!IsValid(DeadSoldier))
	{
		return false;
	}

	UnbindDeath(DeadSoldier);
	DeadSoldier->SetTacticalSelected(false, false);

	const int32 Removed = SelectedSoldiers.RemoveAll(
		[DeadSoldier](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr)
		{
			return !Ptr.IsValid() || Ptr.Get() == DeadSoldier;
		});

	return Removed > 0;
}

bool FIFTacticalSelectionState::PruneInvalidSoldiers(TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath)
{
	const int32 PrevCount = SelectedSoldiers.Num();
	SelectedSoldiers.RemoveAll(
		[&UnbindDeath](const TWeakObjectPtr<AIFSoldierCharacter>& Ptr)
		{
			AIFSoldierCharacter* const Soldier = Ptr.Get();
			if (!IsValid(Soldier) || !Soldier->IsAlive())
			{
				if (IsValid(Soldier))
				{
					UnbindDeath(Soldier);
					Soldier->SetTacticalSelected(false, false);
				}
				return true;
			}
			return false;
		});

	return SelectedSoldiers.Num() != PrevCount;
}

void FIFTacticalSelectionState::SetSelectedSoldierRingsVisible(bool bVisible)
{
	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			Soldier->SetSelectionRingDisplayVisible(bVisible);
		}
	}
}

int32 FIFTacticalSelectionState::GetSelectedSoldierCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		if (const AIFSoldierCharacter* const Soldier = SoldierPtr.Get())
		{
			if (Soldier->IsAlive())
			{
				++Count;
			}
		}
	}
	return Count;
}

void FIFTacticalSelectionState::GatherAliveSelectedSquad(
	TArray<AIFSoldierCharacter*>& OutSquad,
	TArray<FVector>& OutLocations,
	FVector& OutCentroid) const
{
	OutSquad.Reset();
	OutLocations.Reset();
	OutCentroid = FVector::ZeroVector;

	for (const TWeakObjectPtr<AIFSoldierCharacter>& SoldierPtr : SelectedSoldiers)
	{
		AIFSoldierCharacter* const Soldier = SoldierPtr.Get();
		if (IsValid(Soldier) && Soldier->IsAlive() && IsValid(Soldier->GetSoldierController()))
		{
			const FVector Loc = Soldier->GetActorLocation();
			OutSquad.Add(Soldier);
			OutLocations.Add(Loc);
			OutCentroid += Loc;
		}
	}

	if (!OutSquad.IsEmpty())
	{
		OutCentroid /= static_cast<float>(OutSquad.Num());
	}
}

void FIFTacticalSelectionState::GatherSoldiersInMarqueeBox(
	UWorld* World,
	const APlayerController* PC,
	const APawn* PlayerPawn,
	const FVector2D& StartPos,
	const FVector2D& EndPos,
	TArray<AIFSoldierCharacter*>& OutSoldiers)
{
	OutSoldiers.Reset();
	if (!IsValid(World) || !IsValid(PC) || !IsValid(PlayerPawn))
	{
		return;
	}

	const FVector2D BoxMin(FMath::Min(StartPos.X, EndPos.X), FMath::Min(StartPos.Y, EndPos.Y));
	const FVector2D BoxMax(FMath::Max(StartPos.X, EndPos.X), FMath::Max(StartPos.Y, EndPos.Y));
	const FBox2D SelectionBox(BoxMin, BoxMax);

	for (TActorIterator<AIFSoldierCharacter> It(World); It; ++It)
	{
		AIFSoldierCharacter* const Soldier = *It;
		if (!IsValid(Soldier) || !Soldier->IsAlive() || !IFTeamUtils::AreFriendly(PlayerPawn, Soldier))
		{
			continue;
		}

		FVector2D CenterScreen = FVector2D::ZeroVector;
		FVector2D BaseScreen = FVector2D::ZeroVector;
		const bool bCenterInside = PC->ProjectWorldLocationToScreen(Soldier->GetActorLocation(), CenterScreen, true)
			&& SelectionBox.IsInside(CenterScreen);
		const bool bBaseInside = !bCenterInside
			&& PC->ProjectWorldLocationToScreen(Soldier->GetActorLocation() - FVector(0.f, 0.f, 50.f), BaseScreen, true)
			&& SelectionBox.IsInside(BaseScreen);

		if (bCenterInside || bBaseInside)
		{
			OutSoldiers.Add(Soldier);
		}
	}
}

void FIFTacticalSelectionState::SetHighlightedHostileTarget(
	AIFSoldierCharacter* HostileSoldier,
	TFunctionRef<void(AIFSoldierCharacter*)> BindDeath,
	TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath)
{
	if (HighlightedHostileTarget.Get() == HostileSoldier)
	{
		if (IsValid(HostileSoldier) && HostileSoldier->IsAlive())
		{
			HostileSoldier->SetHostileTargetHighlighted(true);
		}
		return;
	}

	ClearHighlightedHostileTarget(UnbindDeath);

	if (!IsValid(HostileSoldier) || !HostileSoldier->IsAlive())
	{
		return;
	}

	HighlightedHostileTarget = HostileSoldier;
	BindDeath(HostileSoldier);
	HostileSoldier->SetHostileTargetHighlighted(true);
}

void FIFTacticalSelectionState::ClearHighlightedHostileTarget(TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath)
{
	if (AIFSoldierCharacter* const Hostile = HighlightedHostileTarget.Get())
	{
		UnbindDeath(Hostile);
		Hostile->SetHostileTargetHighlighted(false);
	}
	HighlightedHostileTarget.Reset();
}

void FIFTacticalSelectionState::SetHighlightedHostileStructure(
	AIFStructure* HostileStructure,
	TFunctionRef<void(AIFStructure*)> BindDestroyed,
	TFunctionRef<void(AIFStructure*)> UnbindDestroyed)
{
	if (HighlightedHostileStructure.Get() == HostileStructure)
	{
		if (IsValid(HostileStructure) && HostileStructure->IsStructureAlive())
		{
			HostileStructure->SetHostileTargetHighlighted(true);
		}
		return;
	}

	ClearHighlightedHostileStructure(UnbindDestroyed);

	if (!IsValid(HostileStructure) || !HostileStructure->IsStructureAlive())
	{
		return;
	}

	HighlightedHostileStructure = HostileStructure;
	BindDestroyed(HostileStructure);
	HostileStructure->SetHostileTargetHighlighted(true);
}

void FIFTacticalSelectionState::ClearHighlightedHostileStructure(TFunctionRef<void(AIFStructure*)> UnbindDestroyed)
{
	if (AIFStructure* const Structure = HighlightedHostileStructure.Get())
	{
		UnbindDestroyed(Structure);
		Structure->SetHostileTargetHighlighted(false);
	}
	HighlightedHostileStructure.Reset();
}

void FIFTacticalSelectionState::BeginMarqueePress(const FVector2D& ScreenPos)
{
	bIsSelectPressed = true;
	bIsMarqueeDragging = false;
	MarqueeStartScreenPos = ScreenPos;
	MarqueeCurrentScreenPos = ScreenPos;
}

bool FIFTacticalSelectionState::UpdateMarqueeDrag(const FVector2D& ScreenPos, float DragThresholdPixels)
{
	MarqueeCurrentScreenPos = ScreenPos;
	if (!bIsMarqueeDragging && FVector2D::Distance(MarqueeStartScreenPos, MarqueeCurrentScreenPos) >= DragThresholdPixels)
	{
		bIsMarqueeDragging = true;
	}
	return bIsMarqueeDragging;
}

void FIFTacticalSelectionState::CancelMarquee()
{
	bIsSelectPressed = false;
	bIsMarqueeDragging = false;
	MarqueeStartScreenPos = FVector2D::ZeroVector;
	MarqueeCurrentScreenPos = FVector2D::ZeroVector;
}
