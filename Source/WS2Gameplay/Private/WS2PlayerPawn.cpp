#include "WS2PlayerPawn.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "WS2Arena.h"
#include "WS2Audio.h"
#include "WS2Crowd.h"
#include "WS2GameMode.h"
#include "WS2PackageTrace.h"
#include "WS2PlayerState.h"
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
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(20.f);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(Radius);
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetRootComponent(Collision);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Collision);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);

	for (int32 Slot = 0; Slot < 6; ++Slot)
	{
		const FName ComponentName(*FString::Printf(TEXT("Weapon%d"), Slot));
		UWS2WeaponComponent* NewWeapon = CreateDefaultSubobject<UWS2WeaponComponent>(ComponentName);
		NewWeapon->SetAutoEquipDefault(Slot == 0);
		Weapons.Add(NewWeapon);
	}
	Weapon = Weapons.Num() > 0 ? Weapons[0] : nullptr;
}

void AWS2PlayerPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWS2PlayerPawn, HeroId);
	DOREPLIFETIME(AWS2PlayerPawn, Hp);
	DOREPLIFETIME(AWS2PlayerPawn, MaxHp);
	DOREPLIFETIME(AWS2PlayerPawn, AimDir);
	DOREPLIFETIME(AWS2PlayerPawn, bDown);
}

void AWS2PlayerPawn::BeginPlay()
{
	Super::BeginPlay();

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	Db.EnsureLoaded();

	ApplyHeroStats(SelectedHeroId.IsEmpty() ? TEXT("leonidas") : SelectedHeroId);
	BuildVisual();
	NetVerifyStartLocation = GetActorLocation();

	UE_LOG(LogWS2Player, Display, TEXT("Player ready: hero=%s hp=%.0f weapon=%s vat=%s clips=%d authority=%d"),
		*HeroId, MaxHp, Weapon ? *Weapon->GetWeaponId() : TEXT("none"),
		HeroVat ? TEXT("ok") : TEXT("missing"), HeroVat ? HeroVat->Clips.Num() : 0,
		HasAuthority() ? 1 : 0);
}

void AWS2PlayerPawn::ApplyHeroStats(const FString& InHeroId, bool bResetHealth)
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	Db.EnsureLoaded();

	const WS2::FHeroDef* Hero = Db.FindHero(InHeroId);
	if (!Hero)
	{
		Hero = Db.FindHero(TEXT("leonidas"));
	}
	HeroId = Hero ? Hero->Id : TEXT("leonidas");
	SelectedHeroId = HeroId;

	MaxHp = 100.f + (Hero ? Hero->Stat(TEXT("maxHp")) * 10.f : 0.f);
	Hp = bResetHealth ? MaxHp : FMath::Min(Hp, MaxHp);
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

	for (int32 Slot = 1; Slot < Weapons.Num(); ++Slot)
	{
		if (Weapons[Slot])
		{
			Weapons[Slot]->ClearWeapon();
		}
	}
	if (Weapon)
	{
		const FString StartWeapon = Hero && !Hero->StartWeapon.IsEmpty() ? Hero->StartWeapon : TEXT("pistol");
		Weapon->SetWeapon(StartWeapon);
	}
}

UWS2WeaponComponent* AWS2PlayerPawn::AddOrUpgradeWeapon(const FString& WeaponId, int32 Tier)
{
	if (WeaponId.IsEmpty())
	{
		return nullptr;
	}
	for (UWS2WeaponComponent* Candidate : Weapons)
	{
		if (Candidate && Candidate->GetWeaponId() == WeaponId)
		{
			if (Tier >= Candidate->GetTier())
			{
				Candidate->SetWeapon(WeaponId, Tier);
			}
			return Candidate;
		}
	}
	for (UWS2WeaponComponent* Candidate : Weapons)
	{
		if (Candidate && Candidate->GetWeaponId().IsEmpty())
		{
			Candidate->SetWeapon(WeaponId, Tier);
			UE_LOG(LogWS2Player, Display, TEXT("weapon slot filled: %s T%d"), *WeaponId, Tier + 1);
			return Candidate;
		}
	}
	if (Weapon)
	{
		Weapon->SetWeapon(WeaponId, Tier);
		UE_LOG(LogWS2Player, Display, TEXT("weapon slots full: replaced slot 1 with %s T%d"), *WeaponId, Tier + 1);
	}
	return Weapon;
}

