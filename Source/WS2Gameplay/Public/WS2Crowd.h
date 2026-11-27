#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WS2Data.h"
#include "WS2Math.h"
#include "WS2Crowd.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class AWS2Arena;
class AWS2PlayerPawn;
class AWS2GameMode;

/** Compact visual rows replicated from the authoritative crowd to every client. */
USTRUCT()
struct FWS2NetEnemy
{
	GENERATED_BODY()

	UPROPERTY() FVector2D WorldPos = FVector2D::ZeroVector;
	UPROPERTY() float Radius = 12.f;
	UPROPERTY() float Flash = 0.f;
	UPROPERTY() int32 DefIndex = 0;
	UPROPERTY() int32 BossIndex = -1;
	UPROPERTY() bool bBoss = false;
	UPROPERTY() bool bElite = false;
};

USTRUCT()
struct FWS2NetProjectile
{
	GENERATED_BODY()

	UPROPERTY() FVector2D WorldPos = FVector2D::ZeroVector;
	UPROPERTY() float Radius = 8.f;
	UPROPERTY() FLinearColor Color = FLinearColor::White;
};

USTRUCT()
struct FWS2NetEnemyShot
{
	GENERATED_BODY()

	UPROPERTY() FVector2D WorldPos = FVector2D::ZeroVector;
	UPROPERTY() float Radius = 10.f;
};

USTRUCT()
struct FWS2NetPickup
{
	GENERATED_BODY()

	UPROPERTY() FVector2D WorldPos = FVector2D::ZeroVector;
};

/**
 * The horde layer: one actor owns every enemy, projectile and pickup in the run.
 * Nothing here is a UCharacter - entities are plain structs simulated on a fixed
 * sub-step and rendered through instanced meshes, which is what keeps hundreds of
 * bodies affordable.
 */
UCLASS()
class WS2GAMEPLAY_API AWS2Crowd : public AActor
{
	GENERATED_BODY()

public:
	AWS2Crowd();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Init(AWS2Arena* InArena, AWS2PlayerPawn* InPlayer, AWS2GameMode* InGameMode);
	void AddPlayer(AWS2PlayerPawn* InPlayer);
	void RemovePlayer(AWS2PlayerPawn* InPlayer);

	/** Drops roster entries whose pawn was torn down (client left mid-run). */
	void PrunePlayers();
	AWS2PlayerPawn* NearestPlayer(const FVector2D& From) const;
	int32 GetPlayerCount() const { return Players.Num(); }

	int32 SpawnEnemy(const WS2::FEnemyDef& Def, const FVector2D& Position, bool bElite = false);
	int32 SpawnBoss(const WS2::FBossDef& Def, const FVector2D& Position, float HpScale);
	/** Spawns one of every requested enemy type in a ring; used by the roster proof. */
	int32 SpawnRoster(const FVector2D& Center, float RingRadius);
	bool GetBossInfo(FString& OutName, FVector2D& OutPos, float& OutHpPct) const;
	int32 SpawnBurst(const WS2::FEnemyDef& Def, int32 Count, float RingRadius);

	void SpawnProjectile(const FVector2D& From, const FVector2D& Dir, float Damage, float Speed,
		int32 Pellets, float Spread, float Pierce, bool bCrit, float Knockback, const FLinearColor& Color);
	void SpawnProjectileEx(const FVector2D& From, const FVector2D& Dir, float Damage, float Speed,
		int32 Pellets, float Spread, float Pierce, bool bCrit, float Knockback, const FLinearColor& Color,
		float BlastRadius, const FString& Element, float SlowTime, float BurnDps, float StunTime,
		float Chain, float ChainRange, float Lifesteal, float ArmorPierce);

	bool FindNearestEnemy(const FVector2D& From, float MaxRange, FVector2D& OutPosition) const;

	/** Weapon behaviour hooks. Each returns the number of bodies actually hit. */
	int32 DamageArc(const FVector2D& From, const FVector2D& Dir, float Range, float ArcDegrees,
		float Damage, bool bCrit, const FString& Element, float SlowTime, float BurnDps, float StunTime,
		float ArmorPierce, float Knockback);
	int32 DamageLine(const FVector2D& From, const FVector2D& Dir, float Range, float Damage, bool bCrit,
		const FString& Element, float SlowTime, float BurnDps, float StunTime, float ArmorPierce);
	int32 DamageArea(const FVector2D& Center, float Radius, float Damage, bool bCrit, const FString& Element,
		float SlowTime, float BurnDps, float StunTime, float ArmorPierce);
	int32 DamageChain(const FVector2D& From, float Damage, int32 Links, float ChainRange, bool bCrit,
		const FString& Element, float StunTime, float ArmorPierce);
	void HealEnemiesNear(const FVector2D& Center, float Radius, float Amount);

	void RegisterShot(int32 Count = 1) { ProjectileFires += FMath::Max(0, Count); }
	void RegisterHit(int32 Count = 1) { ProjectileHits += FMath::Max(0, Count); }
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlaySfx(uint8 Sfx, FVector Location);

