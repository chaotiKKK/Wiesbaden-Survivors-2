#include "WS2Crowd.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Parse.h"
#include "WS2Arena.h"
#include "WS2GameMode.h"
#include "WS2PlayerPawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Crowd, Log, All);

static TAutoConsoleVariable<int32> CVarNoVat(
	TEXT("ws2.NoVat"), 0, TEXT("Debug: zero the vertex-animation offsets (0=off, 1=on)."));

namespace
{
	FLinearColor CrowdHex(const FString& Hex, const FLinearColor& Fallback)
	{
		FString Value = Hex.TrimStartAndEnd();
		if (Value.StartsWith(TEXT("#")))
		{
			Value = Value.RightChop(1);
		}
		if (Value.Len() >= 6)
		{
			const int32 R = FParse::HexNumber(*Value.Mid(0, 2));
			const int32 G = FParse::HexNumber(*Value.Mid(2, 2));
			const int32 B = FParse::HexNumber(*Value.Mid(4, 2));
			return FLinearColor(R / 255.f, G / 255.f, B / 255.f);
		}
		return Fallback;
	}

	UMaterialInterface* CrowdColorMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			UMaterialInterface* Found = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/M_WS2Color.M_WS2Color"));
			if (!Found)
			{
				Found = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
			}
			Cached = Found;
		}
		return Cached.Get();
	}

	UMaterialInterface* VatMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			UMaterialInterface* Found = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/M_WS2VAT.M_WS2VAT"));
			Cached = Found;
		}
		return Cached.Get();
	}

	FString TitleCase(const FString& Id)
	{
		FString Result;
		bool bUpper = true;
		for (const TCHAR Ch : Id)
		{
			if (Ch == TEXT('_') || Ch == TEXT('-') || Ch == TEXT(' '))
			{
				bUpper = true;
				continue;
			}
			Result.AppendChar(bUpper ? FChar::ToUpper(Ch) : Ch);
			bUpper = false;
		}
		return Result;
	}

	/** Every enemy row owns an asset named after its id, so shape and data stay 1:1. */
	FString EnemyAssetKey(const WS2::FEnemyDef& Def)
	{
		return TEXT("SM_") + TitleCase(Def.Id);
	}

	FString BossAssetKey(const WS2::FBossDef& Def)
	{
		return TEXT("SM_Boss_") + TitleCase(Def.Id);
	}

	FString ShapeFallback(const FString& Shape)
	{
		if (Shape == TEXT("dot")) return TEXT("SM_Swarm");
		if (Shape == TEXT("tri") || Shape == TEXT("prism")) return TEXT("SM_Runner");
		if (Shape == TEXT("brick") || Shape == TEXT("box")) return TEXT("SM_Tank");
		return TEXT("SM_Walker");
	}
}

AWS2Crowd::AWS2Crowd()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Rng = FWS2Rng(0x51EDC0DEull);
}

void AWS2Crowd::BeginPlay()
{
	Super::BeginPlay();
	Enemies.Reserve(2048);

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	Db.EnsureLoaded();
	SpawnCounts.Init(0, Db.NumEnemies());
	KillCounts.Init(0, Db.NumEnemies());

	// Build every archetype component up front: registering an instanced mesh while
	// the match is already running is what used to end the process mid-wave.
	for (int32 Index = 0; Index < Db.NumEnemies(); ++Index)
	{
		EnsureEnemyMesh(Index);
	}
	for (int32 Index = 0; Index < Db.Bosses.Num(); ++Index)
	{
		EnsureBossMesh(Index);
	}
	// Effect/pickup components are deliberately created before the first frame:
	// lazily registering one on the first kill produced a 30 ms hitch that polluted
	// the minimum even though the steady-state frame rate was fine.
	EnsureProjectileMesh();
	EnsureEnemyShotMesh();
	EnsurePickupMesh();
	UE_LOG(LogWS2Crowd, Display, TEXT("Crowd ready: %d enemy archetypes, %d bosses, %d weapons"),
		Db.NumEnemies(), Db.Bosses.Num(), Db.Weapons.Num());
}

void AWS2Crowd::Init(AWS2Arena* InArena, AWS2PlayerPawn* InPlayer, AWS2GameMode* InGameMode)
{
	Arena = InArena;
	Player = InPlayer;
	GameMode = InGameMode;
	Enemies.Reserve(2048);
}

UStaticMesh* AWS2Crowd::LoadEnemyArchetype(const WS2::FEnemyDef& Def, FString& OutAssetKey) const
{
	OutAssetKey = EnemyAssetKey(Def);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
		*FString::Printf(TEXT("/Game/Art/Enemies/Slice/%s.%s"), *OutAssetKey, *OutAssetKey));
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr,
			*FString::Printf(TEXT("/Game/Art/Enemies/%s.%s"), *OutAssetKey, *OutAssetKey));
	}
	if (!Mesh)
	{
		const FString Fallback = ShapeFallback(Def.Shape);
		Mesh = LoadObject<UStaticMesh>(nullptr,
			*FString::Printf(TEXT("/Game/Art/Enemies/Slice/%s.%s"), *Fallback, *Fallback));
		UE_LOG(LogWS2Crowd, Warning, TEXT("missing archetype %s (%s), using %s"), *OutAssetKey, *Def.Id, *Fallback);
	}
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}
	return Mesh;
}

UInstancedStaticMeshComponent* AWS2Crowd::EnsureEnemyMesh(int32 DefIndex)
{
	if (const int32* Existing = DefToMesh.Find(DefIndex))
	{
		return EnemyMeshes[*Existing];
	}

	const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(DefIndex);
	FString AssetKey;
	UStaticMesh* Archetype = LoadEnemyArchetype(Def, AssetKey);

	UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetupAttachment(Root);
	AddInstanceComponent(Mesh);

	Mesh->SetStaticMesh(Archetype);
	// One atlas per archetype, baked on LOD0's vertex order: force LOD0 or the
	// cheaper LODs would sample columns that do not exist.
	Mesh->SetForcedLodModel(1);

	const FBoxSphereBounds ArchetypeBounds = Archetype->GetBounds();
	const float MeshRadius = FMath::Max(1.f, FMath::Max(ArchetypeBounds.BoxExtent.X, ArchetypeBounds.BoxExtent.Y));
	const float VisualRadius = FMath::Max(Def.Radius * 2.f, 30.f);
	DefScale.Add(DefIndex, FMath::Clamp(VisualRadius / MeshRadius, 0.02f, 20.f));

	const WS2::FVatInfo* Vat = WS2::FDatabase::Get().FindVat(AssetKey);
	if (UMaterialInterface* Base = VatMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
		UTexture* Atlas = LoadObject<UTexture>(nullptr,
			*FString::Printf(TEXT("/Game/Art/VAT/%s_VAT.%s_VAT"), *AssetKey, *AssetKey));
		if (Atlas)
		{
			Mid->SetTextureParameterValue(TEXT("VAT"), Atlas);
		}
		else
		{
			// A missing atlas still gets a readable colour instead of a grey body.
			Mid->SetVectorParameterValue(TEXT("VATExtent"), FVector::ZeroVector);
		}
		FLinearColor Tint = CrowdHex(Def.Color, FLinearColor(0.9f, 0.3f, 0.35f)) * 0.65f;
		Tint.A = 1.f;
		Mid->SetVectorParameterValue(TEXT("Color"), Tint);
		const bool bNoVat = CVarNoVat.GetValueOnGameThread() > 0;
		Mid->SetVectorParameterValue(TEXT("VATExtent"),
			bNoVat ? FVector::ZeroVector : (Vat ? Vat->Extent : FVector(20.f, 20.f, 20.f)));
		Mid->SetScalarParameterValue(TEXT("VATFrames"), Vat ? static_cast<float>(Vat->Frames) : 16.f);
		Mid->SetScalarParameterValue(TEXT("VATFps"), Vat ? Vat->Fps : 24.f);
		Mid->SetScalarParameterValue(TEXT("VATCount"), Vat ? static_cast<float>(Vat->Frames) : 16.f);
		Mid->SetScalarParameterValue(TEXT("VATStart"), 0.f);
		Mid->SetScalarParameterValue(TEXT("VATRandomize"), 1.f);
		const int32 NumSlots = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetStaticMaterials().Num() : 0;
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			Mesh->SetMaterial(Slot, Mid);
		}
		EnemyMaterials.Add(Mid);

		UE_LOG(LogWS2Crowd, Display, TEXT("archetype %s: mesh=%s vat=%s frames=%d shape=%s ai=%s"),
			*AssetKey, *GetNameSafe(Archetype), Atlas ? *Atlas->GetName() : TEXT("none"),
			Vat ? Vat->Frames : 0, *Def.Shape, *Def.AI);
	}
	else if (UMaterialInterface* ColorBase = CrowdColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(ColorBase, Mesh);
		Mid->SetVectorParameterValue(TEXT("Color"), CrowdHex(Def.Color, FLinearColor(0.9f, 0.3f, 0.35f)));
		const int32 NumSlots = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetStaticMaterials().Num() : 0;
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			Mesh->SetMaterial(Slot, Mid);
		}
		EnemyMaterials.Add(Mid);
	}

	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->RegisterComponent();
	Mesh->MarkRenderStateDirty();

	const int32 MeshIndex = EnemyMeshes.Add(Mesh);
	DefToMesh.Add(DefIndex, MeshIndex);
	KeyToMesh.Add(AssetKey, MeshIndex);
	return Mesh;
}

