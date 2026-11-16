#include "Combat/IFAnimNotifyState_AttackCollision.h"

#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
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

	int32 GetAttackCollisionNotifyInstanceID(const FAnimNotifyEventReference& EventReference)
	{
		const UE::Anim::FAnimNotifyMontageInstanceContext* Context =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		return Context ? Context->MontageInstanceID : INDEX_NONE;
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float, const FAnimNotifyEventReference& EventReference)
{
	UIFMeleeCombatComponent* const Combat = ResolveMeleeCombat(MeshComp);
	if (Combat && GetAttackCollisionNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->OpenHitWindow();
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	UIFMeleeCombatComponent* const Combat = ResolveMeleeCombat(MeshComp);
	if (Combat && GetAttackCollisionNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->UpdateHitWindow(FrameDeltaTime);
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, const FAnimNotifyEventReference& EventReference)
{
	UIFMeleeCombatComponent* const Combat = ResolveMeleeCombat(MeshComp);
	if (Combat && GetAttackCollisionNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->CloseHitWindow();
	}
}