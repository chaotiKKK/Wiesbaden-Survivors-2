#include "WS2WaveDirector.h"

#include "WS2Arena.h"
#include "WS2Crowd.h"
#include "WS2Data.h"
#include "WS2GameMode.h"
#include "WS2PlayerPawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Wave, Log, All);

namespace
{
	constexpr int32 MaxAliveEnemies = 320;
}

UWS2WaveDirector::UWS2WaveDirector()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UWS2WaveDirector::StartSlice(bool bInAllTypes, float InBossHpScale, float InProofBossHp, bool bInShortRun,
	bool bInSpawnBoss)
{
	GameMode = Cast<AWS2GameMode>(GetOwner());
	bAllTypes = bInAllTypes;
	bShortRun = bInShortRun;
	bSpawnBoss = bInSpawnBoss;
	BossHpScale = FMath::Clamp(InBossHpScale, 0.0005f, 1.f);
	ProofBossHp = FMath::Max(0.f, InProofBossHp);
	Rng = FWS2Rng(1984ull);

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const FString ArenaId = GameMode && GameMode->GetArena() ? GameMode->GetArena()->GetDef().Id : FString(TEXT("kurpark"));
	BossDefIndices.Reset();
	for (int32 Index = 0; Index < Db.Bosses.Num(); ++Index)
	{
		if (Db.Bosses[Index].Arena == ArenaId)
		{
			BossDefIndices.Add(Index);
		}
	}
	// Proof runs keep the five-wave slice. A normal run is the full 20-wave
	// arena campaign; the assigned bosses close it out in their phase order.
	MaxWaves = bShortRun ? FMath::Max(SliceWaves, 4 + BossDefIndices.Num()) : FullWaves;
	BossFirstWave = FMath::Max(1, MaxWaves - BossDefIndices.Num() + 1);
	bSliceComplete = false;
	bBossWave = false;
	bBossSpawned = false;

	UE_LOG(LogWS2Wave, Display, TEXT("Arena %s: %d boss(es), %d waves%s%s%s"),
		*ArenaId, BossDefIndices.Num(), MaxWaves,
		bShortRun ? TEXT(", short proof") : TEXT(", full run"),
		bAllTypes ? TEXT(", roster proof") : TEXT(""),
		bSpawnBoss ? TEXT("") : TEXT(", bosses off"));

	StartWave(1);

	if (bAllTypes && GameMode)
	{
		SpawnRoster();
	}
}

void UWS2WaveDirector::SpawnRoster()
{
	AWS2Crowd* Crowd = GameMode ? GameMode->GetCrowd() : nullptr;
	AWS2Arena* Arena = GameMode ? GameMode->GetArena() : nullptr;
	if (!Crowd || !Arena)
	{
		return;
	}
	// A ring slightly inside weapon range: every one of the 28 types actually
	// enters the fight instead of idling at the far wall during a short proof run.
	if (GameMode) GameMode->SetFrameTag(TEXT("roster"));
	const FVector2D PlayerPos = GameMode->GetPlayerPawn() ? GameMode->GetPlayerPawn()->GetArenaPos() : Arena->GetCenter();
	const int32 Spawned = Crowd->SpawnRoster(PlayerPos, 360.f);
	UE_LOG(LogWS2Wave, Display, TEXT("ROSTER proof spawned %d enemy types (arena=%s)"),
		Spawned, *Arena->GetDef().Id);
}