UInstancedStaticMeshComponent* AWS2Crowd::EnsureBossMesh(int32 BossDefIndex)
{
	if (const int32* Existing = BossToMesh.Find(BossDefIndex))
	{
		return BossMeshes[*Existing];
	}

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	if (!Db.Bosses.IsValidIndex(BossDefIndex))
	{
		return nullptr;
	}
	const WS2::FBossDef& Def = Db.Bosses[BossDefIndex];
	FString AssetKey;
	UStaticMesh* Archetype = LoadBossArchetype(Def, AssetKey);

	UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(this);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetupAttachment(Root);
	AddInstanceComponent(Mesh);
	Mesh->SetStaticMesh(Archetype);

	const FBoxSphereBounds Bounds = Archetype->GetBounds();
	const float MeshRadius = FMath::Max(1.f, FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y));
	const float VisualRadius = FMath::Max(Def.Radius * 2.4f, 100.f);
	BossScale.Add(BossDefIndex, FMath::Clamp(VisualRadius / MeshRadius, 0.02f, 30.f));

	if (UMaterialInterface* Base = CrowdColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
		FLinearColor Tint = CrowdHex(Def.Color, FLinearColor(1.f, 0.7f, 0.3f));
		Tint.A = 1.f;
		Mid->SetVectorParameterValue(TEXT("Color"), Tint);
		const int32 NumSlots = Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetStaticMaterials().Num() : 0;
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			Mesh->SetMaterial(Slot, Mid);
		}
		BossMaterials.Add(Mid);
	}

	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->RegisterComponent();
	Mesh->MarkRenderStateDirty();

	const int32 MeshIndex = BossMeshes.Add(Mesh);
	BossToMesh.Add(BossDefIndex, MeshIndex);
	UE_LOG(LogWS2Crowd, Display, TEXT("boss archetype %s: mesh=%s phases=%d"),
		*AssetKey, *GetNameSafe(Archetype), Def.Phases.Num());
	return Mesh;
}

UStaticMesh* AWS2Crowd::LoadBossArchetype(const WS2::FBossDef& Def, FString& OutAssetKey) const
{
	OutAssetKey = BossAssetKey(Def);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
		*FString::Printf(TEXT("/Game/Art/Bosses/%s.%s"), *OutAssetKey, *OutAssetKey));
	if (!Mesh)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		UE_LOG(LogWS2Crowd, Warning, TEXT("missing boss archetype %s"), *OutAssetKey);
	}
	return Mesh;
}

UInstancedStaticMeshComponent* AWS2Crowd::EnsureProjectileMesh()
{
	if (ProjectileMesh)
	{
		return ProjectileMesh;
	}

	if (GameMode) GameMode->SetFrameTag(TEXT("first-projectile-mesh"));
	ProjectileMesh = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Projectiles"));
	ProjectileMesh->SetupAttachment(Root);
	AddInstanceComponent(ProjectileMesh);
	ProjectileMesh->RegisterComponent();
	ProjectileMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	ProjectileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ProjectileMesh->SetCastShadow(false);
	ProjectileMesh->SetMobility(EComponentMobility::Movable);

	if (UMaterialInterface* Base = CrowdColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, ProjectileMesh);
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.35f));
		ProjectileMesh->SetMaterial(0, Mid);
		EnemyMaterials.Add(Mid);
	}
	return ProjectileMesh;
}

UInstancedStaticMeshComponent* AWS2Crowd::EnsureEnemyShotMesh()
{
	if (EnemyShotMesh)
	{
		return EnemyShotMesh;
	}

	if (GameMode) GameMode->SetFrameTag(TEXT("first-enemy-shot-mesh"));
	EnemyShotMesh = NewObject<UInstancedStaticMeshComponent>(this, TEXT("EnemyShots"));
	EnemyShotMesh->SetupAttachment(Root);
	AddInstanceComponent(EnemyShotMesh);
	EnemyShotMesh->RegisterComponent();
	EnemyShotMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	EnemyShotMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EnemyShotMesh->SetCastShadow(false);
	EnemyShotMesh->SetMobility(EComponentMobility::Movable);

	if (UMaterialInterface* Base = CrowdColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, EnemyShotMesh);
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.22f, 0.22f));
		EnemyShotMesh->SetMaterial(0, Mid);
	}
	return EnemyShotMesh;
}

UInstancedStaticMeshComponent* AWS2Crowd::EnsurePickupMesh()
{
	if (PickupMesh)
	{
		return PickupMesh;
	}

	if (GameMode) GameMode->SetFrameTag(TEXT("first-pickup-mesh"));
	PickupMesh = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Pickups"));
	PickupMesh->SetupAttachment(Root);
	AddInstanceComponent(PickupMesh);
	PickupMesh->RegisterComponent();
	PickupMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	PickupMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupMesh->SetCastShadow(false);
	PickupMesh->SetMobility(EComponentMobility::Movable);

	if (UMaterialInterface* Base = CrowdColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, PickupMesh);
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.35f, 1.f, 0.6f));
		PickupMesh->SetMaterial(0, Mid);
	}
	return PickupMesh;
}

