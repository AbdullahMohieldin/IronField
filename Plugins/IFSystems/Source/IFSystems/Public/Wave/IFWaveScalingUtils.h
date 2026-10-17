#pragma once

#include "CoreMinimal.h"

namespace IFWaveScalingUtils
{
	/** Computes health multiplier for wave index (0-based: wave 1 = index 0). */
	inline float ComputeHealthScale(int32 WaveIndex, float PerWaveMultiplier = 1.0f)
	{
		if (WaveIndex <= 0 || FMath::IsNearlyEqual(PerWaveMultiplier, 1.0f))
		{
			return 1.0f;
		}
		return FMath::Pow(FMath::Max(0.f, PerWaveMultiplier), static_cast<float>(WaveIndex));
	}

	/** Computes damage multiplier for wave index (0-based: wave 1 = index 0). */
	inline float ComputeDamageScale(int32 WaveIndex, float PerWaveMultiplier = 1.0f)
	{
		if (WaveIndex <= 0 || FMath::IsNearlyEqual(PerWaveMultiplier, 1.0f))
		{
			return 1.0f;
		}
		return FMath::Pow(FMath::Max(0.f, PerWaveMultiplier), static_cast<float>(WaveIndex));
	}

	/** Scales enemy count by exponential factor for unlimited mode. */
	inline int32 ScaleEnemyCount(int32 BaseCount, int32 WaveIndex, float ScaleFactor = 1.15f)
	{
		if (WaveIndex <= 0 || FMath::IsNearlyEqual(ScaleFactor, 1.0f))
		{
			return FMath::Max(1, BaseCount);
		}
		const float Scale = FMath::Pow(FMath::Max(1.0f, ScaleFactor), static_cast<float>(WaveIndex));
		return FMath::Max(1, FMath::RoundToInt(static_cast<float>(BaseCount) * Scale));
	}
}
