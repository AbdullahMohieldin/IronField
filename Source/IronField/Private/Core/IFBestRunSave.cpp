#include "Core/IFBestRunSave.h"

FText UIFBestRunSave::BuildBestText() const
{
	const FString NormalStr = BestNormalWave > 0
		? FString::Printf(TEXT("W%d K%d"), BestNormalWave, BestNormalKills)
		: TEXT("\u2014");

	const FString UnlimitedStr = BestUnlimitedWave > 0
		? FString::Printf(TEXT("W%d K%d"), BestUnlimitedWave, BestUnlimitedKills)
		: TEXT("\u2014");

	return FText::FromString(FString::Printf(TEXT("NORMAL BEST: %s  \u2022  UNLIMITED BEST: %s"), *NormalStr, *UnlimitedStr));
}