void AWS2PlayerPawn::BuildVisual()
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const WS2::FHeroDef* Hero = Db.FindHero(HeroId);

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
	Body->SetForcedLodModel(1);
	if (bImported)
	{
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
		const int32 NumSlots = Body->GetStaticMesh() ? Body->GetStaticMesh()->GetStaticMaterials().Num() : 0;
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			Body->SetMaterial(Slot, BodyMid);
		}
		SetHeroClip(TEXT("idle"));
	}
}

FString AWS2PlayerPawn::GetHeroName() const
{
	if (const WS2::FHeroDef* Hero = WS2::FDatabase::Get().FindHero(HeroId))
	{
		return Hero->Name;
	}
	return HeroId;
}

FString AWS2PlayerPawn::GetWeaponDisplayName() const
{
	FString Result;
	for (const UWS2WeaponComponent* Candidate : Weapons)
	{
		if (!Candidate || Candidate->GetWeaponId().IsEmpty())
		{
			continue;
		}
		if (!Result.IsEmpty())
		{
			Result += TEXT(" + ");
		}
		Result += Candidate->GetDisplayName();
	}
	return Result.IsEmpty() ? FString(TEXT("-")) : Result;
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

void AWS2PlayerPawn::ServerSetInput_Implementation(float Forward, float Right)
{
	RemoteInput = FVector2D(Right, Forward);
	RemoteInputTimeout = 0.2f;
	if (AWS2GameMode::IsNetVerifyRequested() && !bNetVerifyInputLogged && (FMath::Abs(Forward) > 0.01f || FMath::Abs(Right) > 0.01f))
	{
		bNetVerifyInputLogged = true;
		UE_LOG(LogWS2Player, Display, TEXT("NETVERIFY SERVER INPUT pawn=%s forward=%.2f right=%.2f"), *GetName(), Forward, Right);
		WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY SERVER INPUT pawn=%s forward=%.2f right=%.2f"), *GetName(), Forward, Right));
	}
}

void AWS2PlayerPawn::FireInput()
{
	if (!HasAuthority())
	{
		ServerFireOnce();
		return;
	}
	if (Weapon)
	{
		Weapon->FireProofOnce();
	}
}

void AWS2PlayerPawn::ServerFireOnce_Implementation()
{
	if (HasAuthority() && Weapon)
	{
		Weapon->FireProofOnce();
	}
}

void AWS2PlayerPawn::SetHeroClip(const FString& Clip)
{
	if (!BodyMid || Clip == HeroClip)
	{
		return;
	}

	HeroClip = Clip;
	const FIntPoint Range = HeroVat ? HeroVat->Clip(Clip, FIntPoint(0, HeroVat->Frames)) : FIntPoint(0, 16);
	BodyMid->SetScalarParameterValue(TEXT("VATStart"), static_cast<float>(Range.X));
	BodyMid->SetScalarParameterValue(TEXT("VATCount"), static_cast<float>(Range.Y));
}

void AWS2PlayerPawn::PlayAttackAnim()
{
	if (!HasAuthority())
	{
		return;
	}
	AttackAnimLeft = 0.42f;
	SetHeroClip(TEXT("attack"));
	MulticastPlaySfx(static_cast<uint8>(WS2Audio::ESfx::Shot), GetActorLocation());
}

void AWS2PlayerPawn::MulticastPlaySfx_Implementation(uint8 Sfx, FVector Location)
{
	WS2Audio::Play(GetWorld(), static_cast<WS2Audio::ESfx>(Sfx), Location, 0.5f, this);
}

void AWS2PlayerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		// A client's pawn can begin play before its PlayerState has arrived. Adopt
		// the server-selected hero as soon as the slot identity replicates.
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (const AWS2PlayerState* PS = PC->GetPlayerState<AWS2PlayerState>())
			{
				if (!PS->HeroId.IsEmpty() && PS->HeroId != HeroId)
				{
					ApplyHeroStats(PS->HeroId, false);
					BuildVisual();
					UE_LOG(LogWS2Player, Display, TEXT("Player hero replicated: slot=%d hero=%s"), PS->Slot, *HeroId);
				}
			}
		}
		// The client sends its axis vector once per frame and lets the server
		// simulate movement; only the facing is interpolated locally from the
		// replicated aim direction.
		if (IsLocallyControlled())
		{
			if (InputVector.SizeSquared() > 0.0001f)
			{
				ApplyMovementStep(DeltaSeconds, InputVector);
			}
			ServerSetInput(InputVector.Y, InputVector.X);
			InputVector = FVector2D::ZeroVector;
		}
		if (IsLocallyControlled() && AWS2GameMode::IsNetVerifyRequested() && !bNetVerifyMoveLogged)
		{
			const float MoveDistance = FVector::Dist2D(GetActorLocation(), NetVerifyStartLocation);
			if (MoveDistance > 60.f)
			{
				bNetVerifyMoveLogged = true;
				UE_LOG(LogWS2Player, Display, TEXT("NETVERIFY CLIENT MOVED pawn=%s distance=%.0f pos=%s"),
					*GetName(), MoveDistance, *GetActorLocation().ToCompactString());
				WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY CLIENT MOVED pawn=%s distance=%.0f pos=%s"),
					*GetName(), MoveDistance, *GetActorLocation().ToCompactString()));
			}
		}
		if (Body && AimDir.SizeSquared() > 0.01f)
		{
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(AimDir.Y, AimDir.X));
			Body->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
		}
		return;
	}

	AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr;
	if (!GameMode)
	{
		return;
	}

	if (!IsLocallyControlled())
	{
		RemoteInputTimeout -= DeltaSeconds;
		if (RemoteInputTimeout <= 0.f)
		{
			RemoteInput = FVector2D::ZeroVector;
		}
	}
	const FVector2D Direction = IsLocallyControlled() ? InputVector : RemoteInput;
	ApplyMovementStep(DeltaSeconds, Direction);
	if (GameMode->IsNetVerifyActive() && !bNetVerifyMoveLogged)
	{
		const float MoveDistance = FVector::Dist2D(GetActorLocation(), NetVerifyStartLocation);
		if (MoveDistance > 80.f)
		{
			bNetVerifyMoveLogged = true;
			const APlayerController* PC = Cast<APlayerController>(GetController());
			UE_LOG(LogWS2Player, Display, TEXT("NETVERIFY SERVER MOVED pawn=%s local=%d distance=%.0f pos=%s"),
				*GetName(), PC && PC->IsLocalController() ? 1 : 0, MoveDistance, *GetActorLocation().ToCompactString());
			WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY SERVER MOVED pawn=%s local=%d distance=%.0f pos=%s"),
				*GetName(), PC && PC->IsLocalController() ? 1 : 0, MoveDistance, *GetActorLocation().ToCompactString()));
		}
	}

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