void UWS2WaveDirector::StartWave(int32 InWave)
{
	GameMode = Cast<AWS2GameMode>(GetOwner());
	Wave = FMath::Max(1, InWave);
	if (Wave > MaxWaves)
	{
		bActive = false;
		bSliceComplete = true;
		Queue.Reset();
		bBossWave = false;
		UE_LOG(LogWS2Wave, Display, TEXT("Slice complete: no wave beyond %d"), MaxWaves);
		return;
	}

	bBossWave = bSpawnBoss && Wave >= BossFirstWave && (Wave - BossFirstWave) < BossDefIndices.Num();
	bBossSpawned = false;
	bBossAlive = false;

	// Debug pacing: -ws2fast=N shortens every wave by a factor of N so a full
	// arena proof fits into one short session.  Boss waves ignore the clock and
	// end when the boss dies.
	static float PaceScale = -1.f;
	if (PaceScale < 0.f)
	{
		PaceScale = 1.f;
		float FastValue = 0.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("ws2fast="), FastValue) && FastValue > 0.f)
		{
			PaceScale = FMath::Clamp(1.f / FastValue, 0.1f, 1.f);
		}
	}
	WaveDuration = FMath::Clamp(20.f + Wave, 20.f, 40.f) * PaceScale;
	WaveTimer = WaveDuration;
	SpawnTimer = 0.35f;
	bActive = true;
	Queue.Reset();
	BuildSpawnQueue();

	if (GameMode)
	{
		GameMode->OnWaveStarted(Wave, WaveDuration);
	}

	UE_LOG(LogWS2Wave, Display, TEXT("Wave %d start: %.1fs, %d enemies queued, bossWave=%d"),
		Wave, WaveDuration, Queue.Num(), bBossWave ? 1 : 0);

	if (GameMode)
	{
		GameMode->SetFrameTag(FString::Printf(TEXT("wave-%d"), Wave));
	}

	if (bBossWave)
	{
		SpawnArenaBoss(Wave - BossFirstWave);
	}
}

void UWS2WaveDirector::SpawnArenaBoss(int32 BossOrdinal)
{
	AWS2Crowd* Crowd = GameMode ? GameMode->GetCrowd() : nullptr;
	AWS2Arena* Arena = GameMode ? GameMode->GetArena() : nullptr;
	AWS2PlayerPawn* Player = GameMode ? GameMode->GetPlayerPawn() : nullptr;
	if (!Crowd || !Arena || !BossDefIndices.IsValidIndex(BossOrdinal))
	{
		return;
	}
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const WS2::FBossDef& Boss = Db.Bosses[BossDefIndices[BossOrdinal]];

	const FVector2D Center = Arena->GetCenter();
	const FVector2D PlayerPos = Player ? Player->GetArenaPos() : Center;
	FVector2D Pos = Center;
	if (FVector2D::Distance(Center, PlayerPos) < 420.f)
	{
		Pos = Center + FVector2D(0.f, -420.f);
	}
	Arena->ResolveCircle(Pos, Boss.Radius);
	if (GameMode) GameMode->SetFrameTag(TEXT("boss-spawn"));
	const float UseScale = ProofBossHp > 0.f ? ProofBossHp / FMath::Max(1.f, Boss.Hp) : BossHpScale;
	Crowd->SpawnBoss(Boss, Pos, UseScale);
	bBossSpawned = true;
	bBossAlive = true;
	UE_LOG(LogWS2Wave, Display, TEXT("BOSS WAVE %d: %s enters at (%.0f,%.0f) hpScale=%.3f"),
		Wave, *Boss.Name, Pos.X, Pos.Y, BossHpScale);
}

int32 UWS2WaveDirector::PickEnemyDefIndex()
{
	const WS2::FDatabase& Db = WS2::FDatabase::Get();
	float TotalWeight = 0.f;
	for (const WS2::FEnemyDef& Enemy : Db.Enemies)
	{
		if ((bAllTypes || Enemy.MinWave <= Wave) && !Enemy.Id.Contains(TEXT("shardling")))
		{
			TotalWeight += FMath::Max(0.1f, Enemy.Weight);
		}
	}
	if (TotalWeight <= 0.f)
	{
		return Db.Enemies.Num() > 0 ? 0 : INDEX_NONE;
	}

	float Roll = Rng.Range(0.f, TotalWeight);
	for (int32 Index = 0; Index < Db.Enemies.Num(); ++Index)
	{
		const WS2::FEnemyDef& Enemy = Db.Enemies[Index];
		if ((!bAllTypes && Enemy.MinWave > Wave) || Enemy.Id.Contains(TEXT("shardling")))
		{
			continue;
		}
		Roll -= FMath::Max(0.1f, Enemy.Weight);
		if (Roll <= 0.f)
		{
			return Index;
		}
	}
	return 0;
}

