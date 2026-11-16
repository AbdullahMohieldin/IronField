#include "Combat/IFAnimNotifyState_ComboWindow.h"

#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
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

	int32 GetComboWindowNotifyInstanceID(const FAnimNotifyEventReference& EventReference)
	{
		const UE::Anim::FAnimNotifyMontageInstanceContext* Context =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		return Context ? Context->MontageInstanceID : INDEX_NONE;
	}
}

void UIFAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float, const FAnimNotifyEventReference& EventReference)
{
	UIFPlayerCombatComponent* const Combat = ResolvePlayerCombat(MeshComp);
	if (Combat && GetComboWindowNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->OpenComboWindow();
	}
}

void UIFAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, const FAnimNotifyEventReference& EventReference)
{
	UIFPlayerCombatComponent* const Combat = ResolvePlayerCombat(MeshComp);
	if (Combat && GetComboWindowNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->CloseComboWindow();
	}
}