#include "WS2PlayerPawn.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Parse.h"
#include "WS2Arena.h"
#include "WS2Crowd.h"
#include "WS2GameMode.h"
#include "WS2WeaponComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Player, Log, All);

namespace
{
	FLinearColor PlayerHex(const FString& Hex, const FLinearColor& Fallback)
	{
		FString Value = Hex.TrimStartAndEnd();
		if (Value.StartsWith(TEXT("#")))
		{
			Value = Value.RightChop(1);
		}
		if (Value.Len() >= 6)
		{
			return FLinearColor(
				FParse::HexNumber(*Value.Mid(0, 2)) / 255.f,
				FParse::HexNumber(*Value.Mid(2, 2)) / 255.f,
				FParse::HexNumber(*Value.Mid(4, 2)) / 255.f);
		}
		return Fallback;
	}
}

AWS2PlayerPawn::AWS2PlayerPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(Radius);
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetRootComponent(Collision);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Collision);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);

	Weapon = CreateDefaultSubobject<UWS2WeaponComponent>(TEXT("Weapon"));
}

void AWS2PlayerPawn::BeginPlay()
{
	Super::BeginPlay();

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	Db.EnsureLoaded();

	const WS2::FHeroDef* Hero = Db.FindHero(TEXT("leonidas"));
	const FString HeroId = Hero ? Hero->Id : TEXT("leonidas");

	MaxHp = 100.f + (Hero ? Hero->Stat(TEXT("maxHp")) * 10.f : 100.f);
	Hp = MaxHp;
	HpRegen = Hero ? Hero->Stat(TEXT("hpRegen")) * 0.4f : 0.f;
	AttackSpeedPct = Hero ? Hero->Stat(TEXT("atkSpd")) : 0.f;
	DamagePct = Hero ? Hero->Stat(TEXT("dmgP")) : 0.f;
	RangedPct = Hero ? Hero->Stat(TEXT("ranged")) : 0.f;
	CritPct = Hero ? Hero->Stat(TEXT("crit")) : 0.f;
	RangePct = Hero ? Hero->Stat(TEXT("range")) : 0.f;
	Armor = Hero ? Hero->Stat(TEXT("armor")) : 0.f;
	SpeedPct = Hero ? Hero->Stat(TEXT("speed")) : 0.f;
	CritDmgPct = Hero ? Hero->Stat(TEXT("critDmg")) : 0.f;
	MeleePct = Hero ? Hero->Stat(TEXT("melee")) : 0.f;
	ElementPct = Hero ? Hero->Stat(TEXT("elem")) : 0.f;
	KnockbackPct = Hero ? Hero->Stat(TEXT("knock")) : 0.f;
	LifestealPct = Hero ? Hero->Stat(TEXT("lifesteal")) : 0.f;
	DodgePct = Hero ? Hero->Stat(TEXT("dodge")) : 0.f;
	Luck = Hero ? Hero->Stat(TEXT("luck")) : 0.f;
	Harvest = Hero ? Hero->Stat(TEXT("harvest")) : 0.f;
	XpGainPct = Hero ? Hero->Stat(TEXT("xpGain")) : 0.f;
	ExplosionSizePct = Hero ? Hero->Stat(TEXT("expSize")) : 0.f;
	PierceBonus = Hero ? Hero->Stat(TEXT("pierce")) : 0.f;
	BounceBonus = Hero ? Hero->Stat(TEXT("bounce")) : 0.f;
	Engineering = Hero ? Hero->Stat(TEXT("eng")) : 0.f;

	UStaticMesh* HeroMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Heroes/Slice/SM_Leonidas.SM_Leonidas"));
	if (!HeroMesh)
	{
		HeroMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Art/Heroes/SM_Leonidas.SM_Leonidas"));
	}
	const bool bImported = HeroMesh != nullptr;
	if (!HeroMesh)
	{
		HeroMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	}
	Body->SetStaticMesh(HeroMesh);
	// Vertex-animation data is baked for LOD0; never let distance-based LOD
	// selection swap in a mesh whose vertex count does not match the atlas.
	Body->SetForcedLodModel(1);
	if (bImported)
	{
		// Normalise whatever scale the FBX came in at: the hero should stand ~1.1 m tall.
		const FBoxSphereBounds Bounds = HeroMesh->GetBounds();
		const float SourceHeight = FMath::Max(1.f, Bounds.BoxExtent.Z * 2.f);
		const float UniformScale = 110.f / SourceHeight;
		Body->SetRelativeScale3D(FVector(UniformScale));
		Body->SetRelativeLocation(FVector(0.f, 0.f, -Bounds.Origin.Z * UniformScale + Bounds.BoxExtent.Z * UniformScale));
	}
	else
	{
		Body->SetRelativeLocation(FVector(0.f, 0.f, 42.f));
		Body->SetRelativeScale3D(FVector(0.6f, 0.6f, 0.9f));
	}

	HeroVat = Db.FindVat(TEXT("SM_Leonidas"));
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/M_WS2VAT.M_WS2VAT")))
	{
		BodyMid = UMaterialInstanceDynamic::Create(Base, Body);
		UTexture* Atlas = LoadObject<UTexture>(nullptr, TEXT("/Game/Art/VAT/SM_Leonidas_VAT.SM_Leonidas_VAT"));
		if (Atlas)
		{
			BodyMid->SetTextureParameterValue(TEXT("VAT"), Atlas);
		}

		const FLinearColor Color = PlayerHex(Hero ? Hero->Color : TEXT("#ffcf4a"), FLinearColor(1.f, 0.81f, 0.29f));
		FLinearColor Tint = Color * 0.55f;
		Tint.A = 1.f;
		BodyMid->SetVectorParameterValue(TEXT("Color"), Tint);
		BodyMid->SetVectorParameterValue(TEXT("VATExtent"),
			HeroVat ? HeroVat->Extent : FVector(70.f, 70.f, 140.f));
		BodyMid->SetScalarParameterValue(TEXT("VATFrames"), HeroVat ? static_cast<float>(HeroVat->Frames) : 44.f);
		BodyMid->SetScalarParameterValue(TEXT("VATFps"), HeroVat ? HeroVat->Fps : 24.f);
		BodyMid->SetScalarParameterValue(TEXT("VATRandomize"), 0.f);
		// The rig exports one slot per Blender material and the combined LOD FBX
		// repeats those slots per level; cover every slot so no section falls back
		// to the default world material.
		const int32 NumSlots = Body->GetStaticMesh() ? Body->GetStaticMesh()->GetStaticMaterials().Num() : 0;
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			Body->SetMaterial(Slot, BodyMid);
		}
		SetHeroClip(TEXT("idle"));
	}

	// The slice hero carries the service pistol: real projectiles, real hits.
	if (Weapon)
	{
		Weapon->SetWeapon(TEXT("pistol"));
	}

	UE_LOG(LogWS2Player, Display, TEXT("Player ready: hero=%s hp=%.0f weapon=%s mesh=%s vat=%s clips=%d"),
		*HeroId, MaxHp, Weapon ? *Weapon->GetWeaponId() : TEXT("none"), bImported ? TEXT("imported") : TEXT("blockout"),
		HeroVat ? TEXT("ok") : TEXT("missing"), HeroVat ? HeroVat->Clips.Num() : 0);
}

