#include "Animation/IFAnimNotify_SpinAttackHit.h"

#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyQueue.h"
#include "Combat/IFPlayerCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	void ExecuteIfOwned(USkeletalMeshComponent* MeshComp, int32 InstanceID)
	{
		if (!IsValid(MeshComp))
		{
			return;
		}

		AActor* const Owner = MeshComp->GetOwner();
		if (!IsValid(Owner))
		{
			return;
		}

		UIFPlayerCombatComponent* const Combat = Owner->FindComponentByClass<UIFPlayerCombatComponent>();
		if (Combat && InstanceID == Combat->GetCurrentMontageInstanceID() && Combat->IsAttacking())
		{
			Combat->ExecuteSpinAttackHit();
		}
	}
}

void UIFAnimNotify_SpinAttackHit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase*, const FAnimNotifyEventReference& EventReference)
{
	const UE::Anim::FAnimNotifyMontageInstanceContext* Context =
		EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
	ExecuteIfOwned(MeshComp, Context ? Context->MontageInstanceID : INDEX_NONE);
}

void UIFAnimNotify_SpinAttackHit::BranchingPointNotify(FBranchingPointNotifyPayload& BranchingPointPayload)
{
	ExecuteIfOwned(BranchingPointPayload.SkelMeshComponent, BranchingPointPayload.MontageInstanceID);
}