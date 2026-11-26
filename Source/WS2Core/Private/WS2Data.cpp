#include "WS2Data.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace WS2
{
	namespace
	{
		bool LoadJsonArray(const FString& File, TArray<TSharedPtr<FJsonValue>>& Out, FString& OutError)
		{
			FString Raw;
			if (!FFileHelper::LoadFileToString(Raw, *File))
			{
				OutError = FString::Printf(TEXT("missing data file: %s"), *File);
				return false;
			}

			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
			if (!FJsonSerializer::Deserialize(Reader, Out))
			{
				OutError = FString::Printf(TEXT("malformed json: %s"), *File);
				return false;
			}
			return true;
		}

		float Num(const TSharedPtr<FJsonObject>& Obj, const FString& Key, float Def = 0.f)
		{
			if (!Obj.IsValid())
			{
				return Def;
			}
			double Value = 0.0;
			return Obj->TryGetNumberField(Key, Value) ? static_cast<float>(Value) : Def;
		}

		int32 Int(const TSharedPtr<FJsonObject>& Obj, const FString& Key, int32 Def = 0)
		{
			return FMath::RoundToInt(Num(Obj, Key, static_cast<float>(Def)));
		}

		FString Str(const TSharedPtr<FJsonObject>& Obj, const FString& Key, const FString& Def = FString())
		{
			if (!Obj.IsValid())
			{
				return Def;
			}
			FString Value;
			return Obj->TryGetStringField(Key, Value) ? Value : Def;
		}

		bool Bool(const TSharedPtr<FJsonObject>& Obj, const FString& Key, bool Def = false)
		{
			if (!Obj.IsValid())
			{
				return Def;
			}
			bool Value = false;
			return Obj->TryGetBoolField(Key, Value) ? Value : Def;
		}
	}

	FDatabase& FDatabase::Get()
	{
		static FDatabase Instance;
		return Instance;
	}

	FString FDatabase::DataDirectory()
	{
		return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data"));
	}

	bool FDatabase::EnsureLoaded()
	{
		return bLoaded || LoadFromDisk();
	}

	bool FDatabase::LoadFromDisk()
	{
		const FString Dir = DataDirectory();
		FString Error;

		// ------------------------------------------------------------- weapons
		TArray<TSharedPtr<FJsonValue>> WeaponValues;
		if (!LoadJsonArray(FPaths::Combine(Dir, TEXT("weapons.json")), WeaponValues, Error))
		{
			LastError = Error;
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : WeaponValues)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}

			FWeaponDef Weapon;
			Weapon.Id = Str(Obj, TEXT("id"));
			Weapon.Name = Str(Obj, TEXT("name"));
			Weapon.Kind = Str(Obj, TEXT("type"));
			Weapon.Color = Str(Obj, TEXT("color"));
			Weapon.Special = Str(Obj, TEXT("special"));
			Weapon.bUnlockDefault = Bool(Obj, TEXT("unlockDefault"), false);

			const TArray<TSharedPtr<FJsonValue>>* Classes = nullptr;
			if (Obj->TryGetArrayField(TEXT("classes"), Classes))
			{
				for (const TSharedPtr<FJsonValue>& ClassValue : *Classes)
				{
					Weapon.Classes.Add(ClassValue->AsString());
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* Tiers = nullptr;
			if (Obj->TryGetArrayField(TEXT("tiers"), Tiers))
			{
				for (const TSharedPtr<FJsonValue>& TierValue : *Tiers)
				{
					const TSharedPtr<FJsonObject> TierObj = TierValue->AsObject();
					if (!TierObj.IsValid())
					{
						continue;
					}
					FWeaponTier Tier;
					Tier.Tier = Int(TierObj, TEXT("tier"), Weapon.Tiers.Num());
					Tier.Damage = Num(TierObj, TEXT("damage"), 8.f);
					Tier.Cooldown = Num(TierObj, TEXT("cooldown"), 0.6f);
					Tier.Speed = Num(TierObj, TEXT("speed"), 400.f);
					Tier.Crit = Num(TierObj, TEXT("crit"), 0.05f);
					Tier.CritDmg = Num(TierObj, TEXT("critDmg"), 1.6f);
					Tier.Price = Int(TierObj, TEXT("price"), 10);

					const TSharedPtr<FJsonObject>* Extras = nullptr;
					if (TierObj->TryGetObjectField(TEXT("extras"), Extras) && Extras->IsValid())
					{
						Tier.Pellets = FMath::Max(1, Int(*Extras, TEXT("pellets"), 1));
						Tier.Spread = Num(*Extras, TEXT("spread"), 0.f);
						Tier.Arc = Num(*Extras, TEXT("arc"), 0.f);
						Tier.Pierce = Num(*Extras, TEXT("pierce"), 0.f);
						Tier.Knockback = Num(*Extras, TEXT("knock"), 0.f);
						Tier.BlastRadius = Num(*Extras, TEXT("boom"), 0.f);
						Tier.Stun = Num(*Extras, TEXT("stun"), 0.f);
						Tier.Lifesteal = Num(*Extras, TEXT("lifesteal"), 0.f);
						Tier.Bounce = Num(*Extras, TEXT("bounce"), 0.f);
						Tier.Slow = Num(*Extras, TEXT("slow"), 0.f);
						Tier.SlowTime = Num(*Extras, TEXT("slowT"), Num(*Extras, TEXT("slowTime"), 0.f));
						Tier.Burn = Num(*Extras, TEXT("burn"), 0.f);
						Tier.Chain = Num(*Extras, TEXT("chain"), 0.f);
						Tier.ChainRange = Num(*Extras, TEXT("chainRange"), 150.f);
						Tier.Heal = Num(*Extras, TEXT("healHit"), Num(*Extras, TEXT("heal"), 0.f));
						Tier.ArmorPierce = Num(*Extras, TEXT("armorPierce"), 0.f);
						if (Num(*Extras, TEXT("shock"), 0.f) > 0.f)
						{
							Tier.Stun = FMath::Max(Tier.Stun, 0.35f);
						}
					}

					// Element identity: the extractor writes it, but older exports only
					// carry the extras. Derive the same five-element mapping either way.
					Tier.Element = Str(TierObj, TEXT("element"));
					if (Tier.Element.IsEmpty())
					{
						if (Tier.Burn > 0.f) Tier.Element = TEXT("fire");
						else if (Tier.Slow > 0.f) Tier.Element = TEXT("ice");
						else if (Tier.Chain > 0.f || Tier.Stun > 0.f) Tier.Element = TEXT("shock");
						else if (Tier.BlastRadius > 0.f) Tier.Element = TEXT("boom");
					}
					Weapon.Tiers.Add(Tier);
				}
			}
			Weapons.Add(Weapon);
		}

		// ------------------------------------------------------------- heroes
		TArray<TSharedPtr<FJsonValue>> HeroValues;
		if (!LoadJsonArray(FPaths::Combine(Dir, TEXT("characters.json")), HeroValues, Error))
		{
			LastError = Error;
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : HeroValues)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}
			FHeroDef Hero;
			Hero.Id = Str(Obj, TEXT("id"));
			Hero.Name = Str(Obj, TEXT("name"));
			Hero.Role = Str(Obj, TEXT("role"));
			Hero.StartWeapon = Str(Obj, TEXT("startWeapon"));

			const TSharedPtr<FJsonObject>* Skin = nullptr;
			if (Obj->TryGetObjectField(TEXT("skin"), Skin) && Skin->IsValid())
			{
				Hero.Color = Str(*Skin, TEXT("chest"));
				Hero.Build = Str(*Skin, TEXT("build"));
				Hero.Hat = Str(*Skin, TEXT("hat"));
				Hero.Prop = Str(*Skin, TEXT("prop"));
			}

			const TSharedPtr<FJsonObject>* StatsObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("stats"), StatsObj) && StatsObj->IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StatsObj)->Values)
				{
					if (Pair.Value.IsValid())
					{
						Hero.Stats.Add(Pair.Key, static_cast<float>(Pair.Value->AsNumber()));
					}
				}
			}
			Heroes.Add(Hero);
		}

		// ------------------------------------------------------------- enemies
		TArray<TSharedPtr<FJsonValue>> EnemyValues;
		if (!LoadJsonArray(FPaths::Combine(Dir, TEXT("enemies.json")), EnemyValues, Error))
		{
			LastError = Error;
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : EnemyValues)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}
			FEnemyDef Enemy;
			Enemy.Id = Str(Obj, TEXT("id"));
			Enemy.Name = Str(Obj, TEXT("name"));
			Enemy.AI = Str(Obj, TEXT("ai"), TEXT("chase"));
			Enemy.Shape = Str(Obj, TEXT("shape"), TEXT("dot"));
			Enemy.Color = Str(Obj, TEXT("col"), TEXT("#ff5d6e"));
			Enemy.Hp = Num(Obj, TEXT("hp"), 10.f);
			Enemy.Dmg = Num(Obj, TEXT("dmg"), 5.f);
			Enemy.Speed = Num(Obj, TEXT("spd"), 100.f);
			Enemy.Radius = Num(Obj, TEXT("r"), 12.f);
			Enemy.Armor = Num(Obj, TEXT("armor"), 0.f);
			Enemy.Xp = Int(Obj, TEXT("xp"), 1);
			Enemy.Mat = Int(Obj, TEXT("mat"), 1);
			Enemy.MinWave = Int(Obj, TEXT("minW"), 1);
			Enemy.Weight = Num(Obj, TEXT("w"), 20.f);
			Enemy.bFly = Bool(Obj, TEXT("fly"), false);
			Enemies.Add(Enemy);
		}

		// ------------------------------------------------------------- arenas
		TArray<TSharedPtr<FJsonValue>> ArenaValues;
		if (!LoadJsonArray(FPaths::Combine(Dir, TEXT("arenas.json")), ArenaValues, Error))
		{
			LastError = Error;
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : ArenaValues)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}
			FArenaDef Arena;
			Arena.Id = Str(Obj, TEXT("id"));
			Arena.Name = Str(Obj, TEXT("name"));
			Arena.Theme = Str(Obj, TEXT("theme"));
			Arena.Ground = Str(Obj, TEXT("ground"), TEXT("#12291d"));
			Arena.Grid = Str(Obj, TEXT("grid"), TEXT("#1b3c2a"));
			Arena.Accent = Str(Obj, TEXT("accent"), TEXT("#5dff9b"));
			Arena.Desc = Str(Obj, TEXT("desc"));
			Arena.W = Num(Obj, TEXT("w"), 1900.f);
			Arena.H = Num(Obj, TEXT("h"), 1500.f);
			Arena.Buildings = Int(Obj, TEXT("buildings"), 5);
			Arena.Cover = Int(Obj, TEXT("cover"), 10);
			Arena.Poison = Int(Obj, TEXT("poison"), 0);
			Arena.SpeedField = Int(Obj, TEXT("speedField"), 0);
			Arena.MovingWalls = Int(Obj, TEXT("movingWalls"), 1);
			Arenas.Add(Arena);
		}

		// ------------------------------------------------------------- bosses
		TArray<TSharedPtr<FJsonValue>> BossValues;
		if (LoadJsonArray(FPaths::Combine(Dir, TEXT("bosses.json")), BossValues, Error))
		{
			for (const TSharedPtr<FJsonValue>& Value : BossValues)
			{
				const TSharedPtr<FJsonObject> Obj = Value->AsObject();
				if (!Obj.IsValid())
				{
					continue;
				}
				FBossDef Boss;
				Boss.Id = Str(Obj, TEXT("id"));
				Boss.Name = Str(Obj, TEXT("name"));
				Boss.Arena = Str(Obj, TEXT("arena"));
				Boss.Color = Str(Obj, TEXT("col"), TEXT("#f4c25a"));
				Boss.Hp = Num(Obj, TEXT("hp"), 3400.f);
				Boss.Radius = Num(Obj, TEXT("r"), 46.f);
				Boss.Speed = Num(Obj, TEXT("spd"), 52.f);
				Boss.Dmg = Num(Obj, TEXT("dmg"), 22.f);
				Boss.Armor = Num(Obj, TEXT("armor"), 10.f);
				Boss.Mat = Int(Obj, TEXT("mat"), 60);
				Boss.Xp = Int(Obj, TEXT("xp"), 40);

				const TArray<TSharedPtr<FJsonValue>>* Phases = nullptr;
				if (Obj->TryGetArrayField(TEXT("phases"), Phases))
				{
					for (const TSharedPtr<FJsonValue>& PhaseValue : *Phases)
					{
						const TSharedPtr<FJsonObject> PhaseObj = PhaseValue->AsObject();
						if (!PhaseObj.IsValid())
						{
							continue;
						}
						FBossPhase Phase;
						Phase.At = Num(PhaseObj, TEXT("at"), 1.f);
						Phase.Speed = Num(PhaseObj, TEXT("spd"), Boss.Speed);
						Phase.Pattern = Str(PhaseObj, TEXT("pattern"), TEXT("radial"));
						Phase.Cooldown = Num(PhaseObj, TEXT("cd"), 2.f);
						Phase.Count = Int(PhaseObj, TEXT("n"), 8);
						Phase.BulletSpeed = Num(PhaseObj, TEXT("bs"), 200.f);
						Phase.BulletDamage = Num(PhaseObj, TEXT("bd"), 10.f);
						Phase.Summon = Str(PhaseObj, TEXT("summon"));
						Boss.Phases.Add(Phase);
					}
				}
				Bosses.Add(Boss);
			}
		}

		// -------------------------------------------------------------- items
		TArray<TSharedPtr<FJsonValue>> ItemValues;
		if (LoadJsonArray(FPaths::Combine(Dir, TEXT("items.json")), ItemValues, Error))
		{
			for (const TSharedPtr<FJsonValue>& Value : ItemValues)
			{
				const TSharedPtr<FJsonObject> Obj = Value->AsObject();
				if (!Obj.IsValid())
				{
					continue;
				}
				FItemDef Item;
				Item.Id = Str(Obj, TEXT("id"));
				Item.Name = Str(Obj, TEXT("name"));
				Item.Rarity = Int(Obj, TEXT("r"), 1);
				Item.Price = Int(Obj, TEXT("price"), 20);
				Item.Flavor = Str(Obj, TEXT("fl"));
				const TSharedPtr<FJsonObject>* StatsObj = nullptr;
				if (Obj->TryGetObjectField(TEXT("stats"), StatsObj) && StatsObj->IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StatsObj)->Values)
					{
						if (Pair.Value.IsValid())
						{
							Item.Stats.Add(Pair.Key, static_cast<float>(Pair.Value->AsNumber()));
						}
					}
				}
				Items.Add(Item);
			}
		}

		// ------------------------------------------------- elements and stats
		TArray<TSharedPtr<FJsonValue>> ElementValues;
		if (LoadJsonArray(FPaths::Combine(Dir, TEXT("elements.json")), ElementValues, Error))
		{
			for (const TSharedPtr<FJsonValue>& Value : ElementValues)
			{
				const TSharedPtr<FJsonObject> Obj = Value->AsObject();
				if (!Obj.IsValid())
				{
					continue;
				}
				FElementDef Element;
				Element.Id = Str(Obj, TEXT("id"));
				Element.Name = Str(Obj, TEXT("name"));
				Element.Color = Str(Obj, TEXT("color"), TEXT("#ffffff"));
				Element.Kind = Str(Obj, TEXT("kind"));
				Element.Icon = Str(Obj, TEXT("icon"));
				Elements.Add(Element);
			}
		}

		TArray<TSharedPtr<FJsonValue>> StatValues;
		if (LoadJsonArray(FPaths::Combine(Dir, TEXT("stats.json")), StatValues, Error))
		{
			for (const TSharedPtr<FJsonValue>& Value : StatValues)
			{
				const TSharedPtr<FJsonObject> Obj = Value->AsObject();
				if (!Obj.IsValid())
				{
					continue;
				}
				FStatDef Stat;
				Stat.Id = Str(Obj, TEXT("id"));
				Stat.Name = Str(Obj, TEXT("name"));
				Stat.Unit = Str(Obj, TEXT("unit"));
				Stats.Add(Stat);
			}
		}

		// ---------------------------------------------------------- vertex atlases
		// Optional: a missing vat.json only disables the baked animation.
		FString VatRaw;
		if (FFileHelper::LoadFileToString(VatRaw, *FPaths::Combine(Dir, TEXT("vat.json"))))
		{
			TSharedPtr<FJsonObject> VatRoot;
			const TSharedRef<TJsonReader<>> VatReader = TJsonReaderFactory<>::Create(VatRaw);
			if (FJsonSerializer::Deserialize(VatReader, VatRoot) && VatRoot.IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : VatRoot->Values)
				{
					const TSharedPtr<FJsonObject> Obj = Entry.Value.IsValid() ? Entry.Value->AsObject() : nullptr;
					if (!Obj.IsValid())
					{
						continue;
					}

					FVatInfo Info;
					Info.Asset = Entry.Key;
					Info.Frames = FMath::Max(1, Int(Obj, TEXT("frames"), 16));
					Info.Fps = Num(Obj, TEXT("fps"), 24.f);
					Info.AtlasWidth = FMath::Max(1, Int(Obj, TEXT("atlasWidth"), 64));

					const TArray<TSharedPtr<FJsonValue>>* Extent = nullptr;
					if (Obj->TryGetArrayField(TEXT("extentUnits"), Extent) && Extent->Num() >= 3)
					{
						Info.Extent = FVector((*Extent)[0]->AsNumber(), (*Extent)[1]->AsNumber(), (*Extent)[2]->AsNumber());
					}

					const TSharedPtr<FJsonObject>* Clips = nullptr;
					if (Obj->TryGetObjectField(TEXT("clips"), Clips) && Clips && Clips->IsValid())
					{
						for (const TPair<FString, TSharedPtr<FJsonValue>>& Clip : (*Clips)->Values)
						{
							const TSharedPtr<FJsonObject> ClipObj = Clip.Value.IsValid() ? Clip.Value->AsObject() : nullptr;
							if (ClipObj.IsValid())
							{
								Info.Clips.Add(Clip.Key, FIntPoint(Int(ClipObj, TEXT("start"), 0),
									FMath::Max(1, Int(ClipObj, TEXT("count"), 16))));
							}
						}
					}

					Vats.Add(Info.Asset, Info);
				}
			}
		}

		if (Weapons.Num() == 0 || Heroes.Num() == 0 || Enemies.Num() == 0 || Arenas.Num() == 0)
		{
			LastError = TEXT("data loaded but a required table is empty");
			return false;
		}

		bLoaded = true;
		UE_LOG(LogTemp, Display, TEXT("WS2 data: %d weapons / %d tiers / %d heroes / %d enemies / %d bosses / %d items / %d stats / %d elements / %d arenas / %d VATs"),
			Weapons.Num(), NumWeaponTiers(), Heroes.Num(), Enemies.Num(), Bosses.Num(), Items.Num(), Stats.Num(),
			Elements.Num(), Arenas.Num(), Vats.Num());
		return true;
	}

	const FWeaponDef* FDatabase::FindWeapon(const FString& Id) const
	{
		return Weapons.FindByPredicate([&Id](const FWeaponDef& W) { return W.Id == Id; });
	}

	const FHeroDef* FDatabase::FindHero(const FString& Id) const
	{
		return Heroes.FindByPredicate([&Id](const FHeroDef& H) { return H.Id == Id; });
	}

	const FEnemyDef* FDatabase::FindEnemy(const FString& Id) const
	{
		return Enemies.FindByPredicate([&Id](const FEnemyDef& E) { return E.Id == Id; });
	}

	const FArenaDef* FDatabase::FindArena(const FString& Id) const
	{
		return Arenas.FindByPredicate([&Id](const FArenaDef& A) { return A.Id == Id; });
	}

	const FBossDef* FDatabase::FindBoss(const FString& Id) const
	{
		return Bosses.FindByPredicate([&Id](const FBossDef& B) { return B.Id == Id; });
	}

	const FItemDef* FDatabase::FindItem(const FString& Id) const
	{
		return Items.FindByPredicate([&Id](const FItemDef& I) { return I.Id == Id; });
	}

	const FElementDef* FDatabase::FindElement(const FString& Id) const
	{
		return Elements.FindByPredicate([&Id](const FElementDef& E) { return E.Id == Id; });
	}

	TArray<const FBossDef*> FDatabase::BossesForArena(const FString& ArenaId) const
	{
		TArray<const FBossDef*> Result;
		for (const FBossDef& Boss : Bosses)
		{
			if (Boss.Arena == ArenaId)
			{
				Result.Add(&Boss);
			}
		}
		return Result;
	}

	int32 FDatabase::NumWeaponTiers() const
	{
		int32 Count = 0;
		for (const FWeaponDef& Weapon : Weapons)
		{
			Count += Weapon.Tiers.Num();
		}
		return Count;
	}

	const FVatInfo* FDatabase::FindVat(const FString& Asset) const
	{
		return Vats.Find(Asset);
	}
}
