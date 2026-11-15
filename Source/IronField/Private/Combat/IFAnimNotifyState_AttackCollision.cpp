#include "Combat/IFAnimNotifyState_AttackCollision.h"

#include "Character/IFBaseCharacter.h"
#include "Combat/IFMeleeCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	UIFMeleeCombatComponent* ResolveMeleeCombat(USkeletalMeshComponent* MeshComp)
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

		return Cast<UIFMeleeCombatComponent>(Character->GetCombatComponent());
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float, const FAnimNotifyEventReference&)
{
	if (UIFMeleeCombatComponent* const Combat = ResolveMeleeCombat(MeshComp))
	{
		Combat->OpenHitWindow();
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float FrameDeltaTime, const FAnimNotifyEventReference&)
{
	if (UIFMeleeCombatComponent* const Combat = ResolveMeleeCombat(MeshComp))
	{
		Combat->UpdateHitWindow(FrameDeltaTime);
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, const FAnimNotifyEventReference&)
{
	if (UIFMeleeCombatComponent* const Combat = ResolveMeleeCombat(MeshComp))
	{
		Combat->CloseHitWindow();
	}
}