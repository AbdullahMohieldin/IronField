#include "Combat/IFAnimNotifyLaunchProjectile.h"

#include "Combat/IFCombatComponent.h"
#include "Combat/IFCombatTargetingUtils.h"
#include "Components/SkeletalMeshComponent.h"

void UIFAnimNotifyLaunchProjectile::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (UIFCombatComponent* const Combat = IFCombatTargetingUtils::FindCombatComponent(MeshComp ? MeshComp->GetOwner() : nullptr))
	{
		Combat->LaunchProjectileAttack();
	}
}
