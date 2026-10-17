#pragma once

#include "CoreMinimal.h"

class USoundBase;
class UNiagaraSystem;

namespace IFFeedbackUtils
{
	/** Guarded one-line sound+VFX play. Empty slots stay silent. */
	IFSYSTEMS_API void PlayAtLocation(const UWorld* World, USoundBase* Sound, UNiagaraSystem* VFX, const FVector& Location);
}
