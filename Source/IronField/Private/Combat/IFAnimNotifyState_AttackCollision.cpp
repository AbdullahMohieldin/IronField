#include "Combat/IFAnimNotifyState_AttackCollision.h"

#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	UIFCombatComponent* ResolveCombatComponent(USkeletalMeshComponent* MeshComp)
	{
		if (!IsValid(MeshComp))
		{
			return nullptr;
		}

		const AIFBaseCharacter* const Character = Cast<AIFBaseCharacter>(MeshComp->GetOwner());
		return IsValid(Character) ? Character->GetCombatComponent() : nullptr;
	}

	int32 GetAttackCollisionNotifyInstanceID(const FAnimNotifyEventReference& EventReference)
	{
		// Blending-out montages still fire NotifyEnd after a combo cancel, so match the active instance ID.
		const UE::Anim::FAnimNotifyMontageInstanceContext* Context =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		return Context ? Context->MontageInstanceID : INDEX_NONE;
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float, const FAnimNotifyEventReference& EventReference)
{
	UIFCombatComponent* const Combat = ResolveCombatComponent(MeshComp);
	if (Combat && GetAttackCollisionNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->OpenHitWindow();
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, float FrameDeltaTime, const FAnimNotifyEventReference& EventReference)
{
	UIFCombatComponent* const Combat = ResolveCombatComponent(MeshComp);
	if (Combat && GetAttackCollisionNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->UpdateHitWindow(FrameDeltaTime);
	}
}

void UIFAnimNotifyState_AttackCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, const FAnimNotifyEventReference& EventReference)
{
	UIFCombatComponent* const Combat = ResolveCombatComponent(MeshComp);
	if (Combat && GetAttackCollisionNotifyInstanceID(EventReference) == Combat->GetCurrentMontageInstanceID())
	{
		Combat->CloseHitWindow();
	}
}