#pragma once

#include "CoreMinimal.h"

/** Snapshot both the simulation and the Slate HUD read from. */
struct FWS2RunState
{
	float Hp = 100.f;
	float MaxHp = 100.f;
	float HpRegen = 0.f;

	int32 Wave = 1;
	float WaveTimeLeft = 0.f;
	float WaveDuration = 20.f;
	bool bWaveActive = false;

	int32 Kills = 0;
	int32 Materials = 0;
	int32 Level = 1;
	int32 Xp = 0;
	int32 XpNext = 6;

	int32 AliveEnemies = 0;
	int32 PeakEnemies = 0;
	int32 Projectiles = 0;
	bool bBossAlive = false;
	bool bPlayerDown = false;

	float AvgFps = 0.f;
	float MinFps = 999.f;

	FString ArenaName = TEXT("KURPARK");
	FString WeaponName = TEXT("Dienstpistole");
	FString Status = TEXT("");
	FString HeroName = TEXT("Leonidas");

	// Shop / level-up flow -------------------------------------------------
	bool bShopOpen = false;
	bool bLevelUp = false;
	TArray<FString> ShopOffers;
	FString ShopHint = TEXT("");
	FString Toast = TEXT("");
	float ToastTime = 0.f;

	// Boss framing ----------------------------------------------------------
	FString BossName = TEXT("");
	float BossHpPct = 0.f;
	int32 BossPhase = 0;
	FString ArenaMods = TEXT("");
};
