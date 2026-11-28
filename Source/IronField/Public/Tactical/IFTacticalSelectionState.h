#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class AIFSoldierCharacter;
class AIFStructure;
class APawn;
class APlayerController;
class UWorld;

/**
 * Value-type tactical selection and marquee state owned by UIFTacticalCommandComponent.
 * Stores only TWeakObjectPtr and value types; delegate binding/unbinding is routed through callbacks.
 */
struct IRONFIELD_API FIFTacticalSelectionState
{
	bool ClearSelection(TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath);

	void ReplaceSelection(
		const TArray<AIFSoldierCharacter*>& NewSoldiers,
		bool bShowRingsNow,
		TFunctionRef<void(AIFSoldierCharacter*)> BindDeath,
		TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath);

	bool AddToSelection(
		const TArray<AIFSoldierCharacter*>& SoldiersToAdd,
		bool bShowRingsNow,
		TFunctionRef<void(AIFSoldierCharacter*)> BindDeath);

	bool ToggleSoldierInSelection(
		AIFSoldierCharacter* Soldier,
		bool bShowRingsNow,
		TFunctionRef<void(AIFSoldierCharacter*)> BindDeath,
		TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath);

	bool HandleSelectedSoldierDied(
		AIFSoldierCharacter* DeadSoldier,
		TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath);

	bool PruneInvalidSoldiers(TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath);

	void SetSelectedSoldierRingsVisible(bool bVisible);

	int32 GetSelectedSoldierCount() const;
	bool IsSelectionEmpty() const { return SelectedSoldiers.IsEmpty(); }

	void GatherAliveSelectedSquad(
		TArray<AIFSoldierCharacter*>& OutSquad,
		TArray<FVector>& OutLocations,
		FVector& OutCentroid) const;

	static void GatherSoldiersInMarqueeBox(
		UWorld* World,
		const APlayerController* PC,
		const APawn* PlayerPawn,
		const FVector2D& StartPos,
		const FVector2D& EndPos,
		TArray<AIFSoldierCharacter*>& OutSoldiers);

	void SetHighlightedHostileTarget(
		AIFSoldierCharacter* HostileSoldier,
		TFunctionRef<void(AIFSoldierCharacter*)> BindDeath,
		TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath);

	void ClearHighlightedHostileTarget(TFunctionRef<void(AIFSoldierCharacter*)> UnbindDeath);
	AIFSoldierCharacter* GetHighlightedHostileTarget() const { return HighlightedHostileTarget.Get(); }

	void SetHighlightedHostileStructure(
		AIFStructure* HostileStructure,
		TFunctionRef<void(AIFStructure*)> BindDestroyed,
		TFunctionRef<void(AIFStructure*)> UnbindDestroyed);

	void ClearHighlightedHostileStructure(TFunctionRef<void(AIFStructure*)> UnbindDestroyed);
	AIFStructure* GetHighlightedHostileStructure() const { return HighlightedHostileStructure.Get(); }

	void BeginMarqueePress(const FVector2D& ScreenPos);
	bool UpdateMarqueeDrag(const FVector2D& ScreenPos, float DragThresholdPixels);
	void CancelMarquee();

	bool IsSelectPressed() const { return bIsSelectPressed; }
	bool IsMarqueeDragging() const { return bIsMarqueeDragging; }
	const FVector2D& GetMarqueeStartScreenPos() const { return MarqueeStartScreenPos; }
	const FVector2D& GetMarqueeCurrentScreenPos() const { return MarqueeCurrentScreenPos; }

private:
	TArray<TWeakObjectPtr<AIFSoldierCharacter>> SelectedSoldiers;
	TWeakObjectPtr<AIFSoldierCharacter> HighlightedHostileTarget;
	TWeakObjectPtr<AIFStructure> HighlightedHostileStructure;

	FVector2D MarqueeStartScreenPos = FVector2D::ZeroVector;
	FVector2D MarqueeCurrentScreenPos = FVector2D::ZeroVector;
	bool bIsSelectPressed = false;
	bool bIsMarqueeDragging = false;
};