int32 AWS2Crowd::SpawnEnemy(const WS2::FEnemyDef& Def, const FVector2D& Position, bool bElite)
{
	if (!Arena)
	{
		return INDEX_NONE;
	}

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const int32 DefIndex = Db.Enemies.IndexOfByPredicate(
		[&Def](const WS2::FEnemyDef& Candidate) { return Candidate.Id == Def.Id; });
	if (DefIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	FEnemy Enemy;
	Enemy.DefIndex = DefIndex;
	MaxEnemyRadius = FMath::Max(MaxEnemyRadius, Def.Radius);
	Enemy.Pos = DepenetrateSpawn(Arena->ClampToBounds(Position, Def.Radius), Def.Radius);
	Enemy.MaxHp = Def.Hp;
	Enemy.Hp = Def.Hp;
	Enemy.Radius = Def.Radius;
	Enemy.Speed = Def.Speed;
	Enemy.Damage = Def.Dmg;
	Enemy.Armor = Def.Armor;
	Enemy.bAlive = true;
	Enemy.bFlying = Def.bFly;
	Enemy.bElite = bElite || Def.Id.Contains(TEXT("elite")) || Def.Id.Contains(TEXT("juggernaut"));
	Enemy.FireTimer = Rng.Range(0.6f, 1.8f);
	Enemy.AbilityTimer = Rng.Range(1.5f, 3.5f);
	Enemy.ChargeCooldown = Rng.Range(1.0f, 2.5f);
	if (Def.AI == TEXT("shielded") || Def.AI == TEXT("aura"))
	{
		Enemy.MaxShield = Def.Hp * 0.45f;
		Enemy.Shield = Enemy.MaxShield;
	}
	if (Def.AI == TEXT("healer") || Def.AI == TEXT("summoner") || Def.AI == TEXT("sentry"))
	{
		Enemy.FireTimer = Rng.Range(1.2f, 2.4f);
	}

	EnsureEnemyMesh(DefIndex);

	int32 Slot = INDEX_NONE;
	if (FreeSlots.Num() > 0)
	{
		Slot = FreeSlots.Pop(EAllowShrinking::No);
		Enemies[Slot] = Enemy;
	}
	else
	{
		Slot = Enemies.Add(Enemy);
	}

	if (SpawnCounts.IsValidIndex(DefIndex))
	{
		++SpawnCounts[DefIndex];
	}

	Grid.Insert(Slot, Enemies[Slot].Pos);
	++AliveCount;
	PeakEnemies = FMath::Max(PeakEnemies, AliveCount);
	return Slot;
}

int32 AWS2Crowd::SpawnBoss(const WS2::FBossDef& Def, const FVector2D& Position, float HpScale)
{
	if (!Arena)
	{
		return INDEX_NONE;
	}
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const int32 BossDefIndex = Db.Bosses.IndexOfByPredicate(
		[&Def](const WS2::FBossDef& Candidate) { return Candidate.Id == Def.Id; });
	if (BossDefIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	FEnemy Enemy;
	Enemy.DefIndex = 0;
	Enemy.BossIndex = BossDefIndex;
	Enemy.bBoss = true;
	Enemy.MaxHp = FMath::Max(1.f, Def.Hp * FMath::Max(0.001f, HpScale));
	Enemy.Hp = Enemy.MaxHp;
	Enemy.Radius = Def.Radius;
	Enemy.Speed = Def.Speed;
	Enemy.Damage = Def.Dmg;
	Enemy.Armor = Def.Armor;
	Enemy.bAlive = true;
	Enemy.Phase = 0;
	Enemy.FireTimer = 1.4f;
	Enemy.Pos = DepenetrateSpawn(Arena->ClampToBounds(Position, Def.Radius), Def.Radius);
	MaxEnemyRadius = FMath::Max(MaxEnemyRadius, Def.Radius);

	EnsureBossMesh(BossDefIndex);

	int32 Slot = INDEX_NONE;
	if (FreeSlots.Num() > 0)
	{
		Slot = FreeSlots.Pop(EAllowShrinking::No);
		Enemies[Slot] = Enemy;
	}
	else
	{
		Slot = Enemies.Add(Enemy);
	}
	Grid.Insert(Slot, Enemies[Slot].Pos);
	++AliveCount;
	PeakEnemies = FMath::Max(PeakEnemies, AliveCount);

	FBossRuntime Runtime;
	Runtime.BossDefIndex = BossDefIndex;
	Runtime.EnemySlot = Slot;
	Runtime.Phase = 0;
	Runtime.Hp = Enemy.Hp;
	Runtime.MaxHp = Enemy.MaxHp;
	Runtime.SpawnTime = ElapsedTime;
	Runtime.PhaseReached.Add(0);
	if (Def.Phases.Num() > 0)
	{
		Runtime.PhasePatterns.Add(Def.Phases[0].Pattern);
	}
	BossRuntimes.Add(Runtime);

	UE_LOG(LogWS2Crowd, Display,
		TEXT("BOSS SPAWN id=%s name=%s hp=%.0f (scale %.3f) phases=%d arena=%s"),
		*Def.Id, *Def.Name, Enemy.MaxHp, HpScale, Def.Phases.Num(), *Def.Arena);
	return Slot;
}

int32 AWS2Crowd::SpawnRoster(const FVector2D& Center, float RingRadius)
{
	if (!Arena)
	{
		return 0;
	}
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	int32 Spawned = 0;
	const float GoldenAngle = PI * (3.f - FMath::Sqrt(5.f));
	for (int32 Index = 0; Index < Db.Enemies.Num(); ++Index)
	{
		const WS2::FEnemyDef& Def = Db.Enemies[Index];
		const float Angle = Index * GoldenAngle;
		const float Radius = RingRadius * (0.85f + 0.3f * Rng.FRand());
		FVector2D Pos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		Arena->ResolveCircle(Pos, Def.Radius);
		if (SpawnEnemy(Def, Pos) != INDEX_NONE)
		{
			++Spawned;
		}
	}
	UE_LOG(LogWS2Crowd, Display, TEXT("ROSTER spawn: %d/%d enemy types around (%.0f,%.0f)"),
		Spawned, Db.Enemies.Num(), Center.X, Center.Y);
	return Spawned;
}

bool AWS2Crowd::HasBossAlive() const
{
	for (const FBossRuntime& Runtime : BossRuntimes)
	{
		if (!Runtime.bDefeated && Runtime.Hp > 0.f)
		{
			return true;
		}
	}
	return false;
}

int32 AWS2Crowd::GetActiveBossPhase() const
{
	for (const FBossRuntime& Runtime : BossRuntimes)
	{
		if (!Runtime.bDefeated && Runtime.Hp > 0.f)
		{
			return Runtime.Phase;
		}
	}
	return 0;
}

bool AWS2Crowd::GetBossInfo(FString& OutName, FVector2D& OutPos, float& OutHpPct) const
{
	// Named bosses take priority over the slice elite.
	for (const FEnemy& Enemy : Enemies)
	{
		if (Enemy.bAlive && Enemy.bBoss)
		{
			const WS2::FBossDef& Def = WS2::FDatabase::Get().Bosses[Enemy.BossIndex];
			OutName = FString::Printf(TEXT("%s  P%d"), *Def.Name, Enemy.Phase + 1);
			OutPos = Arena ? Arena->ArenaToWorld(Enemy.Pos) : Enemy.Pos;
			OutHpPct = Enemy.MaxHp > 0.f ? FMath::Clamp(Enemy.Hp / Enemy.MaxHp, 0.f, 1.f) : 0.f;
			return true;
		}
	}
	for (const FEnemy& Enemy : Enemies)
	{
		if (!Enemy.bAlive || !Enemy.bElite || Enemy.bBoss)
		{
			continue;
		}
		const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(Enemy.DefIndex);
		OutName = Def.Name;
		OutPos = Arena ? Arena->ArenaToWorld(Enemy.Pos) : Enemy.Pos;
		OutHpPct = Enemy.MaxHp > 0.f ? FMath::Clamp(Enemy.Hp / Enemy.MaxHp, 0.f, 1.f) : 0.f;
		return true;
	}
	return false;
}

int32 AWS2Crowd::SpawnBurst(const WS2::FEnemyDef& Def, int32 Count, float RingRadius)
{
	if (!Arena)
	{
		return 0;
	}
	const FVector2D Center = Arena->GetCenter();
	int32 Spawned = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Angle = Rng.Range(0.f, 2.f * PI);
		const float Radius = RingRadius > 0.f ? RingRadius * FMath::Sqrt(Rng.FRand()) : Rng.Range(120.f, 420.f);
		const FVector2D Pos = Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;
		if (SpawnEnemy(Def, Pos) != INDEX_NONE)
		{
			++Spawned;
		}
	}
	return Spawned;
}

void AWS2Crowd::SpawnProjectile(const FVector2D& From, const FVector2D& Dir, float Damage, float Speed,
	int32 Pellets, float Spread, float Pierce, bool bCrit, float Knockback, const FLinearColor& Color)
{
	SpawnProjectileEx(From, Dir, Damage, Speed, Pellets, Spread, Pierce, bCrit, Knockback, Color,
		0.f, FString(), 0.f, 0.f, 0.f, 0.f, 150.f, 0.f, 0.f);
}

void AWS2Crowd::SpawnProjectileEx(const FVector2D& From, const FVector2D& Dir, float Damage, float Speed,
	int32 Pellets, float Spread, float Pierce, bool bCrit, float Knockback, const FLinearColor& Color,
	float BlastRadius, const FString& Element, float SlowTime, float BurnDps, float StunTime,
	float Chain, float ChainRange, float Lifesteal, float ArmorPierce)
{
	const FVector2D BaseDir = Dir.GetSafeNormal();
	for (int32 i = 0; i < FMath::Max(1, Pellets); ++i)
	{
		FProjectile Projectile;
		Projectile.Pos = From;
		const float SpreadDegrees = FMath::RadiansToDegrees(Rng.Range(-Spread, Spread));
		Projectile.Vel = BaseDir.GetRotated(SpreadDegrees).GetSafeNormal() * FMath::Max(1.f, Speed);
		Projectile.Damage = Damage;
		Projectile.Life = 1.8f + FMath::Min(1.2f, BlastRadius * 0.004f);
		Projectile.Radius = BlastRadius > 0.f ? 11.f : 9.f;
		Projectile.PierceLeft = FMath::Max(0, FMath::RoundToInt(Pierce));
		Projectile.Knockback = Knockback;
		Projectile.bCrit = bCrit;
		Projectile.Color = Color;
		Projectile.BlastRadius = BlastRadius;
		Projectile.Element = Element;
		Projectile.SlowTime = SlowTime;
		Projectile.BurnDps = BurnDps;
		Projectile.StunTime = StunTime;
		Projectile.Chain = Chain;
		Projectile.ChainRange = ChainRange;
		Projectile.Lifesteal = Lifesteal;
		Projectile.ArmorPierce = ArmorPierce;
		Projectile.Hits.Reserve(8);
		Projectiles.Add(MoveTemp(Projectile));
		++ProjectileFires;
	}
}

bool AWS2Crowd::FindNearestEnemy(const FVector2D& From, float MaxRange, FVector2D& OutPosition) const
{
	float BestDistSq = MaxRange * MaxRange;
	bool bFound = false;
	for (const FEnemy& Enemy : Enemies)
	{
		if (!Enemy.bAlive)
		{
			continue;
		}
		const float DistSq = FVector2D::DistSquared(From, Enemy.Pos);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			OutPosition = Enemy.Pos;
			bFound = true;
		}
	}
	return bFound;
}

void AWS2Crowd::ClearAll()
{
	for (FEnemy& Enemy : Enemies)
	{
		Enemy.bAlive = false;
	}
	FreeSlots.Reset();
	Enemies.Reset();
	Projectiles.Reset();
	EnemyProjectiles.Reset();
	Pickups.Reset();
	AliveCount = 0;
	for (UInstancedStaticMeshComponent* Mesh : EnemyMeshes)
	{
		if (Mesh)
		{
			Mesh->ClearInstances();
		}
	}
	for (UInstancedStaticMeshComponent* Mesh : BossMeshes)
	{
		if (Mesh)
		{
			Mesh->ClearInstances();
		}
	}
	if (ProjectileMesh)
	{
		ProjectileMesh->ClearInstances();
	}
	if (EnemyShotMesh)
	{
		EnemyShotMesh->ClearInstances();
	}
	if (PickupMesh)
	{
		PickupMesh->ClearInstances();
	}
}

void AWS2Crowd::RebuildGrid()
{
	Grid.Reset();
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		if (Enemies[Index].bAlive)
		{
			Grid.Insert(Index, Enemies[Index].Pos);
		}
	}
}

void AWS2Crowd::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Arena || !Player)
	{
		return;
	}

	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	if (Dt <= 0.f)
	{
		return;
	}
	ElapsedTime += Dt;

	RebuildGrid();
	UpdateEnemies(Dt);
	UpdateProjectiles(Dt);
	UpdateEnemyProjectiles(Dt);
	UpdatePickups(Dt);
	UpdateRender();
}

