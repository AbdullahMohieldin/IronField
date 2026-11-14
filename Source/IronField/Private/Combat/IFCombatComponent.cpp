#include "Combat/IFCombatComponent.h"

UIFCombatComponent::UIFCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIFCombatComponent::HandleOwnerDeath()
{
	SetCombatState(EIFCombatState::Dead);
}

void UIFCombatComponent::HandleOwnerRevived()
{
	SetCombatState(EIFCombatState::Idle);
}

void UIFCombatComponent::SetCombatState(EIFCombatState NewState)
{
	if (CombatState == NewState)
	{
		return;
	}

	const EIFCombatState PreviousState = CombatState;
	CombatState = NewState;
	OnCombatStateChanged.Broadcast(PreviousState, NewState);
}