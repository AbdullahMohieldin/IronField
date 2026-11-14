#include "Core/IFGameInstance.h"

#include "Core/IFLog.h"
#include "Kismet/GameplayStatics.h"

void UIFGameInstance::SetRunMode(EIFRunMode NewMode)
{
	RunMode = NewMode;
}

void UIFGameInstance::StartGameRun(EIFRunMode Mode)
{
	SetRunMode(Mode);
	UE_LOG(LogIronField, Log, TEXT("[IF-Flow] Starting run (mode=%d), travelling to %s."), static_cast<int32>(Mode), *GameplayLevelName.ToString());
	UGameplayStatics::OpenLevel(this, GameplayLevelName);
}