void AWS2Crowd::UpdateEnemies(float Dt)
{
	const FVector2D PlayerPos = Player->GetArenaPos();
	const float PlayerRadius = Player->GetRadius();

	TArray<int32> Neighbors;
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		FEnemy& Enemy = Enemies[Index];
		if (!Enemy.bAlive)
		{
			continue;
		}

		if (Enemy.BurnTime > 0.f)
		{
			Enemy.BurnTime = FMath::Max(0.f, Enemy.BurnTime - Dt);
			DamageEnemy(Index, Enemy.BurnDps * Dt, false, FVector2D::ZeroVector, 0.f, FString(), 0.f, 0.f, 0.f, 0.f);
			if (!Enemies[Index].bAlive)
			{
				continue;
			}
		}

		const FVector2D ToPlayer = PlayerPos - Enemy.Pos;
		const float Dist = ToPlayer.Size();
		const FVector2D ChaseDir = Dist > 1.f ? ToPlayer / Dist : FVector2D::ZeroVector;

		if (Enemy.bBoss)
		{
			UpdateBoss(Enemy, Dt, PlayerPos);
		}
		else
		{
			UpdateEnemyProfile(Enemy, Dt, PlayerPos, Dist);
		}

		const float SlowFactor = Enemy.SlowTime > 0.f ? 0.55f : 1.f;
		if (Enemy.SlowTime > 0.f)
		{
			Enemy.SlowTime = FMath::Max(0.f, Enemy.SlowTime - Dt);
		}

		FVector2D MoveDir = ChaseDir;
		float SpeedMul = 1.f;
		if (Enemy.ShockTime > 0.f)
		{
			Enemy.ShockTime = FMath::Max(0.f, Enemy.ShockTime - Dt);
			MoveDir = FVector2D::ZeroVector;
			SpeedMul = 0.f;
		}
		else if (Enemy.bBoss)
		{
			MoveDir = ChaseDir;
			SpeedMul = 0.65f;
		}
		else
		{
			const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(Enemy.DefIndex);
			if (Def.AI == TEXT("orbit"))
			{
				const FVector2D Tangent(-ChaseDir.Y, ChaseDir.X);
				const float Radial = FMath::Clamp((Dist - 300.f) / 300.f, -1.f, 1.f);
				MoveDir = (Tangent + ChaseDir * Radial).GetSafeNormal();
			}
			else if (Def.AI == TEXT("spiral"))
			{
				const FVector2D Tangent(-ChaseDir.Y, ChaseDir.X);
				MoveDir = (Tangent + ChaseDir * 0.35f).GetSafeNormal();
				SpeedMul = 1.1f;
			}
			else if (Def.AI == TEXT("ranged") || Def.AI == TEXT("mortar") || Def.AI == TEXT("sentinel") ||
				Def.AI == TEXT("bomber") || Def.AI == TEXT("siren"))
			{
				if (Dist < 340.f)
				{
					MoveDir = -ChaseDir;
				}
				else if (Dist > 620.f)
				{
					MoveDir = ChaseDir;
				}
				else
				{
					MoveDir = FVector2D(-ChaseDir.Y, ChaseDir.X) * 0.7f;
				}
				SpeedMul = 0.8f;
			}
			else if (Def.AI == TEXT("weaver"))
			{
				const FVector2D Tangent(-ChaseDir.Y, ChaseDir.X);
				MoveDir = (ChaseDir + Tangent * FMath::Sin(ElapsedTime * 4.f + Index)).GetSafeNormal();
			}
			else if (Def.AI == TEXT("mirror"))
			{
				const FVector2D Tangent(-ChaseDir.Y, ChaseDir.X);
				MoveDir = (ChaseDir + Tangent * 0.7f).GetSafeNormal();
			}
			else if (Def.AI == TEXT("charger") && Enemy.ChargeTimer > 0.f)
			{
				MoveDir = Enemy.ChargeDir;
				SpeedMul = 3.2f;
				Enemy.ChargeTimer = FMath::Max(0.f, Enemy.ChargeTimer - Dt);
			}
			else if (Def.AI == TEXT("teleport") || Def.AI == TEXT("blinker"))
			{
				MoveDir = ChaseDir * 0.5f;
			}
		}

		const FVector2D Push = FVector2D::ZeroVector;
		Enemy.Pos += (MoveDir * Enemy.Speed * SpeedMul * SlowFactor + Push) * Dt;
		if (!Enemy.bFlying)
		{
			Arena->ResolveCircle(Enemy.Pos, Enemy.Radius);
		}
		else
		{
			Enemy.Pos = Arena->ClampToBounds(Enemy.Pos, Enemy.Radius);
		}
		ConstrainToPlayer(Enemy);

		// Resolve this body against its neighbours straight away. The global pass
		// below only has crumbs left to fix, which is what keeps a 300-body horde
		// from collapsing into one pile.
		Grid.Query(Enemy.Pos, (Enemy.Radius + MaxEnemyRadius) * 1.15f, Neighbors);
		for (int32 Other : Neighbors)
		{
			if (Other == Index || !Enemies.IsValidIndex(Other) || !Enemies[Other].bAlive)
			{
				continue;
			}
			const FEnemy& Mate = Enemies[Other];
			FVector2D Delta = Enemy.Pos - Mate.Pos;
			float Gap = Delta.Size();
			const float Wanted = (Enemy.Radius + Mate.Radius) * 0.98f;
			if (Gap >= Wanted)
			{
				continue;
			}
			if (Gap < 0.01f)
			{
				Delta = FVector2D(1.f, 0.f);
				Gap = 0.01f;
			}
			Enemy.Pos += (Delta / Gap) * (Wanted - Gap);
		}

		if (!Enemy.bBoss && Dist < Enemy.Radius + PlayerRadius)
		{
			const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(Enemy.DefIndex);
			if (Def.AI == TEXT("exploder"))
			{
				DamageArea(Enemy.Pos, 120.f, FMath::Max(12.f, Enemy.Damage + 8.f), false, TEXT("boom"), 0.f, 0.f, 0.f, 0.f);
				Enemy.Hp = 0.f;
				KillEnemy(Index);
			}
			else
			{
				Player->ApplyContactDamage(Enemy.Damage * Dt * 1.8f);
				if (Def.AI == TEXT("leech") || Def.AI == TEXT("vampire"))
				{
					Enemy.Hp = FMath::Min(Enemy.MaxHp, Enemy.Hp + Enemy.Damage * Dt * 0.6f);
				}
			}
		}

		if (Enemy.Flash > 0.f)
		{
			Enemy.Flash = FMath::Max(0.f, Enemy.Flash - Dt);
		}
	}

	ResolveOverlaps();
}

void AWS2Crowd::UpdateEnemyProfile(FEnemy& Enemy, float Dt, const FVector2D& PlayerPos, float Dist)
{
	const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(Enemy.DefIndex);
	Enemy.FireTimer -= Dt;
	Enemy.AbilityTimer -= Dt;

	const FVector2D Dir = (PlayerPos - Enemy.Pos).GetSafeNormal();
	const FLinearColor Color = CrowdHex(Def.Color, FLinearColor(1.f, 0.3f, 0.3f));

	if ((Def.AI == TEXT("ranged") || Def.AI == TEXT("mortar") || Def.AI == TEXT("bomber")) &&
		Enemy.FireTimer <= 0.f && Dist < 760.f)
	{
		const float Damage = FMath::Max(6.f, Def.Xp * 2.f + Def.Dmg);
		const float Speed = Def.AI == TEXT("mortar") ? 220.f : (Def.AI == TEXT("bomber") ? 260.f : 300.f);
		FireEnemyShot(Enemy.Pos, Dir, Speed, Damage, Color, Def.AI == TEXT("mortar") ? 14.f : 9.f);
		Enemy.FireTimer = Def.AI == TEXT("mortar") ? 2.6f : 1.9f;
	}
	else if (Def.AI == TEXT("sentinel") && Enemy.FireTimer <= 0.f && Dist < 820.f)
	{
		for (int32 i = 0; i < 8; ++i)
		{
			const float Angle = i / 8.f * 2.f * PI + ElapsedTime;
			FireEnemyShot(Enemy.Pos, FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)), 240.f,
				FMath::Max(5.f, Def.Xp * 2.f), Color, 8.f);
		}
		Enemy.FireTimer = 3.0f;
	}
	else if ((Def.AI == TEXT("siren") || Def.AI == TEXT("spiral")) && Enemy.FireTimer <= 0.f && Dist < 700.f)
	{
		const int32 Shots = Def.AI == TEXT("siren") ? 10 : 5;
		for (int32 i = 0; i < Shots; ++i)
		{
			const float Angle = i / static_cast<float>(Shots) * 2.f * PI + ElapsedTime * 1.5f;
			FireEnemyShot(Enemy.Pos, FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)), 220.f,
				FMath::Max(5.f, Def.Xp * 2.f), Color, 8.f);
		}
		Enemy.FireTimer = 2.4f;
	}
	else if (Def.AI == TEXT("summoner") && Enemy.AbilityTimer <= 0.f)
	{
		if (const WS2::FEnemyDef* Minion = WS2::FDatabase::Get().FindEnemy(TEXT("shardling")))
		{
			SpawnEnemy(*Minion, Enemy.Pos + FVector2D(Rng.Range(-90.f, 90.f), Rng.Range(-90.f, 90.f)));
		}
		Enemy.AbilityTimer = 4.5f;
	}
	else if (Def.AI == TEXT("healer") && Enemy.AbilityTimer <= 0.f)
	{
		HealEnemiesNear(Enemy.Pos, 260.f, 12.f);
		Enemy.AbilityTimer = 2.8f;
	}
	else if ((Def.AI == TEXT("shielded") || Def.AI == TEXT("aura")) && Enemy.AbilityTimer <= 0.f)
	{
		TArray<int32> Near;
		Grid.Query(Enemy.Pos, 280.f, Near);
		for (int32 Other : Near)
		{
			if (!Enemies.IsValidIndex(Other) || !Enemies[Other].bAlive || Other == INDEX_NONE)
			{
				continue;
			}
			FEnemy& Mate = Enemies[Other];
			Mate.MaxShield = FMath::Max(Mate.MaxShield, Mate.MaxHp * 0.35f);
			Mate.Shield = FMath::Min(Mate.MaxShield, Mate.Shield + Mate.MaxHp * 0.12f);
		}
		Enemy.AbilityTimer = 3.2f;
	}
	else if ((Def.AI == TEXT("teleport") || Def.AI == TEXT("blinker")) && Enemy.AbilityTimer <= 0.f)
	{
		FVector2D Blink = PlayerPos + FVector2D(Rng.Range(-260.f, 260.f), Rng.Range(-260.f, 260.f));
		Arena->ResolveCircle(Blink, Enemy.Radius);
		Enemy.Pos = Blink;
		Enemy.AbilityTimer = 3.6f;
		UE_LOG(LogWS2Crowd, Verbose, TEXT("AI teleport %s"), *Def.Id);
	}
	else if (Def.AI == TEXT("charger") && Enemy.ChargeCooldown <= 0.f && Dist < 620.f)
	{
		Enemy.ChargeTimer = 0.75f;
		Enemy.ChargeDir = Dir;
		Enemy.ChargeCooldown = 3.4f;
		UE_LOG(LogWS2Crowd, Verbose, TEXT("AI charge %s"), *Def.Id);
	}
	if (Enemy.ChargeCooldown > 0.f)
	{
		Enemy.ChargeCooldown = FMath::Max(0.f, Enemy.ChargeCooldown - Dt);
	}
}

