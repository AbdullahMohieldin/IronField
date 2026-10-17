#include "Core/IFFeedbackUtils.h"

#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "Sound/SoundBase.h"

namespace IFFeedbackUtils
{
	void PlayAtLocation(const UWorld* World, USoundBase* Sound, UNiagaraSystem* VFX, const FVector& Location)
	{
		if (!World || (!Sound && !VFX))
		{
			return;
		}

		if (Sound)
		{
			UGameplayStatics::PlaySoundAtLocation(World, Sound, Location);
		}

		if (VFX)
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, VFX, Location);
		}
	}
}
