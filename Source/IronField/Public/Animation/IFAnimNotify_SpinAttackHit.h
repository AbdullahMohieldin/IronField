#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "IFAnimNotify_SpinAttackHit.generated.h"

UCLASS(meta = (DisplayName = "IF Spin Attack Hit"))
class IRONFIELD_API UIFAnimNotify_SpinAttackHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};