void AWS2Crowd::UpdateBoss(FEnemy& Enemy, float Dt, const FVector2D& PlayerPos)
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	if (!Db.Bosses.IsValidIndex(Enemy.BossIndex))
	{
		return;
	}
	const WS2::FBossDef& Def = Db.Bosses[Enemy.BossIndex];
	const float Pct = Enemy.MaxHp > 0.f ? Enemy.Hp / Enemy.MaxHp : 0.f;

	while (Enemy.Phase + 1 < Def.Phases.Num() && Pct <= Def.Phases[Enemy.Phase + 1].At)
	{
		++Enemy.Phase;
		if (FBossRuntime* Runtime = BossRuntimes.FindByPredicate(
			[&Def](const FBossRuntime& Candidate)
			{
				return WS2::FDatabase::Get().Bosses.IsValidIndex(Candidate.BossDefIndex) &&
					WS2::FDatabase::Get().Bosses[Candidate.BossDefIndex].Id == Def.Id && !Candidate.bDefeated;
			}))
		{
			Runtime->Phase = Enemy.Phase;
			Runtime->PhaseReached.AddUnique(Enemy.Phase);
			Runtime->PhasePatterns.Add(Def.Phases[Enemy.Phase].Pattern);
			Runtime->Hp = Enemy.Hp;
		}
		Enemy.FireTimer = 0.3f;
		UE_LOG(LogWS2Crowd, Display, TEXT("BOSS PHASE id=%s name=%s phase=%d at=%.0f%% pattern=%s"),
			*Def.Id, *Def.Name, Enemy.Phase + 1, Def.Phases[Enemy.Phase].At * 100.f, *Def.Phases[Enemy.Phase].Pattern);
	}

	Enemy.FireTimer -= Dt;
	if (Enemy.FireTimer <= 0.f && Def.Phases.IsValidIndex(Enemy.Phase))
	{
		FireBossPattern(Enemy, Enemy.Phase);
		Enemy.FireTimer = FMath::Max(0.5f, Def.Phases[Enemy.Phase].Cooldown);
		if (FBossRuntime* Runtime = BossRuntimes.FindByPredicate(
			[&Def](const FBossRuntime& Candidate)
			{
				return WS2::FDatabase::Get().Bosses.IsValidIndex(Candidate.BossDefIndex) &&
					WS2::FDatabase::Get().Bosses[Candidate.BossDefIndex].Id == Def.Id && !Candidate.bDefeated;
			}))
		{
			++Runtime->Attacks;
			Runtime->Hp = Enemy.Hp;
		}
	}

	if (Enemy.ChargeTimer > 0.f)
	{
		Enemy.Pos += Enemy.ChargeDir * Enemy.Speed * 4.f * Dt;
		Enemy.ChargeTimer = FMath::Max(0.f, Enemy.ChargeTimer - Dt);
		Arena->ResolveCircle(Enemy.Pos, Enemy.Radius);
	}
}

void AWS2Crowd::FireBossPattern(FEnemy& Enemy, int32 PhaseIndex)
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	if (!Db.Bosses.IsValidIndex(Enemy.BossIndex) || !Db.Bosses[Enemy.BossIndex].Phases.IsValidIndex(PhaseIndex))
	{
		return;
	}
	const WS2::FBossDef& Def = Db.Bosses[Enemy.BossIndex];
	const WS2::FBossPhase& Phase = Def.Phases[PhaseIndex];
	const FVector2D ToPlayer = (Player ? Player->GetArenaPos() : Enemy.Pos) - Enemy.Pos;
	const FVector2D Aim = ToPlayer.GetSafeNormal();
	const FLinearColor Color = CrowdHex(Def.Color, FLinearColor(1.f, 0.4f, 0.3f));
	const int32 Count = FMath::Clamp(Phase.Count, 1, 40);
	const FString Pattern = Phase.Pattern;

	auto Fan = [&](float SpreadDegrees, int32 Shots, float Speed, float Damage)
	{
		for (int32 i = 0; i < Shots; ++i)
		{
			const float Offset = Shots > 1 ? (i / static_cast<float>(Shots - 1) - 0.5f) * SpreadDegrees : 0.f;
			FireEnemyShot(Enemy.Pos, Aim.GetRotated(Offset), Speed, Damage, Color, 10.f);
		}
	};
	auto Ring = [&](int32 Shots, float Speed, float Damage)
	{
		for (int32 i = 0; i < Shots; ++i)
		{
			const float Angle = i / static_cast<float>(Shots) * 2.f * PI + ElapsedTime;
			FireEnemyShot(Enemy.Pos, FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)), Speed, Damage, Color, 9.f);
		}
	};

	const float Damage = FMath::Max(6.f, Phase.BulletDamage);
	if (Pattern == TEXT("radial") || Pattern == TEXT("bellwave") || Pattern == TEXT("wailRings") ||
		Pattern == TEXT("geysers") || Pattern == TEXT("boilRain") || Pattern == TEXT("shockwave"))
	{
		Ring(Count, Phase.BulletSpeed, Damage);
	}
	else if (Pattern == TEXT("shotgunBurst") || Pattern == TEXT("steamCone") || Pattern == TEXT("crossbeams") ||
		Pattern == TEXT("waterjet") || Pattern == TEXT("cablewhip"))
	{
		Fan(70.f, Count, Phase.BulletSpeed, Damage);
	}
	else if (Pattern == TEXT("lasergrid") || Pattern == TEXT("spireLasers") || Pattern == TEXT("rails"))
	{
		for (int32 i = 0; i < 4; ++i)
		{
			FireEnemyShot(Enemy.Pos, FVector2D(FMath::Cos(i * PI / 2.f + ElapsedTime * 0.5f),
				FMath::Sin(i * PI / 2.f + ElapsedTime * 0.5f)), Phase.BulletSpeed * 1.8f, Damage * 1.2f, Color, 7.f);
		}
	}
	else if (Pattern == TEXT("orbitMines"))
	{
		Ring(FMath::Max(4, Count / 2), Phase.BulletSpeed * 0.45f, Damage * 0.7f);
	}
	else if (Pattern == TEXT("stoneRain"))
	{
		for (int32 i = 0; i < Count; ++i)
		{
			FVector2D Target = (Player ? Player->GetArenaPos() : Enemy.Pos) +
				FVector2D(Rng.Range(-260.f, 260.f), Rng.Range(-260.f, 260.f));
			FireEnemyShot(Enemy.Pos, (Target - Enemy.Pos).GetSafeNormal(), Phase.BulletSpeed, Damage, Color, 9.f);
		}
	}
	else if (Pattern == TEXT("charge"))
	{
		Enemy.ChargeTimer = 0.9f;
		Enemy.ChargeDir = Aim;
	}
	else
	{
		Fan(40.f, Count, Phase.BulletSpeed, Damage);
	}

	if (!Phase.Summon.IsEmpty())
	{
		SummonForBoss(Enemy, Phase.Summon);
	}
	UE_LOG(LogWS2Crowd, Verbose, TEXT("BOSS ATTACK id=%s phase=%d pattern=%s n=%d"),
		*Def.Id, PhaseIndex + 1, *Pattern, Count);
}

void AWS2Crowd::SummonForBoss(const FEnemy& Enemy, const FString& SummonId)
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const WS2::FEnemyDef* Minion = Db.FindEnemy(SummonId);
	if (!Minion)
	{
		Minion = Db.FindEnemy(TEXT("runner"));
	}
	if (!Minion)
	{
		return;
	}
	for (int32 i = 0; i < 2; ++i)
	{
		const FVector2D Offset(Rng.Range(-120.f, 120.f), Rng.Range(-120.f, 120.f));
		SpawnEnemy(*Minion, Enemy.Pos + Offset);
	}
}