void UWS2WaveDirector::BuildSpawnQueue()
{
	const int32 Count = 10 + Wave * 7;
	Queue.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 DefIndex = PickEnemyDefIndex();
		if (DefIndex != INDEX_NONE)
		{
			const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(DefIndex);
			const int32 PackSize = (Def.Id == TEXT("swarm") || Def.Id == TEXT("runner")) ? Rng.RangeI(1, 2) : 1;
			for (int32 p = 0; p < PackSize; ++p)
			{
				Queue.Add(DefIndex);
			}
		}
	}
}

void UWS2WaveDirector::SpawnPerfHorde(int32 Count)
{
	GameMode = Cast<AWS2GameMode>(GetOwner());
	if (!GameMode)
	{
		return;
	}

	AWS2Crowd* Crowd = GameMode->GetCrowd();
	AWS2Arena* Arena = GameMode->GetArena();
	if (!Crowd || !Arena)
	{
		return;
	}

	const TCHAR* Ids[] = { TEXT("runner"), TEXT("swarm"), TEXT("walker"), TEXT("tank") };
	const float Ring = FMath::Min(Arena->GetExtent().X, Arena->GetExtent().Y) * 0.42f;
	const float GoldenAngle = PI * (3.f - FMath::Sqrt(5.f));
	const FVector2D Center = Arena->GetCenter();
	for (int32 i = 0; i < Count; ++i)
	{
		const WS2::FEnemyDef* Def = WS2::FDatabase::Get().FindEnemy(Ids[i % 4]);
		if (!Def)
		{
			continue;
		}
		const float Angle = i * GoldenAngle + Rng.Range(0.f, 0.35f);
		const float Radius = Ring * FMath::Sqrt((static_cast<float>(i) + 0.5f) / FMath::Max(1, Count));
		const FVector2D Pos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		Crowd->SpawnEnemy(*Def, Pos);
	}

	UE_LOG(LogWS2Wave, Display, TEXT("Perf horde spawned: %d enemies alive"), Crowd->GetAliveCount());
}

void UWS2WaveDirector::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActive || !GameMode)
	{
		return;
	}

	AWS2Crowd* Crowd = GameMode->GetCrowd();
	AWS2Arena* Arena = GameMode->GetArena();
	AWS2PlayerPawn* Player = GameMode->GetPlayerPawn();
	if (!Crowd || !Arena || !Player)
	{
		return;
	}

	WaveTimer -= DeltaTime;
	SpawnTimer -= DeltaTime;

	if (bBossWave && bBossSpawned)
	{
		const bool bAlive = Crowd->HasBossAlive();
		bBossAlive = bAlive;
		if (!bAlive)
		{
			bActive = false;
			Crowd->ClearAll();
			Queue.Reset();
			bBossWave = false;
			GameMode->OnWaveCleared();
			UE_LOG(LogWS2Wave, Display, TEXT("Boss wave %d cleared: boss down"), Wave);
			return;
		}
		// No timer end while the boss lives: the phase table is the encounter.
		if (WaveTimer > 0.f)
		{
			// keep spawning trash pressure during the boss fight
		}
	}
	else if (WaveTimer <= 0.f)
	{
		// Brotato pacing: the wave ends on the clock, leftovers despawn.
		bActive = false;
		Crowd->ClearAll();
		Queue.Reset();
		if (GameMode)
		{
			GameMode->OnWaveCleared();
		}
		UE_LOG(LogWS2Wave, Display, TEXT("Wave %d cleared by timer"), Wave);
		return;
	}

	if (Queue.Num() > 0 && SpawnTimer <= 0.f && Crowd->GetAliveCount() < MaxAliveEnemies)
	{
		const int32 DefIndex = Queue[0];
		Queue.RemoveAt(0, 1, EAllowShrinking::No);

		const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(DefIndex);
		const FVector2D PlayerPos = Player->GetArenaPos();

		FVector2D SpawnPos = Arena->GetCenter();
		for (int32 Try = 0; Try < 12; ++Try)
		{
			const FVector2D Candidate = Arena->GetRandomOpenPoint(Rng, Def.Radius);
			if (FVector2D::Distance(Candidate, PlayerPos) > 620.f)
			{
				SpawnPos = Candidate;
				break;
			}
			SpawnPos = Candidate;
		}

		Crowd->SpawnEnemy(Def, SpawnPos);
		SpawnTimer = FMath::Max(0.08f, 0.5f - Wave * 0.03f);
	}
}