void AWS2PlayerPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (PlayerInputComponent)
	{
		PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AWS2PlayerPawn::MoveForward);
		PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AWS2PlayerPawn::MoveRight);
	}
}

void AWS2PlayerPawn::MoveForward(float Value)
{
	InputVector.Y += Value;
}

void AWS2PlayerPawn::MoveRight(float Value)
{
	InputVector.X += Value;
}

void AWS2PlayerPawn::SetHeroClip(const FString& Clip)
{
	if (!BodyMid || Clip == HeroClip)
	{
		return;
	}

	HeroClip = Clip;
	const FIntPoint Range = HeroVat ? HeroVat->Clip(Clip, FIntPoint(0, HeroVat->Frames)) : FIntPoint(0, 16);
	UE_LOG(LogWS2Player, Display, TEXT("HERO clip=%s vatFrames=%d..%d"), *Clip, Range.X, Range.X + Range.Y);
	BodyMid->SetScalarParameterValue(TEXT("VATStart"), static_cast<float>(Range.X));
	BodyMid->SetScalarParameterValue(TEXT("VATCount"), static_cast<float>(Range.Y));
}

void AWS2PlayerPawn::PlayAttackAnim()
{
	AttackAnimLeft = 0.42f;
	SetHeroClip(TEXT("attack"));
}

void AWS2PlayerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}

	FVector2D Direction = InputVector;
	if (Direction.SizeSquared() > 1.f)
	{
		Direction = Direction.GetSafeNormal();
	}

	const float Speed = 430.f * (1.f + SpeedPct / 100.f) * (bDown ? 0.35f : 1.f);
	FVector2D WorldPosition = GetPos2D() + Direction * Speed * DeltaSeconds;

	if (AWS2Arena* Arena = GameMode->GetArena())
	{
		FVector2D ArenaPosition = Arena->WorldToArena(WorldPosition);
		Arena->ResolveCircle(ArenaPosition, Radius);
		WorldPosition = Arena->ArenaToWorld(ArenaPosition);
	}
	SetActorLocation(FVector(WorldPosition.X, WorldPosition.Y, 42.f));

	if (HpRegen > 0.f && !bDown)
	{
		Hp = FMath::Min(MaxHp, Hp + HpRegen * DeltaSeconds);
	}

	FindTarget();

	// Clip switching: attack overrides locomotion, run overrides idle.
	if (AttackAnimLeft > 0.f)
	{
		AttackAnimLeft = FMath::Max(0.f, AttackAnimLeft - DeltaSeconds);
		if (AttackAnimLeft <= 0.f)
		{
			SetHeroClip(bWasMoving ? TEXT("run") : TEXT("idle"));
		}
	}
	else
	{
		const bool bMoving = Direction.SizeSquared() > 0.01f;
		if (bMoving != bWasMoving)
		{
			bWasMoving = bMoving;
			SetHeroClip(bMoving ? TEXT("run") : TEXT("idle"));
		}
	}

	// Face the current target (or movement direction when the arena is empty).
	if (Body)
	{
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(AimDir.Y, AimDir.X));
		Body->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
	}

	InputVector = FVector2D::ZeroVector;
}

