#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "IFAnimNotifyState_ComboWindow.generated.h"

// Stateless: placed over the recovery phase; opening it consumes any buffered attack
// immediately, cutting the recovery tail instead of waiting for the montage to end.
UCLASS(meta = (DisplayName = "IF Combo Window"))
class IRONFIELD_API UIFAnimNotifyState_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};