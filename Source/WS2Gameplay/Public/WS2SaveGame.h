#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "WS2SaveGame.generated.h"

/** Persistent local profile: records survive restarts and are shown in the HUD/lobby. */
UCLASS()
class WS2GAMEPLAY_API UWS2SaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 BestWave = 0;

	UPROPERTY()
	int32 BestKills = 0;

	UPROPERTY()
	int32 Runs = 0;

	UPROPERTY()
	int32 TotalKills = 0;

	UPROPERTY()
	FString BestArena;

	UPROPERTY()
	bool bArenaCleared = false;
};