void AWS2Crowd::FireEnemyShot(const FVector2D& From, const FVector2D& Dir, float Speed, float Damage,
	const FLinearColor& Color, float Radius)
{
	if (!Arena)
	{
		return;
	}
	FEnemyShot Shot;
	Shot.Pos = From;
	Shot.Vel = Dir.GetSafeNormal() * FMath::Max(60.f, Speed);
	Shot.Life = 2.4f;
	Shot.Radius = Radius;
	Shot.Damage = Damage;
	Shot.Color = Color;
	EnemyProjectiles.Add(Shot);
}

int32 AWS2Crowd::DamageArc(const FVector2D& From, const FVector2D& Dir, float Range, float ArcDegrees,
	float Damage, bool bCrit, const FString& Element, float SlowTime, float BurnDps, float StunTime,
	float ArmorPierce, float Knockback)
{
	int32 Hits = 0;
	const FVector2D Aim = Dir.GetSafeNormal();
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(ArcDegrees, 5.f, 360.f) * 0.5f));
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		FEnemy& Enemy = Enemies[Index];
		if (!Enemy.bAlive)
		{
			continue;
		}
		const FVector2D Delta = Enemy.Pos - From;
		const float Dist = Delta.Size();
		if (Dist > Range + Enemy.Radius)
		{
			continue;
		}
		if (Dist > 1.f && FVector2D::DotProduct(Delta / Dist, Aim) < CosHalf)
		{
			continue;
		}
		DamageEnemy(Index, Damage, bCrit, Aim, Knockback, Element, SlowTime, BurnDps, StunTime, ArmorPierce);
		++Hits;
	}
	return Hits;
}

int32 AWS2Crowd::DamageLine(const FVector2D& From, const FVector2D& Dir, float Range, float Damage, bool bCrit,
	const FString& Element, float SlowTime, float BurnDps, float StunTime, float ArmorPierce)
{
	int32 Hits = 0;
	const FVector2D Aim = Dir.GetSafeNormal();
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		FEnemy& Enemy = Enemies[Index];
		if (!Enemy.bAlive)
		{
			continue;
		}
		const FVector2D Delta = Enemy.Pos - From;
		const float Along = FVector2D::DotProduct(Delta, Aim);
		if (Along < 0.f || Along > Range)
		{
			continue;
		}
		const FVector2D Perp = Delta - Aim * Along;
		if (Perp.Size() > 28.f + Enemy.Radius)
		{
			continue;
		}
		DamageEnemy(Index, Damage, bCrit, Aim, 0.f, Element, SlowTime, BurnDps, StunTime, ArmorPierce);
		++Hits;
	}
	return Hits;
}

int32 AWS2Crowd::DamageArea(const FVector2D& Center, float Radius, float Damage, bool bCrit,
	const FString& Element, float SlowTime, float BurnDps, float StunTime, float ArmorPierce)
{
	int32 Hits = 0;
	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		FEnemy& Enemy = Enemies[Index];
		if (!Enemy.bAlive)
		{
			continue;
		}
		const FVector2D Delta = Enemy.Pos - Center;
		if (Delta.Size() > Radius + Enemy.Radius)
		{
			continue;
		}
		DamageEnemy(Index, Damage, bCrit, Delta.GetSafeNormal(), 0.f, Element, SlowTime, BurnDps, StunTime, ArmorPierce);
		++Hits;
	}
	return Hits;
}

int32 AWS2Crowd::DamageChain(const FVector2D& From, float Damage, int32 Links, float ChainRange, bool bCrit,
	const FString& Element, float StunTime, float ArmorPierce)
{
	int32 Hits = 0;
	FVector2D Cursor = From;
	TSet<int32> AlreadyHit;
	for (int32 Link = 0; Link < FMath::Max(1, Links); ++Link)
	{
		int32 Best = INDEX_NONE;
		float BestDistSq = ChainRange * ChainRange;
		for (int32 Index = 0; Index < Enemies.Num(); ++Index)
		{
			const FEnemy& Enemy = Enemies[Index];
			if (!Enemy.bAlive || AlreadyHit.Contains(Index))
			{
				continue;
			}
			const float DistSq = FVector2D::DistSquared(Cursor, Enemy.Pos);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Index;
			}
		}
		if (Best == INDEX_NONE)
		{
			break;
		}
		AlreadyHit.Add(Best);
		DamageEnemy(Best, Damage * FMath::Pow(0.82f, static_cast<float>(Link)), bCrit,
			(Enemies[Best].Pos - Cursor).GetSafeNormal(), 0.f, Element, 0.f, 0.f, StunTime, ArmorPierce);
		Cursor = Enemies[Best].Pos;
		++Hits;
	}
	return Hits;
}

void AWS2Crowd::HealEnemiesNear(const FVector2D& Center, float Radius, float Amount)
{
	for (FEnemy& Enemy : Enemies)
	{
		if (!Enemy.bAlive)
		{
			continue;
		}
		if (FVector2D::Distance(Center, Enemy.Pos) <= Radius + Enemy.Radius)
		{
			Enemy.Hp = FMath::Min(Enemy.MaxHp, Enemy.Hp + Amount);
		}
	}
}

FVector2D AWS2Crowd::DepenetrateSpawn(const FVector2D& Wanted, float Radius)
{
	FVector2D Pos = Wanted;
	TArray<int32> Near;
	const float Lookup = Radius + MaxEnemyRadius;

	for (int32 Attempt = 0; Attempt < 8; ++Attempt)
	{
		Grid.Query(Pos, Lookup, Near);
		FVector2D Push = FVector2D::ZeroVector;
		bool bBlocked = false;
		for (int32 Other : Near)
		{
			if (!Enemies.IsValidIndex(Other) || !Enemies[Other].bAlive)
			{
				continue;
			}

			FVector2D Delta = Pos - Enemies[Other].Pos;
			float Gap = Delta.Size();
			const float WantedGap = (Radius + Enemies[Other].Radius) * 1.02f;
			if (Gap >= WantedGap)
			{
				continue;
			}

			bBlocked = true;
			if (Gap < 1.f)
			{
				Delta = FVector2D(Rng.Range(-1.f, 1.f), Rng.Range(-1.f, 1.f)).GetSafeNormal();
				Gap = 1.f;
			}
			Push += (Delta / Gap) * (WantedGap - Gap + 0.5f);
		}

		if (!bBlocked)
		{
			break;
		}
		Pos += Push;
		Arena->ResolveCircle(Pos, Radius);
	}
	return Pos;
}

void AWS2Crowd::ConstrainToPlayer(FEnemy& Enemy) const
{
	if (!Player)
	{
		return;
	}

	const FVector2D PlayerPos = Player->GetArenaPos();
	const float MinDist = Enemy.Radius + Player->GetRadius();
	FVector2D Away = Enemy.Pos - PlayerPos;
	const float AwayDist = Away.Size();
	if (AwayDist >= MinDist)
	{
		return;
	}

	Away = AwayDist > 1.f ? Away / AwayDist : FVector2D(1.f, 0.f);
	Enemy.Pos = PlayerPos + Away * MinDist;
	Arena->ResolveCircle(Enemy.Pos, Enemy.Radius);
}

void AWS2Crowd::ResolveOverlaps()
{
	for (int32 Pass = 0; Pass < MaxOverlapPasses; ++Pass)
	{
		if (Pass > 0)
		{
			RebuildGrid();
		}
		ResolveOverlapsOnce();
		if (OverlapPairs == 0)
		{
			break;
		}
	}

	RebuildGrid();
	MeasureOverlaps();
}

void AWS2Crowd::ResolveOverlapsOnce()
{
	TArray<int32> Neighbors;
	WorstOverlap = 0.f;
	OverlapPairs = 0;

	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		FEnemy& Enemy = Enemies[Index];
		if (!Enemy.bAlive)
		{
			continue;
		}

		Grid.Query(Enemy.Pos, Enemy.Radius + MaxEnemyRadius, Neighbors);
		if (Player && !Enemy.bFlying)
		{
			const float PlayerRadius = Player->GetRadius();
			FVector2D ToPlayer = Enemy.Pos - Player->GetArenaPos();
			float PlayerGap = ToPlayer.Size();
			const float PlayerWanted = Enemy.Radius + PlayerRadius;
			if (PlayerGap < PlayerWanted)
			{
				if (PlayerGap < 0.01f)
				{
					ToPlayer = FVector2D(1.f, 0.f);
					PlayerGap = 0.01f;
				}
				WorstOverlap = FMath::Max(WorstOverlap, PlayerWanted - PlayerGap);
				++OverlapPairs;
				Enemy.Pos += (ToPlayer / PlayerGap) * (PlayerWanted - PlayerGap);
			}
		}

		for (int32 Other : Neighbors)
		{
			if (Other <= Index || !Enemies.IsValidIndex(Other) || !Enemies[Other].bAlive)
			{
				continue;
			}

			FEnemy& Mate = Enemies[Other];
			FVector2D Delta = Enemy.Pos - Mate.Pos;
			float Gap = Delta.Size();
			const float Wanted = (Enemy.Radius + Mate.Radius) * 0.98f;
			if (Gap >= Wanted)
			{
				continue;
			}

			if (Gap < 0.01f)
			{
				Delta = FVector2D(Rng.Range(-1.f, 1.f), Rng.Range(-1.f, 1.f)).GetSafeNormal();
				Gap = 0.01f;
			}

			const float Overlap = Wanted - Gap;
			WorstOverlap = FMath::Max(WorstOverlap, Overlap);
			++OverlapPairs;

			const FVector2D Correction = (Delta / Gap) * (Overlap * 0.5f);
			Enemy.Pos += Correction;
			Mate.Pos -= Correction;
		}
	}

	for (FEnemy& Enemy : Enemies)
	{
		if (Enemy.bAlive)
		{
			Arena->ResolveCircle(Enemy.Pos, Enemy.Radius);
		}
	}
}