	int32 GetAliveCount() const { return HasAuthority() ? AliveCount : NetAliveCount; }
	int32 GetProjectileCount() const { return HasAuthority() ? Projectiles.Num() : NetProjectiles.Num(); }
	int32 GetEnemyProjectileCount() const { return HasAuthority() ? EnemyProjectiles.Num() : NetShots.Num(); }
	int32 GetPeakEnemies() const { return PeakEnemies; }
	int32 GetProjectileFires() const { return ProjectileFires; }
	int32 GetProjectileHits() const { return ProjectileHits; }
	int32 GetKillCount() const { return KillCount; }
	float GetWorstOverlap() const { return WorstOverlap; }
	int32 GetOverlapPairs() const { return OverlapPairs; }
	/** One-line render self-check: instance totals, mesh and material actually bound. */
	FString RenderDiag() const;
	void ClearAll();
	void SpawnPickupAt(const FVector2D& Position, int32 Amount);
	int32 GetPickupCount() const { return HasAuthority() ? Pickups.Num() : NetPickups.Num(); }

	float ConsumeHitStop()
	{
		const float Value = PendingHitStop;
		PendingHitStop = 0.f;
		return Value;
	}
	FVector2D ConsumeShake(float& OutAmount)
	{
		OutAmount = PendingShakeAmount;
		PendingShakeAmount = 0.f;
		return PendingShakeAt;
	}
	float ConsumePunch()
	{
		const float Value = PendingPunch;
		PendingPunch = 0.f;
		return Value;
	}

	struct FEnemy
	{
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Vel = FVector2D::ZeroVector;
		FVector2D ChargeDir = FVector2D::ZeroVector;
		float Hp = 10.f;
		float MaxHp = 10.f;
		float Radius = 12.f;
		float Speed = 100.f;
		float Damage = 6.f;
		float Armor = 0.f;
		float Flash = 0.f;
		float SlowTime = 0.f;
		float BurnTime = 0.f;
		float BurnDps = 0.f;
		float ShockTime = 0.f;
		float FireTimer = 0.f;
		float AbilityTimer = 0.f;
		float ChargeTimer = 0.f;
		float ChargeCooldown = 0.f;
		float Shield = 0.f;
		float MaxShield = 0.f;
		int32 DefIndex = 0;
		int32 BossIndex = -1;
		int32 Phase = 0;
		bool bAlive = false;
		bool bElite = false;
		bool bBoss = false;
		bool bFlying = false;
	};

	struct FProjectile
	{
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Vel = FVector2D::ZeroVector;
		float Damage = 0.f;
		float Life = 0.f;
		float Radius = 8.f;
		float Knockback = 0.f;
		float BlastRadius = 0.f;
		float SlowTime = 0.f;
		float BurnDps = 0.f;
		float StunTime = 0.f;
		float Chain = 0.f;
		float ChainRange = 150.f;
		float Lifesteal = 0.f;
		float ArmorPierce = 0.f;
		int32 PierceLeft = 0;
		bool bCrit = false;
		FString Element;
		FLinearColor Color = FLinearColor::White;
		TArray<int32> Hits;
	};

	struct FEnemyShot
	{
		FVector2D Pos = FVector2D::ZeroVector;
		FVector2D Vel = FVector2D::ZeroVector;
		float Life = 1.8f;
		float Radius = 10.f;
		float Damage = 8.f;
		FLinearColor Color = FLinearColor(1.f, 0.3f, 0.3f);
	};

	struct FPickup
	{
		FVector2D Pos = FVector2D::ZeroVector;
		int32 Amount = 1;
		float Life = 20.f;
	};

	/** One boss instance's phase log, written verbatim into the arena result JSON. */
	struct FBossRuntime
	{
		int32 BossDefIndex = -1;
		int32 EnemySlot = -1;
		int32 Phase = 0;
		int32 Attacks = 0;
		bool bDefeated = false;
		float SpawnTime = 0.f;
		float Hp = 0.f;
		float MaxHp = 0.f;
		TArray<int32> PhaseReached;
		TArray<FString> PhasePatterns;
	};

