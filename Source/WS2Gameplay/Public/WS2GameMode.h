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
class AWS2GameState;
class UWS2WaveDirector;
class UWS2WeaponComponent;
class UWS2SaveGame;

UCLASS()
class WS2GAMEPLAY_API AWS2GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWS2GameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void Tick(float DeltaSeconds) override;

	void AddKill(int32 Mat, int32 Xp);
	void AddMaterials(int32 Amount);
	void OnPlayerDown(AWS2PlayerPawn* DownedPawn);
	void OnWaveCleared();
	void OnWaveStarted(int32 Wave, float Duration);
	void RequestShake(const FVector2D& At, float Amount, float Duration);
	void RequestHitStop(float Seconds);
	void OnSliceComplete();
	/** Labels the current frame for the frame-time proof (wave, shop, first-use...). */
	void SetFrameTag(const FString& Tag) { CurrentFrameTag = Tag; }
	void OpenShop(bool bLevelUp);
	void PickShop(int32 Index, AWS2PlayerPawn* Buyer = nullptr);
	void RerollShop();
	bool IsShopOpen() const { return bShopOpen; }
	void AddToast(const FString& Text, float Seconds = 2.4f);
	bool ShouldSpawnBoss() const { return NoBossRequest <= 0; }

	/** Starts the shared wave/boss run. Clients call the server RPC on their controller. */
	void StartRun();
	bool IsRunStarted() const { return bRunStarted; }
	bool IsFullRun() const { return !bProofMode || bForceFullRun; }
	/** Debug-only deterministic co-op acceptance run, enabled with -ws2netverify=1. */
	static bool IsNetVerifyRequested();
	bool IsNetVerifyActive() const { return bNetVerify; }
	void NotifyNetVerifyPickup(AWS2PlayerPawn* Collector, int32 Amount);

	/** Called by the controller when a client selects a hero in the lobby. */
	void SetPlayerHero(AWS2PlayerPawn* Pawn, const FString& HeroId);

	const TArray<AWS2PlayerPawn*>& GetPlayerPawns() const { return PlayerPawns; }
	AWS2PlayerPawn* GetPlayerPawnFor(APlayerController* Controller) const;

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

	/** One shop session with the offers it presented (level-up proof row). */
	struct FShopEvent
	{
		int32 Wave = 0;
		int32 Level = 0;
		bool bLevelUp = false;
		FString OffersJson;
		FString Pick;
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
	void EnsurePlayerForController(APlayerController* Controller);
	FVector SpawnLocationForSlot(int32 Slot) const;
	FString HeroForSlot(int32 Slot) const;
	void UpdateState(float DeltaSeconds);
	void UpdateNetState();
	void CheckAutomation(float RealTime);
	void NetVerifyTick(float RealTime);
	void SpawnNetVerifyTargets();
	void LogPerformance(float RealTime);
	void BuildShopOffers();
	void TrackShopOffers();
	FString DescribeOffersJson() const;
	FString BuildLevelUpsJson() const;
	FString BuildShopPoolProofJson() const;
	void RunWeaponMatrix();
	void ApplyAllItems();
	void ReviveAllPlayers();
	void LoadProfile();
	void CommitProfile();

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

	UPROPERTY()
	AWS2GameState* NetState = nullptr;

	/** Removes roster entries whose pawn no longer exists (client disconnect). */
	void PruneInvalidPlayerPawns();

	TArray<AWS2PlayerPawn*> PlayerPawns;
	TMap<APlayerController*, TWeakObjectPtr<AWS2PlayerPawn>> ControllerPawns;
	TMap<AWS2PlayerPawn*, float> PlayerRespawnTimers;

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

	// Level-up / shop-pool proof state (written to the arena result JSON).
	TArray<FShopEvent> ShopEvents;
	int32 LevelUpCount = 0;
	int32 LevelUpShops = 0;
	TSet<FString> OfferedWeaponIds;
	TSet<FString> OfferedTiers;
	TSet<FString> OfferedItemIds;
	TSet<FString> OfferedStatIds;
	TSet<FString> OfferedElements;

	// Co-op / session state.
	FString StartupOptions;
	FString RequestedHeroId;
	bool bAutostart = false;
	bool bProofMode = false;
	bool bForceFullRun = false;
	bool bWorldReady = false;
	bool bRunStarted = false;
	bool bProfileCommitted = false;
	bool bNetVerify = false;
	int32 NetVerifyRequest = 0;
	float NetVerifyStart = -1.f;
	float NetVerifySpawnTime = -1.f;
	bool bNetVerifySpawned = false;
	bool bNetVerifyPickupSpawned = false;
	bool bNetVerifyReserveSpawned = false;
	bool bNetVerifyWaveShifted = false;
	int32 NextPlayerSlot = 0;

	UPROPERTY()
	UWS2SaveGame* Profile = nullptr;

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