void AWS2PlayerPawn::FindTarget()
{
	AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr;
	if (!GameMode || !GameMode->GetCrowd())
	{
		return;
	}

	FVector2D Target;
	const FVector2D ArenaPos = GetArenaPos();
	if (GameMode->GetCrowd()->FindNearestEnemy(ArenaPos, 1400.f, Target))
	{
		AimTarget = Target;
		const FVector2D Delta = Target - ArenaPos;
		if (Delta.SizeSquared() > 1.f)
		{
			AimDir = Delta.GetSafeNormal();
		}
	}
}

void AWS2PlayerPawn::ApplyItemStats(const TMap<FString, float>& Stats)
{
	for (const TPair<FString, float>& Pair : Stats)
	{
		const FString& Key = Pair.Key;
		const float Value = Pair.Value;
		if (Key == TEXT("speed")) SpeedPct += Value;
		else if (Key == TEXT("atkSpd")) AttackSpeedPct += Value;
		else if (Key == TEXT("armor")) Armor += Value;
		else if (Key == TEXT("maxHp")) { MaxHp += Value; Hp = FMath::Min(MaxHp, Hp + Value); }
		else if (Key == TEXT("hpRegen")) HpRegen += Value;
		else if (Key == TEXT("range")) RangePct += Value;
		else if (Key == TEXT("crit")) CritPct += Value;
		else if (Key == TEXT("eng")) Engineering += Value;
		else if (Key == TEXT("luck")) Luck += Value;
		else if (Key == TEXT("melee")) MeleePct += Value;
		else if (Key == TEXT("ranged")) RangedPct += Value;
		else if (Key == TEXT("dmgP")) DamagePct += Value;
		else if (Key == TEXT("knock")) KnockbackPct += Value;
		else if (Key == TEXT("dodge")) DodgePct += Value;
		else if (Key == TEXT("critDmg")) CritDmgPct += Value;
		else if (Key == TEXT("elem")) ElementPct += Value;
		else if (Key == TEXT("expSize")) ExplosionSizePct += Value;
		else if (Key == TEXT("harvest")) Harvest += Value;
		else if (Key == TEXT("bounce")) BounceBonus += Value;
		else if (Key == TEXT("pierce")) PierceBonus += Value;
		else if (Key == TEXT("lifesteal")) LifestealPct += Value;
		else if (Key == TEXT("xpGain")) XpGainPct += Value;
	}
	UE_LOG(LogWS2Player, Display,
		TEXT("ITEM stats applied: %d rows -> dmg=%.0f atkSpd=%.0f armor=%.0f crit=%.0f elem=%.0f hp=%.0f"),
		Stats.Num(), DamagePct, AttackSpeedPct, Armor, CritPct, ElementPct, MaxHp);
}

void AWS2PlayerPawn::ApplyContactDamage(float Amount)
{
	if (bDown || bInvulnerable || Amount <= 0.f)
	{
		return;
	}

	if (DodgePct > 0.f && FMath::FRand() * 100.f < FMath::Min(60.f, DodgePct))
	{
		return;
	}

	const float Mitigated = Amount * (100.f / (100.f + Armor * 12.f));
	Hp -= Mitigated;

	if (Hp <= 0.f)
	{
		Hp = 0.f;
		bDown = true;
		if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
		{
			GameMode->OnPlayerDown();
		}
	}
}

void AWS2PlayerPawn::Heal(float Amount)
{
	if (Amount <= 0.f)
	{
		return;
	}
	bDown = false;
	Hp = FMath::Min(MaxHp, Hp + Amount);
}

FVector2D AWS2PlayerPawn::GetPos2D() const
{
	const FVector Location = GetActorLocation();
	return FVector2D(Location.X, Location.Y);
}

FVector2D AWS2PlayerPawn::GetArenaPos() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const AWS2GameMode* GameMode = World->GetAuthGameMode<AWS2GameMode>())
		{
			if (const AWS2Arena* Arena = GameMode->GetArena())
			{
				return Arena->WorldToArena(GetPos2D());
			}
		}
	}
	return GetPos2D();
}
