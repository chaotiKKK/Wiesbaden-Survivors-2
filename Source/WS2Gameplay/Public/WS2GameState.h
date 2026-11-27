#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "WS2RunState.h"
#include "WS2GameState.generated.h"

/** One replicated co-op player row. */
USTRUCT()
struct FWS2CoopPlayer
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Slot = 0;

	UPROPERTY()
	FString PlayerName;

	UPROPERTY()
	FString HeroId;

	UPROPERTY()
	FString HeroName;

	UPROPERTY()
	FString WeaponName;

	UPROPERTY()
	float Hp = 100.f;

	UPROPERTY()
	float MaxHp = 100.f;

	UPROPERTY()
	int32 Level = 1;

	UPROPERTY()
	int32 Xp = 0;

	UPROPERTY()
	int32 XpNext = 6;

	UPROPERTY()
	int32 Kills = 0;

	UPROPERTY()
	bool bDown = false;
};

/**
 * Replicated view of the shared run. The game mode remains the authority; every
 * client builds its HUD from this snapshot, so joining mid-run needs no game mode
 * access and no custom client-side simulation.
 */
UCLASS()
class WS2GAMEPLAY_API AWS2GameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AWS2GameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Builds the exact HUD snapshot for one player slot. */
	void BuildHudState(int32 InSlot, FWS2RunState& OutState) const;
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastSfx(uint8 Sfx, FVector Location);

	int32 NumPlayers() const { return Players.Num(); }

	UPROPERTY(Replicated)
	bool bRunStarted = false;

	UPROPERTY(Replicated)
	int32 Wave = 1;

	UPROPERTY(Replicated)
	float WaveTimeLeft = 0.f;

	UPROPERTY(Replicated)
	float WaveDuration = 20.f;

	UPROPERTY(Replicated)
	bool bWaveActive = false;

	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 Materials = 0;

	UPROPERTY(Replicated)
	int32 AliveEnemies = 0;

	UPROPERTY(Replicated)
	int32 PeakEnemies = 0;

	UPROPERTY(Replicated)
	int32 Projectiles = 0;

	UPROPERTY(Replicated)
	int32 PickupsOnFloor = 0;

	UPROPERTY(Replicated)
	float AvgFps = 0.f;

	UPROPERTY(Replicated)
	bool bBossAlive = false;

	UPROPERTY(Replicated)
	FString BossName;

	UPROPERTY(Replicated)
	float BossHpPct = 0.f;

	UPROPERTY(Replicated)
	int32 BossPhase = 0;

	UPROPERTY(Replicated)
	FVector2D BossWorldPos = FVector2D::ZeroVector;

	UPROPERTY(Replicated)
	FString ArenaName = TEXT("KURPARK");

	UPROPERTY(Replicated)
	FString ArenaId = TEXT("kurpark");

	UPROPERTY(Replicated)
	FString ArenaMods;

	UPROPERTY(Replicated)
	FVector2D ArenaExtent = FVector2D(1900.f, 1500.f);

	UPROPERTY(Replicated)
	FString Status;

	UPROPERTY(Replicated)
	FString Toast;

	UPROPERTY(Replicated)
	float ToastTime = 0.f;

	UPROPERTY(Replicated)
	bool bShopOpen = false;

	UPROPERTY(Replicated)
	bool bLevelUp = false;

	UPROPERTY(Replicated)
	TArray<FString> ShopOffers;

	UPROPERTY(Replicated)
	TArray<FWS2CoopPlayer> Players;

	UPROPERTY(Replicated)
	int32 BestWave = 0;

	UPROPERTY(Replicated)
	int32 BestKills = 0;

	UPROPERTY(Replicated)
	int32 TotalRuns = 0;
};