void AWS2Crowd::MeasureOverlaps()
{
	OverlapPairs = 0;
	WorstOverlap = 0.f;

	TArray<int32> Neighbors;
	const FVector2D PlayerPos = Player ? Player->GetArenaPos() : FVector2D::ZeroVector;
	const float PlayerRadius = Player ? Player->GetRadius() : 0.f;

	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		const FEnemy& Enemy = Enemies[Index];
		if (!Enemy.bAlive)
		{
			continue;
		}

		if (Player)
		{
			const float PlayerGap = FVector2D::Distance(Enemy.Pos, PlayerPos);
			const float Wanted = Enemy.Radius + PlayerRadius;
			if (PlayerGap < Wanted - 0.05f)
			{
				WorstOverlap = FMath::Max(WorstOverlap, Wanted - PlayerGap);
				++OverlapPairs;
			}
		}

		Grid.Query(Enemy.Pos, Enemy.Radius + MaxEnemyRadius, Neighbors);
		for (int32 Other : Neighbors)
		{
			if (Other <= Index || !Enemies.IsValidIndex(Other) || !Enemies[Other].bAlive)
			{
				continue;
			}
			const float Gap = FVector2D::Distance(Enemy.Pos, Enemies[Other].Pos);
			const float Wanted = (Enemy.Radius + Enemies[Other].Radius) * 0.98f;
			if (Gap < Wanted - 0.05f)
			{
				WorstOverlap = FMath::Max(WorstOverlap, Wanted - Gap);
				++OverlapPairs;
			}
		}
	}
}

