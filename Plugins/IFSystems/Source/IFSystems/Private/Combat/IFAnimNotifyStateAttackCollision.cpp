#include "Combat/IFAnimNotifyStateAttackCollision.h"

#include "Combat/IFCombatComponent.h"
#include "Combat/IFCombatTargetingUtils.h"
#include "Components/SkeletalMeshComponent.h"

void UIFAnimNotifyStateAttackCollision::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (UIFCombatComponent* const Combat = IFCombatTargetingUtils::FindCombatComponent(MeshComp ? MeshComp->GetOwner() : nullptr))
	{
		Combat->BeginAttackCollision();
	}
}

void UIFAnimNotifyStateAttackCollision::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);

	if (UIFCombatComponent* const Combat = IFCombatTargetingUtils::FindCombatComponent(MeshComp ? MeshComp->GetOwner() : nullptr))
	{
		Combat->EndAttackCollision();
	}
}
