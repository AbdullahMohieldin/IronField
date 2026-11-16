#include "Animation/IFAnimNotify_SpinAttackHit.h"

#include "Combat/IFPlayerCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UIFAnimNotify_SpinAttackHit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!IsValid(MeshComp))
	{
		return;
	}

	AActor* const Owner = MeshComp->GetOwner();
	if (!IsValid(Owner))
	{
		return;
	}

	if (UIFPlayerCombatComponent* const Combat = Owner->FindComponentByClass<UIFPlayerCombatComponent>())
	{
		Combat->ExecuteSpinAttackHit();
	}
}
