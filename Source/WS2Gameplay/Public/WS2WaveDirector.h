#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WS2Data.h"
#include "WS2Math.h"
#include "WS2WaveDirector.generated.h"

class AWS2GameMode;

/**
 * Wave pacing: 20-40 s waves, weighted enemy mix gated by minW, rising spawn
 * rate.  Each arena ends on its own boss wave(s); arenas with two assigned
 * bosses get one extra wave so both three-phase tables are played.
 */
UCLASS(ClassGroup = (WS2), meta = (BlueprintSpawnableComponent))
class WS2GAMEPLAY_API UWS2WaveDirector : public UActorComponent
{
	GENERATED_BODY()

public:
	UWS2WaveDirector();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void StartSlice(bool bInAllTypes = false, float InBossHpScale = 1.f, float InProofBossHp = 0.f,
		bool bInShortRun = true, bool bInSpawnBoss = true);
	void StartWave(int32 InWave);
	void SpawnPerfHorde(int32 Count);

	static constexpr int32 SliceWaves = 5;
	static constexpr int32 FullWaves = 20;

	int32 GetWave() const { return Wave; }
	int32 GetMaxWave() const { return MaxWaves; }
	bool IsSliceComplete() const { return bSliceComplete; }
	float GetWaveTimeLeft() const { return FMath::Max(0.f, WaveTimer); }
	float GetWaveDuration() const { return WaveDuration; }
	bool IsWaveActive() const { return bActive; }
	bool IsBossWave() const { return bBossWave; }
	int32 GetQueuedCount() const { return Queue.Num(); }
	int32 GetArenaBossCount() const { return BossDefIndices.Num(); }

private:
	void BuildSpawnQueue();
	int32 PickEnemyDefIndex();
	void SpawnRoster();
	void SpawnArenaBoss(int32 BossOrdinal);

	AWS2GameMode* GameMode = nullptr;
	FWS2Rng Rng;

	int32 Wave = 1;
	int32 MaxWaves = SliceWaves;
	float WaveTimer = 0.f;
	float WaveDuration = 20.f;
	float SpawnTimer = 0.f;
	bool bActive = false;
	bool bSliceComplete = false;
	bool bBossWave = false;
	bool bBossSpawned = false;
	bool bBossAlive = false;
	bool bAllTypes = false;
	bool bShortRun = true;
	/** -ws2noboss=1 keeps the arena wave clock but leaves the bosses out. */
	bool bSpawnBoss = true;
	int32 BossFirstWave = SliceWaves;
	float BossHpScale = 1.f;
	float ProofBossHp = 0.f;
	TArray<int32> BossDefIndices;
	TArray<int32> Queue;
};
