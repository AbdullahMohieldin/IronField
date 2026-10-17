#pragma once

#include "CoreMinimal.h"

namespace IFStatUtils
{
	inline float ComputePercent(float Current, float Max)
	{
		return Max > 0.f ? Current / Max : 0.f;
	}

	/** Clamps Current into [0, Max]. Returns true if the value changed. */
	inline bool ApplyClampedValue(float& Current, float Max, float NewValue)
	{
		const float Previous = Current;
		Current = FMath::Clamp(NewValue, 0.f, Max);
		return !FMath::IsNearlyEqual(Current, Previous);
	}
}
