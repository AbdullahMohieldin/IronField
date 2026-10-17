#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"

class UAnimMontage;

namespace IFAnimMontageUtils
{
	inline void ClearMontageEndDelegate(UAnimInstance* AnimInstance, UAnimMontage* Montage)
	{
		if (!AnimInstance || !Montage)
		{
			return;
		}

		FOnMontageEnded EmptyDelegate;
		AnimInstance->Montage_SetEndDelegate(EmptyDelegate, Montage);
	}
}
