#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WS2Data.h"
#include "WS2RunState.h"
#include "WS2GameMode.generated.h"

class AWS2Arena;
class AWS2Crowd;
class AWS2ArenaCamera;
class AWS2PlayerPawn;
class UWS2WaveDirector;
class UWS2WeaponComponent;

UCLASS()
class WS2GAMEPLAY_API AWS2GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWS2GameMode();

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void AddKill(int32 Mat, int32 Xp);
	void AddMaterials(int32 Amount);
	void OnPlayerDown();
	void OnWaveCleared();
	void OnWaveStarted(int32 Wave, float Duration);
	void RequestShake(const FVector2D& At, float Amount, float Duration);
	void RequestHitStop(float Seconds);
	void OnSliceComplete();
	/** Labels the current frame for the frame-time proof (wave, shop, first-use...). */
	void SetFrameTag(const FString& Tag) { CurrentFrameTag = Tag; }
	void OpenShop(bool bLevelUp);
	void PickShop(int32 Index);
	void RerollShop();
	bool IsShopOpen() const { return bShopOpen; }
	void AddToast(const FString& Text, float Seconds = 2.4f);
	bool ShouldSpawnBoss() const { return NoBossRequest <= 0; }

	struct FWS2Offer
	{
		FString Kind = TEXT("stat");
		FString Id;
		FString Label;
		FString Stat;
		FString StatsText;
		float Amount = 0.f;
		int32 Tier = 0;
		int32 Cost = 0;
	};

	const FWS2RunState& GetState() const { return State; }
	TSharedPtr<FWS2RunState> GetHudState() const { return HudState; }
	AWS2Arena* GetArena() const { return Arena; }
	AWS2Crowd* GetCrowd() const { return Crowd; }
	AWS2ArenaCamera* GetArenaCamera() const { return Camera; }
	UWS2WaveDirector* GetDirector() const { return Director; }
	AWS2PlayerPawn* GetPlayerPawn() const { return PlayerPawn; }

private:
	void SpawnWorld();
	void UpdateState(float DeltaSeconds);
	void CheckAutomation(float RealTime);
	void LogPerformance(float RealTime);
	void BuildShopOffers();
	void RunWeaponMatrix();
	void ApplyAllItems();

	UPROPERTY()
	AWS2Arena* Arena = nullptr;

	UPROPERTY()
	AWS2Crowd* Crowd = nullptr;

	UPROPERTY()
	AWS2ArenaCamera* Camera = nullptr;

	UPROPERTY()
	UWS2WaveDirector* Director = nullptr;

	UPROPERTY()
	AWS2PlayerPawn* PlayerPawn = nullptr;

	TSharedPtr<FWS2RunState> HudState;
	FWS2RunState State;

	float RealStartTime = 0.f;
	float LastRealSeconds = 0.f;
	float StatsAccum = 0.f;
	int32 StatsFrames = 0;
	float NextLogAt = 5.f;
	float NextShotAt = 8.f;
	bool bShotTaken = false;
	bool bShot2Taken = false;
	bool bBossShotTaken = false;
	bool bNoScreenshots = false;
	bool bBossWasAlive = false;
	float BossShotTimer = -1.f;
	float WaveClearTimer = 0.f;
	float RespawnTimer = 0.f;
	float HitStopLeft = 0.f;
	bool bClockStarted = false;
	int32 PerfRequest = 0;
	int32 InvulnRequest = 0;

	TArray<FWS2Offer> ShopOffers;
	bool bShopOpen = false;
	bool bLevelUpShop = false;
	float ShopTimer = 0.f;
	float SliceTimer = 0.f;
	bool bSliceDone = false;
	int32 HitStopCount = 0;
	int32 AutoRequest = 0;
	int32 ShotRequest = 0;
	int32 EliteRequest = 0;
	int32 NoBossRequest = 0;
	int32 PendingLevelUps = 0;
	int32 QuitRequest = 0;
	FString ShopPicks = TEXT("");
	int32 RerollCount = 0;
	int32 ItemsApplied = 0;
	int32 WeaponTiersFired = 0;

	// Content proof switches.
	int32 AllTypesRequest = 0;
	int32 BossProofRequest = 0;
	int32 ArmsRequest = 0;
	int32 ItemsRequest = 0;
	FString ScreenshotName;
	FString ArenaId;
	int32 ShopPoolWeapons = 0;
	int32 ShopPoolTiers = 0;
	int32 ShopPoolItems = 0;
	int32 ShopPoolStats = 0;
	bool bPoolLogged = false;

	void WriteSliceResult();
	void WriteArenaResult();

	struct FFrameSample
	{
		float Time = 0.f;
		float Ms = 0.f;
		FString Tag;
	};

	/** Percentiles use the post-startup distribution; hitches keep their tag. */
	FString BuildFrameStatsJson(float& OutP99Fps, float& OutWorstWindowFps,
		float& OutStartupMaxMs, float& OutSustainedMaxMs, bool& bOutPass) const;

	TArray<FFrameSample> FrameSamples;
	double LastFrameClock = 0.0;
	FString CurrentFrameTag = TEXT("startup");
};
