#include "WS2WeaponComponent.h"

#include "Misc/Parse.h"
#include "WS2Crowd.h"
#include "WS2GameMode.h"
#include "WS2PackageTrace.h"
#include "WS2PlayerPawn.h"
#include "GameFramework/PlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Weapon, Log, All);

namespace
{
	FLinearColor WeaponHex(const FString& Hex)
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
		return FLinearColor(1.f, 0.85f, 0.35f);
	}
}

UWS2WeaponComponent::UWS2WeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	Rng = FWS2Rng(0xC0FFEEull);
}

void UWS2WeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerPawn = Cast<AWS2PlayerPawn>(GetOwner());
	GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr;

	if (WeaponId.IsEmpty() && bAutoEquipDefault)
	{
		SetWeapon(TEXT("pistol"));
	}
}

void UWS2WeaponComponent::ClearWeapon()
{
	WeaponId.Reset();
	Def = nullptr;
	Tier = 0;
	Cooldown = 0.f;
	LastBehavior.Reset();
}

void UWS2WeaponComponent::SetWeapon(const FString& InWeaponId, int32 InTier)
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	Db.EnsureLoaded();

	Def = Db.FindWeapon(InWeaponId);
	WeaponId = InWeaponId;
	Tier = FMath::Max(0, InTier);
	Cooldown = 0.f;

	if (!Def)
	{
		for (const WS2::FWeaponDef& Candidate : Db.Weapons)
		{
			if (Candidate.Tiers.Num() > 0)
			{
				Def = &Candidate;
				WeaponId = Candidate.Id;
				break;
			}
		}
	}

	const WS2::FWeaponTier* TierData = Def && Def->Tiers.Num() > 0 ? &Def->GetTier(Tier) : nullptr;
	UE_LOG(LogWS2Weapon, Display, TEXT("Weapon set: %s (tier %d/%d, kind=%s, element=%s)"),
		Def ? *Def->Name : TEXT("none"), Tier + 1, Def ? Def->Tiers.Num() : 0,
		Def ? *Def->Kind : TEXT("-"), TierData && !TierData->Element.IsEmpty() ? *TierData->Element : TEXT("none"));
}

float UWS2WeaponComponent::GetRange() const
{
	const float Multiplier = OwnerPawn ? OwnerPawn->GetRangeMultiplier() : 1.f;
	const float Base = Def && Def->Kind == TEXT("melee") ? 210.f : 470.f;
	return Base * Multiplier;
}

void UWS2WeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerPawn)
	{
		OwnerPawn = Cast<AWS2PlayerPawn>(GetOwner());
	}
	if (!OwnerPawn || !OwnerPawn->HasAuthority())
	{
		// Auto-fire is server-authoritative; remote clients see the replicated
		// projectiles and hit flashes instead of running a second simulation.
		return;
	}
	if (!GameMode)
	{
		GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr;
	}
	if (!Def || !OwnerPawn || !GameMode || Def->Tiers.Num() == 0)
	{
		return;
	}

	Cooldown -= DeltaTime;
	if (Cooldown > 0.f)
	{
		return;
	}

	AWS2Crowd* Crowd = GameMode->GetCrowd();
	if (!Crowd)
	{
		return;
	}

	const FVector2D From = OwnerPawn->GetArenaPos();
	FVector2D Target;
	if (!Crowd->FindNearestEnemy(From, GetRange() * 1.4f, Target))
	{
		return;
	}

	FireAt(From, Target);
}

bool UWS2WeaponComponent::FireProofOnce()
{
	if (!OwnerPawn || !Def || Def->Tiers.Num() == 0)
	{
		return false;
	}
	AWS2Crowd* Crowd = GameMode ? GameMode->GetCrowd() : nullptr;
	if (!Crowd)
	{
		return false;
	}

	const FVector2D From = OwnerPawn->GetArenaPos();
	FVector2D Target = From + OwnerPawn->GetAimDir() * 320.f;
	Crowd->FindNearestEnemy(From, 2500.f, Target);
	Cooldown = 0.f;
	FireAt(From, Target, true);
	return true;
}

