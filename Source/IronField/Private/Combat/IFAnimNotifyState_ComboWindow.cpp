#include "Combat/IFAnimNotifyState_ComboWindow.h"

#include "Character/IFBaseCharacter.h"
#include "Combat/IFPlayerCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	UIFPlayerCombatComponent* ResolvePlayerCombat(USkeletalMeshComponent* MeshComp)
	{
		if (!IsValid(MeshComp))
		{
			return nullptr;
		}

		const AIFBaseCharacter* const Character = Cast<AIFBaseCharacter>(MeshComp->GetOwner());
		if (!IsValid(Character))
		{
			return nullptr;
		}

		return Cast<UIFPlayerCombatComponent>(Character->GetCombatComponent());
	}
}

void UIFAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float, const FAnimNotifyEventReference&)
{
	if (UIFPlayerCombatComponent* const Combat = ResolvePlayerCombat(MeshComp))
	{
		Combat->OpenComboWindow();
	}
}

void UIFAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, const FAnimNotifyEventReference&)
{
	if (UIFPlayerCombatComponent* const Combat = ResolvePlayerCombat(MeshComp))
	{
		Combat->CloseComboWindow();
	}
}