void AWS2PlayerPawn::ApplyMovementStep(float DeltaSeconds, const FVector2D& InDirection)
{
	FVector2D Direction = InDirection;
	if (Direction.SizeSquared() > 1.f)
	{
		Direction = Direction.GetSafeNormal();
	}

	AWS2Arena* Arena = nullptr;
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		Arena = GameMode->GetArena();
	}
	if (!Arena && GetWorld())
	{
		for (TActorIterator<AWS2Arena> It(GetWorld()); It; ++It)
		{
			Arena = *It;
			break;
		}
	}

	float Speed = 430.f * (1.f + SpeedPct / 100.f) * (bDown ? 0.35f : 1.f);
	if (Arena)
	{
		Speed *= Arena->GetSpeedMultiplierAt(Arena->WorldToArena(GetPos2D()));
	}
	FVector2D WorldPosition = GetPos2D() + Direction * Speed * DeltaSeconds;
	if (Arena)
	{
		FVector2D ArenaPosition = Arena->WorldToArena(WorldPosition);
		Arena->ResolveCircle(ArenaPosition, Radius);
		WorldPosition = Arena->ArenaToWorld(ArenaPosition);
	}
	SetActorLocation(FVector(WorldPosition.X, WorldPosition.Y, 42.f));
}

void AWS2PlayerPawn::ApplyItemStats(const TMap<FString, float>& Stats)
{
	if (!HasAuthority())
	{
		return;
	}
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
}

void AWS2PlayerPawn::ApplyContactDamage(float Amount)
{
	if (!HasAuthority() || bDown || bInvulnerable || Amount <= 0.f)
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
			GameMode->OnPlayerDown(this);
		}
	}
}

void AWS2PlayerPawn::Heal(float Amount)
{
	if (!HasAuthority() || Amount <= 0.f)
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