void UWS2WeaponComponent::FireAt(const FVector2D& From, const FVector2D& Target, bool bFromInput)
{
	AWS2Crowd* Crowd = GameMode ? GameMode->GetCrowd() : nullptr;
	if (!Crowd || !Def || !OwnerPawn)
	{
		return;
	}

	const WS2::FWeaponTier& TierData = Def->GetTier(Tier);
	const FVector2D Direction = (Target - From).GetSafeNormal();
	const bool bMelee = Def->Kind == TEXT("melee") || Def->Kind == TEXT("aura");
	const float ClassPct = bMelee ? OwnerPawn->GetMeleePct() : OwnerPawn->GetRangedPct();
	const float DamageScale = 1.f + (OwnerPawn->GetDamagePct() + ClassPct + OwnerPawn->GetElementPct() * 0.3f) / 100.f;
	const bool bCrit = Rng.Chance(FMath::Clamp(TierData.Crit + OwnerPawn->GetCritPct() / 100.f, 0.f, 0.95f));
	const float CritMultiplier = TierData.CritDmg * (1.f + OwnerPawn->GetCritDmgPct() / 100.f);
	const float Damage = TierData.Damage * DamageScale * (bCrit ? CritMultiplier : 1.f);
	const float Knockback = TierData.Knockback * (1.f + OwnerPawn->GetKnockbackPct() / 100.f);

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	FLinearColor Color = WeaponHex(Def->Color);
	if (const WS2::FElementDef* Element = TierData.Element.IsEmpty() ? nullptr : Db.FindElement(TierData.Element))
	{
		Color = WeaponHex(Element->Color);
	}

	const FString Kind = Def->Kind;
	LastBehavior = Kind;
	int32 Hits = 0;

	if (Kind == TEXT("melee"))
	{
		const float Arc = TierData.Arc > 0.f ? TierData.Arc : 140.f;
		const float Range = GetRange();
		Crowd->RegisterShot();
		Hits = Crowd->DamageArc(From, Direction, Range, Arc, Damage, bCrit, TierData.Element,
			TierData.SlowTime, TierData.Burn, TierData.Stun, TierData.ArmorPierce, Knockback);
		LastBehavior = FString::Printf(TEXT("melee arc=%.0f range=%.0f hits=%d"), Arc, Range, Hits);
	}
	else if (Kind == TEXT("hitscan"))
	{
		Crowd->RegisterShot();
		Hits = Crowd->DamageLine(From, Direction, FMath::Max(900.f, GetRange() * 2.f), Damage, bCrit,
			TierData.Element, TierData.SlowTime, TierData.Burn, TierData.Stun, TierData.ArmorPierce);
		LastBehavior = FString::Printf(TEXT("hitscan pierce=all hits=%d"), Hits);
	}
	else if (Kind == TEXT("aura"))
	{
		const float Radius = 220.f + GetRange() * 0.25f;
		Crowd->RegisterShot();
		Hits = Crowd->DamageArea(From, Radius, Damage, bCrit, TierData.Element, TierData.SlowTime,
			TierData.Burn, TierData.Stun, TierData.ArmorPierce);
		LastBehavior = FString::Printf(TEXT("aura radius=%.0f hits=%d"), Radius, Hits);
	}
	else if (Kind == TEXT("chain"))
	{
		const int32 Links = FMath::Max(1, FMath::RoundToInt(TierData.Chain > 0.f ? TierData.Chain : 3.f));
		const float ChainRange = TierData.ChainRange > 0.f ? TierData.ChainRange : 170.f;
		Crowd->RegisterShot();
		Hits = Crowd->DamageChain(From, Damage, Links, ChainRange, bCrit, TierData.Element,
			FMath::Max(0.35f, TierData.Stun), TierData.ArmorPierce);
		LastBehavior = FString::Printf(TEXT("chain links=%d range=%.0f hits=%d"), Links, ChainRange, Hits);
	}
	else
	{
		const bool bCone = Kind == TEXT("cone");
		const bool bCharge = Kind == TEXT("charge");
		const int32 Pellets = bCone ? FMath::Max(3, TierData.Pellets) : TierData.Pellets;
		const float Spread = bCone ? FMath::Max(0.35f, TierData.Spread) : TierData.Spread;
		const float Speed = bCharge ? TierData.Speed * 1.15f : TierData.Speed;
		const float Pierce = bCharge ? FMath::Max(99.f, TierData.Pierce) : TierData.Pierce;
		Crowd->SpawnProjectileEx(From, Direction, Damage, Speed, Pellets, Spread, Pierce, bCrit, Knockback,
			Color, TierData.BlastRadius, TierData.Element, TierData.SlowTime, TierData.Burn, TierData.Stun,
			TierData.Chain, TierData.ChainRange, TierData.Lifesteal, TierData.ArmorPierce);
		Hits = Pellets;
		LastBehavior = FString::Printf(TEXT("%s pellets=%d spread=%.2f blast=%.0f element=%s"),
			*Kind, Pellets, Spread, TierData.BlastRadius, TierData.Element.IsEmpty() ? TEXT("none") : *TierData.Element);
	}

	if (TierData.Lifesteal > 0.f && Hits > 0 && Kind != TEXT("projectile") && Kind != TEXT("cone") && Kind != TEXT("charge"))
	{
		OwnerPawn->Heal(Damage * Hits * TierData.Lifesteal * 0.01f);
	}
	if (TierData.Heal > 0.f && Hits > 0)
	{
		OwnerPawn->Heal(TierData.Heal * Hits);
	}

	OwnerPawn->PlayAttackAnim();
	if (GameMode && GameMode->IsNetVerifyActive())
	{
		const AController* Controller = OwnerPawn->GetController();
		const FString OwnerName = Controller && Controller->PlayerState
			? Controller->PlayerState->GetPlayerName() : OwnerPawn->GetName();
		UE_LOG(LogWS2Weapon, Display, TEXT("NETVERIFY SERVER FIRE owner=%s pawn=%s manual=%d kind=%s hits=%d from=%.0f,%.0f target=%.0f,%.0f"),
			*OwnerName, *OwnerPawn->GetName(), bFromInput ? 1 : 0, *Kind, Hits,
			From.X, From.Y, Target.X, Target.Y);
		WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY SERVER FIRE owner=%s pawn=%s manual=%d kind=%s hits=%d from=%.0f,%.0f target=%.0f,%.0f"),
			*OwnerName, *OwnerPawn->GetName(), bFromInput ? 1 : 0, *Kind, Hits,
			From.X, From.Y, Target.X, Target.Y));
	}

	const float AttackSpeed = 1.f + OwnerPawn->GetAttackSpeedPct() / 100.f;
	Cooldown = FMath::Max(0.04f, TierData.Cooldown / AttackSpeed);
}
