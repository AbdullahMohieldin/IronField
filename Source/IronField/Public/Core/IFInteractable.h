#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IFInteractable.generated.h"

class AActor;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UIFInteractable : public UInterface
{
	GENERATED_BODY()
};

class IRONFIELD_API IIFInteractable
{
	GENERATED_BODY()

public:
	virtual bool CanInteract(AActor* Interactor) const = 0;
	virtual void Interact(AActor* Interactor) = 0;
	virtual FText GetInteractPromptText(AActor* Interactor) const = 0;
};
