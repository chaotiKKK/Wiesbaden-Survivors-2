#pragma once

#include "CoreMinimal.h"

namespace WS2
{
	struct FWeaponTier
	{
		int32 Tier = 0;
		float Damage = 8.f;
		float Cooldown = 0.6f;
		float Speed = 400.f;
		float Crit = 0.05f;
		float CritDmg = 1.6f;
		int32 Price = 10;
		int32 Pellets = 1;
		float Spread = 0.f;
		float Arc = 0.f;
		float Pierce = 0.f;
		float Knockback = 0.f;
		float BlastRadius = 0.f;
		float Stun = 0.f;
		float Lifesteal = 0.f;
		float Bounce = 0.f;
		float Slow = 0.f;
		float SlowTime = 0.f;
		float Burn = 0.f;
		float Chain = 0.f;
		float ChainRange = 0.f;
		float Heal = 0.f;
		float ArmorPierce = 0.f;
		FString Element;
	};

	struct FWeaponDef
	{
		FString Id;
		FString Name;
		TArray<FString> Classes;
		FString Kind;
		FString Color;
		FString Special;
		bool bUnlockDefault = false;
		TArray<FWeaponTier> Tiers;

		const FWeaponTier& GetTier(int32 Index) const
		{
			return Tiers[FMath::Clamp(Index, 0, FMath::Max(0, Tiers.Num() - 1))];
		}
	};

	struct FHeroDef
	{
		FString Id;
		FString Name;
		FString Role;
		FString StartWeapon;
		FString Color;
		FString Build;
		FString Hat;
		FString Prop;
		TMap<FString, float> Stats;

		float Stat(const FString& Key) const
		{
			const float* Found = Stats.Find(Key);
			return Found ? *Found : 0.f;
		}
	};

	struct FEnemyDef
	{
		FString Id;
		FString Name;
		FString AI;
		FString Shape;
		FString Color;
		float Hp = 10.f;
		float Dmg = 5.f;
		float Speed = 100.f;
		float Radius = 12.f;
		float Armor = 0.f;
		int32 Xp = 1;
		int32 Mat = 1;
		int32 MinWave = 1;
		float Weight = 20.f;
		bool bFly = false;
	};

	/** One vertex-animation atlas: frames, playback rate and the offset bounds. */
	struct FBossPhase
	{
		float At = 1.f;
		float Speed = 60.f;
		FString Pattern = TEXT("radial");
		float Cooldown = 2.f;
		int32 Count = 8;
		float BulletSpeed = 200.f;
		float BulletDamage = 10.f;
		FString Summon;
	};

	struct FBossDef
	{
		FString Id;
		FString Name;
		FString Arena;
		FString Color;
		float Hp = 3400.f;
		float Radius = 46.f;
		float Speed = 52.f;
		float Dmg = 22.f;
		float Armor = 10.f;
		int32 Mat = 60;
		int32 Xp = 40;
		TArray<FBossPhase> Phases;
	};

	struct FItemDef
	{
		FString Id;
		FString Name;
		int32 Rarity = 1;
		int32 Price = 20;
		FString Flavor;
		TMap<FString, float> Stats;
	};

	struct FElementDef
	{
		FString Id;
		FString Name;
		FString Color;
		FString Kind;
		FString Icon;
	};

	struct FStatDef
	{
		FString Id;
		FString Name;
		FString Unit;
	};

	struct FVatInfo
	{
		FString Asset;
		int32 Frames = 16;
		float Fps = 24.f;
		int32 AtlasWidth = 64;
		FVector Extent = FVector(10.f, 10.f, 10.f);
		TMap<FString, FIntPoint> Clips;

		FIntPoint Clip(const FString& Name, const FIntPoint& Fallback = FIntPoint(0, 16)) const
		{
			const FIntPoint* Found = Clips.Find(Name);
			return Found ? *Found : Fallback;
		}
	};

	struct FArenaDef
	{
		FString Id;
		FString Name;
		FString Theme;
		FString Ground;
		FString Grid;
		FString Accent;
		FString Desc;
		float W = 1900.f;
		float H = 1500.f;
		int32 Buildings = 5;
		int32 Cover = 10;
		int32 Poison = 0;
		int32 SpeedField = 0;
		int32 MovingWalls = 1;
	};

	class WS2CORE_API FDatabase
	{
	public:
		static FDatabase& Get();

		bool EnsureLoaded();
		const FWeaponDef* FindWeapon(const FString& Id) const;
		const FHeroDef* FindHero(const FString& Id) const;
		const FEnemyDef* FindEnemy(const FString& Id) const;
		const FArenaDef* FindArena(const FString& Id) const;
		const FBossDef* FindBoss(const FString& Id) const;
		const FItemDef* FindItem(const FString& Id) const;
		const FElementDef* FindElement(const FString& Id) const;
		const FVatInfo* FindVat(const FString& Asset) const;
		TArray<const FBossDef*> BossesForArena(const FString& ArenaId) const;

		const FEnemyDef& EnemyByIndex(int32 Index) const { return Enemies[FMath::Clamp(Index, 0, Enemies.Num() - 1)]; }
		int32 NumEnemies() const { return Enemies.Num(); }
		int32 NumWeaponTiers() const;

		TArray<FWeaponDef> Weapons;
		TArray<FHeroDef> Heroes;
		TArray<FEnemyDef> Enemies;
		TArray<FArenaDef> Arenas;
		TArray<FBossDef> Bosses;
		TArray<FItemDef> Items;
		TArray<FElementDef> Elements;
		TArray<FStatDef> Stats;
		TMap<FString, FVatInfo> Vats;

		bool bLoaded = false;
		FString LastError;

	private:
		bool LoadFromDisk();
		static FString DataDirectory();
	};
}
