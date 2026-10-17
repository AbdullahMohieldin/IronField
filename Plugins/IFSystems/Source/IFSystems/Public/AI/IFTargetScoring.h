#pragma once

#include "CoreMinimal.h"

enum class EIFTargetCandidate : uint8
{
	None,
	Player,
	Stronghold
};

/**
 * Pure input payload for target scoring with zero UObject dependencies.
 */
struct FIFTargetScoreInput
{
	bool bPlayerValid = false;
	bool bStrongholdValid = false;
	bool bCurrentValid = false;

	float PlayerDistance = 0.f;
	float StrongholdDistance = 0.f;

	float Aggression = 0.5f;
	bool bHasGrudge = false;
	float TimeSinceLastSwitch = 0.f;
	EIFTargetCandidate CurrentTarget = EIFTargetCandidate::None;

	float DistanceFalloffRange = 800.f;
	float PlayerWeightBase = 0.5f;
	float StrongholdWeightBase = 1.5f;
	float RetaliationBonus = 3.f;
	float CommitScoreBonus = 1.6f;
	float SwitchThreshold = 1.35f;
	float CommitLockSeconds = 3.f;
};

struct FIFTargetScoreResult
{
	EIFTargetCandidate DesiredTarget = EIFTargetCandidate::None;
	float PlayerRawScore = 0.f;
	float StrongholdRawScore = 0.f;
	float ChampionScore = 0.f;
	bool bTargetSwitched = false;
};

namespace FIFTargetScoring
{
	inline float DistanceDesire(float Distance, float FalloffRange)
	{
		if (FalloffRange <= 0.f)
		{
			return 1.f;
		}
		return 1.f / (1.f + FMath::Max(0.f, Distance) / FalloffRange);
	}

	inline FIFTargetScoreResult ScoreTarget(const FIFTargetScoreInput& Input)
	{
		FIFTargetScoreResult Result;

		if (Input.bPlayerValid)
		{
			Result.PlayerRawScore = DistanceDesire(Input.PlayerDistance, Input.DistanceFalloffRange)
				* (Input.PlayerWeightBase + Input.Aggression);

			if (Input.bHasGrudge)
			{
				Result.PlayerRawScore *= Input.RetaliationBonus;
			}
		}

		if (Input.bStrongholdValid)
		{
			Result.StrongholdRawScore = DistanceDesire(Input.StrongholdDistance, Input.DistanceFalloffRange)
				* (Input.StrongholdWeightBase - Input.Aggression);
		}

		EIFTargetCandidate Champion = EIFTargetCandidate::None;
		float ChampionScore = 0.f;

		if (Result.PlayerRawScore > ChampionScore)
		{
			Champion = EIFTargetCandidate::Player;
			ChampionScore = Result.PlayerRawScore;
		}
		if (Result.StrongholdRawScore > ChampionScore)
		{
			Champion = EIFTargetCandidate::Stronghold;
			ChampionScore = Result.StrongholdRawScore;
		}

		Result.ChampionScore = ChampionScore;
		EIFTargetCandidate Desired = Input.CurrentTarget;

		if (Champion != Input.CurrentTarget)
		{
			float CurrentScore = 0.f;
			if (Input.bCurrentValid)
			{
				const float BaseScore = (Input.CurrentTarget == EIFTargetCandidate::Player) ? Result.PlayerRawScore : Result.StrongholdRawScore;
				CurrentScore = BaseScore * Input.CommitScoreBonus;
			}

			const bool bLocked = Input.bCurrentValid && !Input.bHasGrudge && (Input.TimeSinceLastSwitch < Input.CommitLockSeconds);
			if (!bLocked && ChampionScore > CurrentScore * Input.SwitchThreshold)
			{
				Desired = Champion;
			}
		}

		if (Champion == EIFTargetCandidate::None && !Input.bCurrentValid)
		{
			Desired = EIFTargetCandidate::None;
		}

		Result.DesiredTarget = Desired;
		Result.bTargetSwitched = (Desired != Input.CurrentTarget);
		return Result;
	}
}