void AWS2Crowd::UpdateProjectiles(float Dt)
{
	TArray<int32> Candidates;
	const FVector2D Extent = Arena->GetExtent();

	for (int32 Index = Projectiles.Num() - 1; Index >= 0; --Index)
	{
		FProjectile& Projectile = Projectiles[Index];
		Projectile.Pos += Projectile.Vel * Dt;
		Projectile.Life -= Dt;

		bool bDead = Projectile.Life <= 0.f || Projectile.Pos.X < 0.f || Projectile.Pos.Y < 0.f ||
			Projectile.Pos.X > Extent.X || Projectile.Pos.Y > Extent.Y;

		if (!bDead)
		{
			Grid.Query(Projectile.Pos, Projectile.Radius + 62.f, Candidates);
			for (int32 Candidate : Candidates)
			{
				if (!Enemies.IsValidIndex(Candidate) || !Enemies[Candidate].bAlive)
				{
					continue;
				}
				if (Projectile.Hits.Contains(Candidate))
				{
					continue;
				}

				const FEnemy& Enemy = Enemies[Candidate];
				const float Reach = Projectile.Radius + Enemy.Radius;
				if (FVector2D::DistSquared(Projectile.Pos, Enemy.Pos) > Reach * Reach)
				{
					continue;
				}

				const FVector2D Dir = Projectile.Vel.GetSafeNormal();
				Projectile.Hits.Add(Candidate);
				DamageEnemy(Candidate, Projectile.Damage, Projectile.bCrit, Dir, Projectile.Knockback,
					Projectile.Element, Projectile.SlowTime, Projectile.BurnDps, Projectile.StunTime,
					Projectile.ArmorPierce);

				if (Projectile.Lifesteal > 0.f && Player)
				{
					Player->Heal(Projectile.Damage * Projectile.Lifesteal * 0.01f);
				}
				if (Projectile.Chain > 0.f)
				{
					DamageChain(Enemy.Pos, Projectile.Damage * 0.7f,
						FMath::RoundToInt(Projectile.Chain), Projectile.ChainRange, false,
						Projectile.Element, Projectile.StunTime, Projectile.ArmorPierce);
				}
				if (Projectile.BlastRadius > 0.f)
				{
					DamageArea(Enemy.Pos, Projectile.BlastRadius, Projectile.Damage * 0.6f, false,
						Projectile.Element, Projectile.SlowTime, Projectile.BurnDps, 0.f, 0.f);
				}

				if (Projectile.PierceLeft <= 0)
				{
					bDead = true;
					break;
				}
				--Projectile.PierceLeft;
			}
		}

		if (bDead)
		{
			Projectiles.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}

void AWS2Crowd::UpdateEnemyProjectiles(float Dt)
{
	if (!Player)
	{
		return;
	}
	const FVector2D PlayerPos = Player->GetArenaPos();
	const float PlayerRadius = Player->GetRadius();
	const FVector2D Extent = Arena->GetExtent();

	for (int32 Index = EnemyProjectiles.Num() - 1; Index >= 0; --Index)
	{
		FEnemyShot& Shot = EnemyProjectiles[Index];
		Shot.Pos += Shot.Vel * Dt;
		Shot.Life -= Dt;

		bool bDead = Shot.Life <= 0.f || Shot.Pos.X < 0.f || Shot.Pos.Y < 0.f ||
			Shot.Pos.X > Extent.X || Shot.Pos.Y > Extent.Y;
		if (!bDead && FVector2D::Distance(Shot.Pos, PlayerPos) < PlayerRadius + Shot.Radius)
		{
			Player->ApplyContactDamage(Shot.Damage);
			bDead = true;
		}
		if (bDead)
		{
			EnemyProjectiles.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}

void AWS2Crowd::UpdatePickups(float Dt)
{
	if (!Player)
	{
		return;
	}

	const FVector2D PlayerPos = Player->GetArenaPos();
	for (int32 Index = Pickups.Num() - 1; Index >= 0; --Index)
	{
		FPickup& Pickup = Pickups[Index];
		Pickup.Life -= Dt;

		const FVector2D Delta = PlayerPos - Pickup.Pos;
		const float Dist = Delta.Size();
		if (Dist < 280.f && Dist > 1.f)
		{
			Pickup.Pos += (Delta / Dist) * FMath::Min(900.f * Dt, Dist);
		}

		if (Dist < 55.f)
		{
			if (GameMode)
			{
				GameMode->AddMaterials(Pickup.Amount);
			}
			Pickups.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}

		if (Pickup.Life <= 0.f)
		{
			Pickups.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}

void AWS2Crowd::DamageEnemy(int32 Index, float Damage, bool bCrit, const FVector2D& Dir, float Knockback,
	const FString& Element, float SlowTime, float BurnDps, float StunTime, float ArmorPierce)
{
	if (!Enemies.IsValidIndex(Index))
	{
		return;
	}

	FEnemy& Enemy = Enemies[Index];
	if (!Enemy.bAlive)
	{
		return;
	}

	float Remaining = FMath::Max(0.f, Damage);
	if (Enemy.Shield > 0.f)
	{
		const float Absorbed = FMath::Min(Enemy.Shield, Remaining);
		Enemy.Shield -= Absorbed;
		Remaining -= Absorbed;
	}

	const float EffectiveArmor = FMath::Max(0.f, Enemy.Armor * (1.f - FMath::Clamp(ArmorPierce, 0.f, 0.9f)));
	const float Applied = Remaining > 0.f ? FMath::Max(0.f, Remaining - EffectiveArmor * 0.5f) : 0.f;
	Enemy.Hp -= Applied;
	Enemy.Flash = 0.14f;
	++ProjectileHits;

	if (Element == TEXT("fire")) { Enemy.BurnTime = FMath::Max(Enemy.BurnTime, 2.2f); Enemy.BurnDps = FMath::Max(Enemy.BurnDps, (BurnDps > 0.f ? BurnDps : 3.f)); }
	else if (Element == TEXT("ice")) { Enemy.SlowTime = FMath::Max(Enemy.SlowTime, SlowTime > 0.f ? SlowTime : 1.6f); }
	else if (Element == TEXT("shock")) { Enemy.ShockTime = FMath::Max(Enemy.ShockTime, StunTime > 0.f ? StunTime : 0.35f); }
	else if (Element == TEXT("poison")) { Enemy.BurnTime = FMath::Max(Enemy.BurnTime, 3.0f); Enemy.BurnDps = FMath::Max(Enemy.BurnDps, (BurnDps > 0.f ? BurnDps : 2.f)); }
	else if (Element == TEXT("boom")) { Enemy.SlowTime = FMath::Max(Enemy.SlowTime, 0.4f); }

	if (Enemy.bElite && PendingPunch <= 0.f)
	{
		PendingPunch = 0.45f;
	}

	if (Knockback > 0.f)
	{
		Enemy.Pos += Dir.GetSafeNormal() * Knockback * 0.035f;
	}
	if (bCrit)
	{
		PendingHitStop = FMath::Max(PendingHitStop, 0.02f);
	}

	if (Enemy.Hp <= 0.f)
	{
		KillEnemy(Index);
	}
}

void AWS2Crowd::KillEnemy(int32 Index)
{
	if (!Enemies.IsValidIndex(Index))
	{
		return;
	}
	FEnemy& Enemy = Enemies[Index];
	if (!Enemy.bAlive)
	{
		return;
	}
	Enemy.bAlive = false;
	--AliveCount;
	++KillCount;
	FreeSlots.AddUnique(Index);

	if (Enemy.bBoss)
	{
		WS2::FDatabase& Db = WS2::FDatabase::Get();
		if (Db.Bosses.IsValidIndex(Enemy.BossIndex))
		{
			const WS2::FBossDef& Def = Db.Bosses[Enemy.BossIndex];
			if (FBossRuntime* Runtime = BossRuntimes.FindByPredicate(
				[&Def](const FBossRuntime& Candidate)
				{
					return WS2::FDatabase::Get().Bosses.IsValidIndex(Candidate.BossDefIndex) &&
						WS2::FDatabase::Get().Bosses[Candidate.BossDefIndex].Id == Def.Id && !Candidate.bDefeated;
				}))
			{
				Runtime->bDefeated = true;
				Runtime->Phase = Enemy.Phase;
				Runtime->Hp = 0.f;
				Runtime->PhaseReached.AddUnique(Enemy.Phase);
			}
			UE_LOG(LogWS2Crowd, Display, TEXT("BOSS DEFEATED id=%s name=%s phase=%d attacks=%d"),
				*Def.Id, *Def.Name, Enemy.Phase + 1,
				[this, &Def]()
				{
					for (const FBossRuntime& Runtime : BossRuntimes)
					{
						if (WS2::FDatabase::Get().Bosses.IsValidIndex(Runtime.BossDefIndex) &&
							WS2::FDatabase::Get().Bosses[Runtime.BossDefIndex].Id == Def.Id)
						{
							return Runtime.Attacks;
						}
					}
					return 0;
				}());
			PendingHitStop = FMath::Max(PendingHitStop, 0.22f);
			PendingShakeAt = Enemy.Pos;
			PendingShakeAmount = FMath::Max(PendingShakeAmount, 24.f);
			PendingPunch = FMath::Max(PendingPunch, 0.9f);
			if (GameMode)
			{
				GameMode->AddKill(Def.Mat, Def.Xp);
			}
		}
		return;
	}

	const WS2::FEnemyDef& Def = WS2::FDatabase::Get().EnemyByIndex(Enemy.DefIndex);
	if (KillCounts.IsValidIndex(Enemy.DefIndex))
	{
		++KillCounts[Enemy.DefIndex];
	}

	FPickup Pickup;
	Pickup.Pos = Enemy.Pos;
	Pickup.Amount = FMath::Max(1, Def.Mat);
	Pickup.Life = 22.f;
	Pickups.Add(Pickup);

	if (Enemy.bElite)
	{
		PendingHitStop = FMath::Max(PendingHitStop, 0.12f);
		PendingShakeAt = Enemy.Pos;
		PendingShakeAmount = FMath::Max(PendingShakeAmount, 18.f);
		PendingPunch = FMath::Max(PendingPunch, 0.8f);
	}
	else
	{
		PendingShakeAt = Enemy.Pos;
		PendingShakeAmount = FMath::Max(PendingShakeAmount, 3.f);
	}

	if (GameMode)
	{
		GameMode->AddKill(Def.Mat, Def.Xp);
	}
}

FString AWS2Crowd::RenderDiag() const
{
	int32 Instances = 0;
	int32 BossInstances = 0;
	for (const UInstancedStaticMeshComponent* Mesh : EnemyMeshes)
	{
		if (Mesh)
		{
			Instances += Mesh->GetInstanceCount();
		}
	}
	for (const UInstancedStaticMeshComponent* Mesh : BossMeshes)
	{
		if (Mesh)
		{
			BossInstances += Mesh->GetInstanceCount();
		}
	}

	FString MeshPath = TEXT("none");
	FString MatPath = TEXT("none");
	FString Param = TEXT("n/a");
	if (EnemyMeshes.Num() > 0 && EnemyMeshes[0])
	{
		MeshPath = EnemyMeshes[0]->GetStaticMesh() ? EnemyMeshes[0]->GetStaticMesh()->GetPathName() : TEXT("no-mesh");
		if (const UMaterialInterface* Bound = EnemyMeshes[0]->GetMaterial(0))
		{
			MatPath = Bound->GetPathName();
			if (const UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Bound))
			{
				FLinearColor Value;
				if (Mid->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Value))
				{
					Param = Value.ToString();
				}
			}
		}
	}

	FString Sample = TEXT("none");
	for (const FEnemy& Enemy : Enemies)
	{
		if (Enemy.bAlive)
		{
			const FVector2D World = Arena ? Arena->ArenaToWorld(Enemy.Pos) : Enemy.Pos;
			Sample = FString::Printf(TEXT("def=%d boss=%d pos=(%.0f,%.0f) r=%.0f"),
				Enemy.DefIndex, Enemy.bBoss ? 1 : 0, World.X, World.Y, Enemy.Radius);
			break;
		}
	}

	int32 Near = 0;
	int32 Mid = 0;
	int32 Far = 0;
	float DistSum = 0.f;
	const FVector2D PlayerPos = Player ? Player->GetArenaPos() : FVector2D::ZeroVector;
	for (const FEnemy& Enemy : Enemies)
	{
		if (!Enemy.bAlive)
		{
			continue;
		}
		const float Dist = FVector2D::Distance(Enemy.Pos, PlayerPos);
		DistSum += Dist;
		if (Dist < 400.f) ++Near;
		else if (Dist < 800.f) ++Mid;
		else ++Far;
	}
	const float AvgDist = AliveCount > 0 ? DistSum / AliveCount : 0.f;

	return FString::Printf(
		TEXT("meshes=%d bossMeshes=%d instances=%d bossInstances=%d mesh=%s mat=%s color=%s near=%d mid=%d far=%d avgDist=%.0f ")
		TEXT("overlapPairs=%d worstOverlap=%.2f fires=%d hits=%d kills=%d enemyShots=%d sample=%s"),
		EnemyMeshes.Num(), BossMeshes.Num(), Instances, BossInstances, *MeshPath, *MatPath, *Param,
		Near, Mid, Far, AvgDist, OverlapPairs, WorstOverlap, ProjectileFires, ProjectileHits, KillCount,
		EnemyProjectiles.Num(), *Sample);
}

void AWS2Crowd::UpdateRender()
{
	if (!Arena)
	{
		return;
	}

	for (UInstancedStaticMeshComponent* Mesh : EnemyMeshes)
	{
		if (Mesh)
		{
			Mesh->ClearInstances();
		}
	}
	for (UInstancedStaticMeshComponent* Mesh : BossMeshes)
	{
		if (Mesh)
		{
			Mesh->ClearInstances();
		}
	}

	for (int32 Index = 0; Index < Enemies.Num(); ++Index)
	{
		const FEnemy& Enemy = Enemies[Index];
		if (!Enemy.bAlive)
		{
			continue;
		}

		const FVector2D World = Arena->ArenaToWorld(Enemy.Pos);
		const float Pulse = 1.f + Enemy.Flash * 1.8f;
		if (Enemy.bBoss)
		{
			const int32* MeshIndex = BossToMesh.Find(Enemy.BossIndex);
			const float* Scale = BossScale.Find(Enemy.BossIndex);
			if (!MeshIndex || !BossMeshes.IsValidIndex(*MeshIndex) || !BossMeshes[*MeshIndex])
			{
				continue;
			}
			const float UseScale = Pulse * (Scale ? *Scale : 1.f);
			BossMeshes[*MeshIndex]->AddInstance(FTransform(FRotator(0.f, ElapsedTime * 12.f, 0.f),
				FVector(World.X, World.Y, 20.f), FVector(UseScale, UseScale, UseScale)), false);
			continue;
		}

		const int32* MeshIndexPtr = DefToMesh.Find(Enemy.DefIndex);
		if (!MeshIndexPtr || !EnemyMeshes.IsValidIndex(*MeshIndexPtr) || !EnemyMeshes[*MeshIndexPtr])
		{
			continue;
		}
		const float* FoundScale = DefScale.Find(Enemy.DefIndex);
		const float Scale = Pulse * (FoundScale ? *FoundScale : 1.f);
		EnemyMeshes[*MeshIndexPtr]->AddInstance(FTransform(FRotator::ZeroRotator,
			FVector(World.X, World.Y, 0.f), FVector(Scale, Scale, Scale)), false);
	}

	EnsureProjectileMesh();
	ProjectileMesh->ClearInstances();
	for (const FProjectile& Projectile : Projectiles)
	{
		const FVector2D World = Arena->ArenaToWorld(Projectile.Pos);
		const float Scale = Projectile.Radius * 2.f / 100.f;
		ProjectileMesh->AddInstance(FTransform(FRotator::ZeroRotator,
			FVector(World.X, World.Y, 42.f), FVector(Scale, Scale, Scale)), false);
	}

	EnsureEnemyShotMesh();
	EnemyShotMesh->ClearInstances();
	for (const FEnemyShot& Shot : EnemyProjectiles)
	{
		const FVector2D World = Arena->ArenaToWorld(Shot.Pos);
		const float Scale = Shot.Radius * 2.f / 100.f;
		EnemyShotMesh->AddInstance(FTransform(FRotator::ZeroRotator,
			FVector(World.X, World.Y, 38.f), FVector(Scale, Scale, Scale)), false);
	}

	EnsurePickupMesh();
	PickupMesh->ClearInstances();
	for (const FPickup& Pickup : Pickups)
	{
		const FVector2D World = Arena->ArenaToWorld(Pickup.Pos);
		PickupMesh->AddInstance(FTransform(FRotator::ZeroRotator,
			FVector(World.X, World.Y, 18.f), FVector(0.22f, 0.22f, 0.12f)), false);
	}
}