	bool HasBossAlive() const;
	int32 GetActiveBossPhase() const;
	const TArray<FEnemy>& GetEnemies() const { return Enemies; }
	const TArray<int32>& GetFreeSlots() const { return FreeSlots; }
	const TArray<int32>& GetEnemySpawnCounts() const { return SpawnCounts; }
	const TArray<int32>& GetEnemyKillCounts() const { return KillCounts; }
	const TArray<FBossRuntime>& GetBossRuntimes() const { return BossRuntimes; }

private:
	void RebuildGrid();
	void UpdateEnemies(float Dt);
	void UpdateEnemyMovement(FEnemy& Enemy, float Dt, const FVector2D& PlayerPos, float Dist,
		const FVector2D& ChaseDir);
	void UpdateEnemyProfile(FEnemy& Enemy, float Dt, const FVector2D& PlayerPos, float Dist);
	void UpdateBoss(FEnemy& Enemy, float Dt, const FVector2D& PlayerPos);
	void FireBossPattern(FEnemy& Enemy, int32 PhaseIndex);
	void FireEnemyShot(const FVector2D& From, const FVector2D& Dir, float Speed, float Damage,
		const FLinearColor& Color, float Radius = 10.f);
	void SummonForBoss(const FEnemy& Enemy, const FString& SummonId);
	void ResolveOverlaps();
	void ResolveOverlapsOnce();
	FVector2D DepenetrateSpawn(const FVector2D& Wanted, float Radius);
	void ConstrainToPlayer(FEnemy& Enemy, AWS2PlayerPawn* TargetPlayer) const;
	void MeasureOverlaps();
	void UpdateProjectiles(float Dt);
	void UpdateEnemyProjectiles(float Dt);
	void UpdatePickups(float Dt);
	void UpdateRender();
	void UpdateNetSnapshot();
	void RenderNetSnapshot();
	void KillEnemy(int32 Index);
	void DamageEnemy(int32 Index, float Damage, bool bCrit, const FVector2D& Dir, float Knockback,
		const FString& Element = FString(), float SlowTime = 0.f, float BurnDps = 0.f,
		float StunTime = 0.f, float ArmorPierce = 0.f);

	UInstancedStaticMeshComponent* EnsureEnemyMesh(int32 DefIndex);
	UInstancedStaticMeshComponent* EnsureBossMesh(int32 BossDefIndex);
	UInstancedStaticMeshComponent* EnsureProjectileMesh();
	UInstancedStaticMeshComponent* EnsureEnemyShotMesh();
	UInstancedStaticMeshComponent* EnsurePickupMesh();
	UStaticMesh* LoadEnemyArchetype(const WS2::FEnemyDef& Def, FString& OutAssetKey) const;
	UStaticMesh* LoadBossArchetype(const WS2::FBossDef& Def, FString& OutAssetKey) const;

	TArray<FEnemy> Enemies;
	TArray<int32> FreeSlots;
	TArray<FProjectile> Projectiles;
	TArray<FEnemyShot> EnemyProjectiles;
	TArray<FPickup> Pickups;
	FWS2SpatialGrid Grid;
	FWS2Rng Rng;

	UPROPERTY()
	USceneComponent* Root = nullptr;

	UPROPERTY()
	TArray<UInstancedStaticMeshComponent*> EnemyMeshes;

	UPROPERTY()
	TArray<UInstancedStaticMeshComponent*> BossMeshes;

	UPROPERTY()
	TArray<UMaterialInterface*> EnemyMaterials;

	UPROPERTY()
	TArray<UMaterialInterface*> BossMaterials;

	UPROPERTY()
	UInstancedStaticMeshComponent* ProjectileMesh = nullptr;

	UPROPERTY()
	UInstancedStaticMeshComponent* EnemyShotMesh = nullptr;

	UPROPERTY()
	UInstancedStaticMeshComponent* PickupMesh = nullptr;

	TMap<int32, int32> DefToMesh;
	TMap<int32, int32> BossToMesh;
	TMap<FString, int32> KeyToMesh;
	TMap<int32, float> DefScale;
	TMap<int32, float> BossScale;

	TArray<int32> SpawnCounts;
	TArray<int32> KillCounts;
	TArray<FBossRuntime> BossRuntimes;

	/** Largest enemy radius ever spawned: every separation query widens to cover it. */
	float MaxEnemyRadius = 12.f;
	/** Hard overlap passes per tick; more passes only run while overlaps remain. */
	static constexpr int32 MaxOverlapPasses = 16;

	AWS2Arena* Arena = nullptr;
	AWS2PlayerPawn* Player = nullptr;
	AWS2GameMode* GameMode = nullptr;
	TArray<AWS2PlayerPawn*> Players;
	float NetSnapshotTimer = 0.f;

	UPROPERTY(ReplicatedUsing = OnRep_NetSnapshot)
	TArray<FWS2NetEnemy> NetEnemies;

	UPROPERTY(Replicated)
	TArray<FWS2NetProjectile> NetProjectiles;

	UPROPERTY(Replicated)
	TArray<FWS2NetEnemyShot> NetShots;

	UPROPERTY(Replicated)
	TArray<FWS2NetPickup> NetPickups;

	UPROPERTY(Replicated)
	int32 NetAliveCount = 0;

	UFUNCTION()
	void OnRep_NetSnapshot();

	float ElapsedTime = 0.f;
	int32 AliveCount = 0;
	int32 PeakEnemies = 0;
	int32 MaterialPasses = 4;
	int32 ProjectileFires = 0;
	int32 ProjectileHits = 0;
	int32 KillCount = 0;
	float WorstOverlap = 0.f;
	int32 OverlapPairs = 0;
	float PendingHitStop = 0.f;
	FVector2D PendingShakeAt = FVector2D::ZeroVector;
	float PendingShakeAmount = 0.f;
	float PendingPunch = 0.f;
	bool bLoggedClientSnapshot = false;
	float LastHitSfxTime = -1.f;
	float LastKillSfxTime = -1.f;
	float LastPickupSfxTime = -1.f;
	int32 NetVerifyDamageLogs = 0;
	bool bNetVerifyHitSnapshotLogged = false;
};
