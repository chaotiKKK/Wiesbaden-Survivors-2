#include "WS2GameMode.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "WS2Arena.h"
#include "WS2ArenaCamera.h"
#include "WS2Crowd.h"
#include "WS2PlayerController.h"
#include "WS2PlayerPawn.h"
#include "WS2WaveDirector.h"
#include "WS2WeaponComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2, Log, All);

static TAutoConsoleVariable<int32> CVarPerfMode(
	TEXT("ws2.PerfMode"), 0, TEXT("Spawn a performance horde immediately (0=off, 1=on)."));
static TAutoConsoleVariable<int32> CVarPerfCount(
	TEXT("ws2.PerfCount"), 300, TEXT("Enemy count used by ws2.PerfMode."));
static TAutoConsoleVariable<int32> CVarInvulnerable(
	TEXT("ws2.Invulnerable"), 0, TEXT("Benchmark/dev switch: player ignores contact damage."));
static TAutoConsoleVariable<int32> CVarAutoQuit(
	TEXT("ws2.AutoQuitSeconds"), 0, TEXT("Quit the run after N seconds (0=off)."));
static TAutoConsoleVariable<int32> CVarAutoShot(
	TEXT("ws2.ShotSeconds"), 8, TEXT("Take a screenshot after N seconds (0=off)."));
static TAutoConsoleVariable<int32> CVarAutoShot2(
	TEXT("ws2.ShotSeconds2"), 0, TEXT("Take a second (boss) screenshot after N seconds (0=off)."));
static TAutoConsoleVariable<int32> CVarPerfLog(
	TEXT("ws2.PerfLogSeconds"), 5, TEXT("Emit performance telemetry every N seconds (0=off)."));
static TAutoConsoleVariable<int32> CVarElite(
	TEXT("ws2.Elite"), 0, TEXT("Spawn the slice elite immediately (0=off, 1=on)."));
static TAutoConsoleVariable<FString> CVarArena(
	TEXT("ws2.Arena"), TEXT("kurpark"), TEXT("Arena id to load."));

namespace
{
	FString EscapeJson(const FString& Value)
	{
		FString Out = Value;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\r"), TEXT(""));
		return Out;
	}

	FString ItemStatText(const WS2::FItemDef& Item)
	{
		FString Text;
		for (const TPair<FString, float>& Stat : Item.Stats)
		{
			Text += FString::Printf(TEXT("%s%+.0f "), *Stat.Key, Stat.Value);
		}
		Text.TrimEndInline();
		return Text;
	}
}

AWS2GameMode::AWS2GameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = AWS2PlayerController::StaticClass();
	DefaultPawnClass = nullptr; // the slice spawns and possesses its pawn explicitly
}

void AWS2GameMode::StartPlay()
{
	Super::StartPlay();

	HudState = MakeShared<FWS2RunState>();
	FrameSamples.Reserve(65536);
	SetFrameTag(TEXT("startup"));

	// Headless perf runs: -ws2perf=300 spawns the horde before the first frame
	// (ExecCmds arrive too late, after the map's StartPlay).
	FParse::Value(FCommandLine::Get(), TEXT("ws2perf="), PerfRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2invuln="), InvulnRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2auto="), AutoRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2quit="), QuitRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2shot="), ShotRequest);
	bNoScreenshots = ShotRequest < 0;
	FParse::Value(FCommandLine::Get(), TEXT("ws2elite="), EliteRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2noboss="), NoBossRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2alltypes="), AllTypesRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2bossproof="), BossProofRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2arms="), ArmsRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2items="), ItemsRequest);
	FParse::Value(FCommandLine::Get(), TEXT("ws2shotname="), ScreenshotName);
	FParse::Value(FCommandLine::Get(), TEXT("ws2arena="), ArenaId);
	if (ArenaId.IsEmpty())
	{
		ArenaId = CVarArena.GetValueOnGameThread();
	}
	if (ArenaId.IsEmpty())
	{
		ArenaId = TEXT("kurpark");
	}

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	if (!Db.EnsureLoaded())
	{
		UE_LOG(LogWS2, Error, TEXT("WS2 database failed to load: %s"), *Db.LastError);
		return;
	}

	ShopPoolWeapons = Db.Weapons.Num();
	ShopPoolStats = Db.Stats.Num();
	for (const WS2::FWeaponDef& Weapon : Db.Weapons)
	{
		ShopPoolTiers += Weapon.Tiers.Num();
	}
	ShopPoolItems = Db.Items.Num();

	SpawnWorld();
	SetFrameTag(TEXT("gameplay"));
}

void AWS2GameMode::SpawnWorld()
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const WS2::FArenaDef* ArenaDef = Db.FindArena(ArenaId);
	if (!ArenaDef && Db.Arenas.Num() > 0)
	{
		ArenaDef = &Db.Arenas[0];
	}
	if (!ArenaDef)
	{
		UE_LOG(LogWS2, Error, TEXT("No arena data available for '%s'."), *ArenaId);
		return;
	}
	ArenaId = ArenaDef->Id;

	const FTransform Identity = FTransform::Identity;
	Arena = GetWorld()->SpawnActorDeferred<AWS2Arena>(AWS2Arena::StaticClass(), Identity);
	Arena->Configure(*ArenaDef);
	Arena->FinishSpawning(Identity);

	Crowd = GetWorld()->SpawnActor<AWS2Crowd>(AWS2Crowd::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	Camera = GetWorld()->SpawnActor<AWS2ArenaCamera>(AWS2ArenaCamera::StaticClass(), FVector(0.f, 0.f, 3200.f), FRotator(-89.5f, 0.f, 0.f));

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	const FVector2D Center = Arena->ArenaToWorld(Arena->GetCenter());

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PlayerPawn = PC ? Cast<AWS2PlayerPawn>(PC->GetPawn()) : nullptr;
	if (!PlayerPawn)
	{
		PlayerPawn = GetWorld()->SpawnActor<AWS2PlayerPawn>(AWS2PlayerPawn::StaticClass(),
			FVector(Center.X, Center.Y, 42.f), FRotator::ZeroRotator, SpawnParams);
		if (PC && PlayerPawn)
		{
			PC->Possess(PlayerPawn);
		}
	}

	// Lighting: the blockout materials carry emissive, but lit materials and the
	// imported meshes still need a key light and some ambient.
	ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(),
		FVector(0.f, 0.f, 3000.f), FRotator(-52.f, 38.f, 0.f));
	if (Sun)
	{
		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			LightComponent->SetIntensity(8.f);
			LightComponent->SetLightColor(FLinearColor(1.f, 0.94f, 0.86f));
		}
	}

	ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (Sky)
	{
		if (USkyLightComponent* SkyComponent = Sky->GetLightComponent())
		{
			SkyComponent->SetIntensity(2.f);
			SkyComponent->SetLightColor(FLinearColor(0.35f, 0.45f, 0.62f));
			SkyComponent->bLowerHemisphereIsBlack = false;
			SkyComponent->RecaptureSky();
		}
	}

	if (Crowd)
	{
		Crowd->Init(Arena, PlayerPawn, this);
	}
	if (Camera)
	{
		Camera->SetTargetPlayer(PlayerPawn);
		Camera->SetArenaExtent(Arena ? Arena->GetExtent() : FVector2D(1900.f, 1500.f));
	}
	if (PC)
	{
		PC->SetViewTarget(Camera);
	}

	Director = NewObject<UWS2WaveDirector>(this, TEXT("WaveDirector"));
	AddInstanceComponent(Director);
	Director->RegisterComponent();

	State.ArenaName = ArenaDef->Name;
	State.ArenaMods = Arena ? Arena->GetModSummary() : FString();
	State.Materials = 80;
	State.HeroName = TEXT("Leonidas");
	if (PlayerPawn)
	{
		if (const UWS2WeaponComponent* Weapon = PlayerPawn->GetComponentByClass<UWS2WeaponComponent>())
		{
			State.WeaponName = Weapon->GetDisplayName();
		}
	}

	if (ScreenshotName.IsEmpty())
	{
		ScreenshotName = FString::Printf(TEXT("WS2_%s"), *ArenaId);
	}

	if (Director)
	{
		const float BossHpScale = BossProofRequest > 0 ? 0.001f : 1.f;
		const float ProofBossHp = BossProofRequest > 0 ? 600.f : 0.f;
		Director->StartSlice(AllTypesRequest > 0, BossHpScale, ProofBossHp);
	}

	// The proof passes run after the roster exists so every weapon tier has a
	// target and every item is applied to the real pawn.
	if (ArmsRequest > 0)
	{
		RunWeaponMatrix();
	}
	if (ItemsRequest > 0)
	{
		ApplyAllItems();
	}

	if (AWS2PlayerController* WPC = Cast<AWS2PlayerController>(PC))
	{
		WPC->ShowHud(HudState);
	}

	// Capture helper: put the elite on screen right away for the boss-frame shot.
	if ((EliteRequest > 0 || CVarElite.GetValueOnGameThread() > 0) && Crowd && PlayerPawn)
	{
		if (const WS2::FEnemyDef* BossDef = Db.FindEnemy(TEXT("juggernaut")))
		{
			const FVector2D PlayerPos = PlayerPawn->GetArenaPos();
			FVector2D BossPos = PlayerPos + FVector2D(0.f, -520.f);
			Arena->ResolveCircle(BossPos, BossDef->Radius * 2.4f);
			Crowd->SpawnEnemy(*BossDef, BossPos, true);
		}
	}

	if (Director && (PerfRequest > 0 || CVarPerfMode.GetValueOnGameThread() > 0))
	{
		const int32 Count = PerfRequest > 0 ? PerfRequest : CVarPerfCount.GetValueOnGameThread();
		Director->SpawnPerfHorde(Count);
	}

	UE_LOG(LogWS2, Display,
		TEXT("WS2 arena up: arena=%s name=%s extent=%.0fx%.0f kit=%s mods[%s] pawn=%s bossproof=%d alltypes=%d arms=%d items=%d"),
		*ArenaDef->Id, *ArenaDef->Name, ArenaDef->W, ArenaDef->H,
		Arena ? *Arena->GetKitName() : TEXT("-"), *State.ArenaMods,
		PlayerPawn ? TEXT("ok") : TEXT("missing"), BossProofRequest, AllTypesRequest, ArmsRequest, ItemsRequest);
	UE_LOG(LogWS2, Display, TEXT("SHOP POOL weapons=%d tiers=%d items=%d stats=%d elements=%d arenas=%d bosses=%d enemies=%d"),
		ShopPoolWeapons, ShopPoolTiers, ShopPoolItems, ShopPoolStats, Db.Elements.Num(), Db.Arenas.Num(),
		Db.Bosses.Num(), Db.Enemies.Num());
}

void AWS2GameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bClockStarted)
	{
		bClockStarted = true;
		RealStartTime = FPlatformTime::Seconds();
		LastRealSeconds = 0.f;
		NextLogAt = 3.f;
		NextShotAt = bNoScreenshots ? 0.f
			: (ShotRequest > 0 ? static_cast<float>(ShotRequest)
				: static_cast<float>(FMath::Max(0, CVarAutoShot.GetValueOnGameThread())));
	}

	const float RealTime = FPlatformTime::Seconds() - RealStartTime;
	const float RealDelta = FMath::Max(0.0001f, RealTime - LastRealSeconds);
	LastRealSeconds = RealTime;

	// Keep the actual frame clock independent of gameplay time dilation and of
	// the five-second FPS buckets. The per-frame tag turns a hitch into a named
	// event instead of an unexplained minimum.
	const double Now = FPlatformTime::Seconds();
	if (LastFrameClock > 0.0)
	{
		FFrameSample Sample;
		Sample.Time = RealTime;
		Sample.Ms = static_cast<float>((Now - LastFrameClock) * 1000.0);
		Sample.Tag = CurrentFrameTag;
		FrameSamples.Add(Sample);
		if (FrameSamples.Num() > 65536)
		{
			FrameSamples.RemoveAt(0, 8192, EAllowShrinking::No);
		}
	}
	LastFrameClock = Now;

	UpdateState(DeltaSeconds);

	// Hit-stop: short global slowdown on elite/boss hits, exactly like the source game.
	if (Crowd)
	{
		const float Requested = Crowd->ConsumeHitStop();
		if (Requested > 0.f)
		{
			UGameplayStatics::SetGlobalTimeDilation(this, 0.15f);
			HitStopLeft = FMath::Max(HitStopLeft, Requested);
			++HitStopCount;
			UE_LOG(LogWS2, Display, TEXT("CAMERA hit-stop %.2fs (total %d)"), Requested, HitStopCount);
		}

		const float Punch = Crowd->ConsumePunch();
		if (Punch > 0.f && Camera)
		{
			Camera->Punch(Punch);
		}

		float ShakeAmount = 0.f;
		const FVector2D ShakeAt = Crowd->ConsumeShake(ShakeAmount);
		if (ShakeAmount > 0.f && Camera)
		{
			Camera->AddShake(ShakeAt, ShakeAmount, 0.3f);
		}
	}

	if (HitStopLeft > 0.f)
	{
		const float Dilation = FMath::Max(0.01f, UGameplayStatics::GetGlobalTimeDilation(this));
		HitStopLeft -= DeltaSeconds / Dilation;
		if (HitStopLeft <= 0.f)
		{
			UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
		}
	}

	// Between-wave shop: the offer list is live, the run keeps ticking.
	if (bShopOpen)
	{
		ShopTimer -= DeltaSeconds;
		if (ShopTimer <= 0.f)
		{
			if (AutoRequest > 0 && RerollCount == 0)
			{
				RerollShop();
			}
			PickShop(0);
		}
	}

	if (WaveClearTimer > 0.f)
	{
		WaveClearTimer -= DeltaSeconds;
		if (WaveClearTimer <= 0.f && Director && !bSliceDone)
		{
			Director->StartWave(Director->GetWave() + 1);
		}
	}

	if (BossShotTimer > 0.f)
	{
		BossShotTimer -= DeltaSeconds;
		if (BossShotTimer <= 0.f && !bBossShotTaken)
		{
			bBossShotTaken = true;
			const FString BossShot = ScreenshotName + TEXT("_boss");
			FScreenshotRequest::RequestScreenshot(BossShot, true, false);
			UE_LOG(LogWS2, Display, TEXT("WS2 boss screenshot requested: %s (boss=%s hp=%.0f%% phase=%d)"),
				*BossShot, *State.BossName, State.BossHpPct * 100.f, State.BossPhase);
		}
	}

	if (bSliceDone && SliceTimer > 0.f)
	{
		SliceTimer -= DeltaSeconds;
		if (SliceTimer <= 0.f)
		{
			UE_LOG(LogWS2, Display, TEXT("SLICE RUN END: waves=%d kills=%d fires=%d hits=%d peak=%d avg=%.1ffps min=%.1ffps"),
				State.Wave, State.Kills, Crowd ? Crowd->GetProjectileFires() : 0,
				Crowd ? Crowd->GetProjectileHits() : 0, State.PeakEnemies, State.AvgFps, State.MinFps);
			WriteSliceResult();
			WriteArenaResult();
			if (AutoRequest > 0 || QuitRequest > 0)
			{
				UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
			}
		}
	}

	if (RespawnTimer > 0.f)
	{
		RespawnTimer -= DeltaSeconds;
		if (RespawnTimer <= 0.f)
		{
			if (PlayerPawn)
			{
				const FVector2D Center = Arena ? Arena->ArenaToWorld(Arena->GetCenter()) : FVector2D::ZeroVector;
				PlayerPawn->SetActorLocation(FVector(Center.X, Center.Y, 42.f));
				PlayerPawn->Heal(PlayerPawn->MaxHp);
			}
			if (Crowd)
			{
				Crowd->ClearAll();
			}
			if (Director)
			{
				Director->StartWave(1);
			}
			State.Status.Reset();
		}
	}

	// FPS statistics (real time, unaffected by hit-stop dilation).
	++StatsFrames;
	StatsAccum += RealDelta;
	if (RealTime > 2.f)
	{
		State.MinFps = FMath::Min(State.MinFps, 1.f / RealDelta);
	}

	const int32 PerfLogSeconds = CVarPerfLog.GetValueOnGameThread();
	if (PerfLogSeconds > 0 && RealTime >= NextLogAt)
	{
		const float AvgFps = StatsFrames / FMath::Max(0.001f, StatsAccum);
		State.AvgFps = AvgFps;
		LogPerformance(RealTime);
		StatsFrames = 0;
		StatsAccum = 0.f;
		NextLogAt += static_cast<float>(PerfLogSeconds);
	}

	CheckAutomation(RealTime);
}

void AWS2GameMode::UpdateState(float DeltaSeconds)
{
	if (PlayerPawn)
	{
		PlayerPawn->bInvulnerable = InvulnRequest > 0 || CVarInvulnerable.GetValueOnGameThread() > 0;
		State.Hp = PlayerPawn->Hp;
		State.MaxHp = PlayerPawn->MaxHp;
		State.bPlayerDown = PlayerPawn->bDown;

		// Arena poison is a real damage source, not decoration.
		if (Arena && !PlayerPawn->bInvulnerable)
		{
			const float Dps = Arena->GetPoisonDpsAt(PlayerPawn->GetArenaPos());
			if (Dps > 0.f)
			{
				PlayerPawn->ApplyContactDamage(Dps * DeltaSeconds);
			}
		}
	}
	if (Crowd)
	{
		State.AliveEnemies = Crowd->GetAliveCount();
		State.Projectiles = Crowd->GetProjectileCount();
		State.PeakEnemies = FMath::Max(State.PeakEnemies, Crowd->GetPeakEnemies());
	}
	if (Director)
	{
		State.Wave = Director->GetWave();
		State.WaveTimeLeft = Director->GetWaveTimeLeft();
		State.WaveDuration = Director->GetWaveDuration();
		State.bWaveActive = Director->IsWaveActive();
	}
	if (Arena)
	{
		State.ArenaMods = Arena->GetModSummary();
	}

	// Boss framing: hand the elite's arena position to the camera each frame.
	if (Crowd && Camera)
	{
		FString BossName;
		FVector2D BossPos;
		float BossHp = 0.f;
		const bool bBoss = Crowd->GetBossInfo(BossName, BossPos, BossHp);
		State.bBossAlive = bBoss;
		State.BossName = bBoss ? BossName : FString();
		State.BossHpPct = BossHp;
		State.BossPhase = bBoss ? Crowd->GetActiveBossPhase() + 1 : 0;
		Camera->SetBossTarget(bBoss, BossPos, BossHp);

		// Capture the boss frame the moment it starts: the slice proof needs a shot
		// that actually shows the elite, not whatever the timer happened to hit.
		if (!bNoScreenshots && bBoss && !bBossWasAlive && !bBossShotTaken)
		{
			BossShotTimer = 1.4f;
		}
		bBossWasAlive = bBoss;
	}

	State.bShopOpen = bShopOpen;
	State.bLevelUp = bLevelUpShop;
	State.ShopOffers.Reset(ShopOffers.Num());
	for (const FWS2Offer& Offer : ShopOffers)
	{
		State.ShopOffers.Add(FString::Printf(TEXT("%s  (%d)"), *Offer.Label, Offer.Cost));
	}
	State.ShopHint = bShopOpen ? TEXT("1 / 2 / 3  -  WAHL    R  -  REROLL") : FString();
	if (State.ToastTime > 0.f)
	{
		State.ToastTime = FMath::Max(0.f, State.ToastTime - DeltaSeconds);
	}
	if (HudState.IsValid())
	{
		*HudState = State;
	}
}

void AWS2GameMode::BuildShopOffers()
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	ShopOffers.Reset(3);
	if (!bPoolLogged)
	{
		bPoolLogged = true;
		UE_LOG(LogWS2, Display, TEXT("SHOP POOL READY weapons=%d tiers=%d items=%d stats=%d elements=%d"),
			ShopPoolWeapons, ShopPoolTiers, ShopPoolItems, ShopPoolStats, Db.Elements.Num());
	}

	const int32 Seed = State.Wave * 31 + State.Level * 7 + RerollCount * 101;
	if (Db.Weapons.Num() > 0)
	{
		const WS2::FWeaponDef& Weapon = Db.Weapons[Seed % Db.Weapons.Num()];
		const int32 TierIndex = (Seed + State.Level) % FMath::Max(1, Weapon.Tiers.Num());
		const WS2::FWeaponTier& Tier = Weapon.GetTier(TierIndex);

		FWS2Offer Offer;
		Offer.Kind = TEXT("weapon");
		Offer.Id = Weapon.Id;
		Offer.Tier = TierIndex;
		Offer.Label = FString::Printf(TEXT("%s  T%d  [%s]%s"), *Weapon.Name, TierIndex + 1, *Weapon.Kind,
			Tier.Element.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" %s"), *Tier.Element));
		Offer.StatsText = FString::Printf(TEXT("dmg %.0f / cd %.2f"), Tier.Damage, Tier.Cooldown);
		Offer.Cost = 12 + TierIndex * 6 + State.Wave * 2;
		ShopOffers.Add(Offer);
	}

	for (int32 Slot = 0; Slot < 2 && Db.Items.Num() > 0; ++Slot)
	{
		const WS2::FItemDef& Item = Db.Items[(Seed * (Slot + 2) + Slot * 17) % Db.Items.Num()];
		FWS2Offer Offer;
		Offer.Kind = TEXT("item");
		Offer.Id = Item.Id;
		Offer.Label = FString::Printf(TEXT("%s  [%s]"), *Item.Name, *ItemStatText(Item));
		Offer.StatsText = ItemStatText(Item);
		Offer.Cost = FMath::Max(8, Item.Price);
		ShopOffers.Add(Offer);
	}

	if (ShopOffers.Num() < 3 && Db.Stats.Num() > 0)
	{
		const WS2::FStatDef& Stat = Db.Stats[Seed % Db.Stats.Num()];
		FWS2Offer Offer;
		Offer.Kind = TEXT("stat");
		Offer.Id = Stat.Id;
		Offer.Stat = Stat.Id;
		Offer.Label = Stat.Name;
		Offer.Amount = 10.f;
		Offer.Cost = 10 + State.Wave * 3;
		ShopOffers.Add(Offer);
	}
}

void AWS2GameMode::OpenShop(bool bLevelUp)
{
	SetFrameTag(bLevelUp ? TEXT("levelup-shop") : TEXT("shop-open"));
	BuildShopOffers();
	bShopOpen = true;
	bLevelUpShop = bLevelUp;
	ShopTimer = AutoRequest > 0 ? 1.4f : 9.f;
	State.Status = bLevelUp ? TEXT("LEVEL-UP  -  WAHL") : TEXT("SHOP  -  WAHL");
	AddToast(bLevelUp ? TEXT("LEVEL-UP BELOHNUNG") : TEXT("SHOP OFFEN"), 2.2f);

	FString Labels;
	for (const FWS2Offer& Offer : ShopOffers)
	{
		Labels += FString::Printf(TEXT("[%s | %s] "), *Offer.Kind, *Offer.Label);
	}
	UE_LOG(LogWS2, Display, TEXT("SHOP open wave=%d rerolls=%d offers=%s auto=%d"),
		State.Wave, RerollCount, *Labels, AutoRequest > 0 ? 1 : 0);
}

void AWS2GameMode::RerollShop()
{
	if (!bShopOpen)
	{
		return;
	}
	const int32 Cost = 5 + State.Wave * 2;
	if (State.Materials < Cost)
	{
		UE_LOG(LogWS2, Display, TEXT("REROLL skipped: %d mats < %d"), State.Materials, Cost);
		return;
	}
	State.Materials -= Cost;
	++RerollCount;
	BuildShopOffers();
	ShopTimer = FMath::Max(ShopTimer, 0.6f);
	State.Status = FString::Printf(TEXT("REROLL %d  (-%d)"), RerollCount, Cost);
	UE_LOG(LogWS2, Display, TEXT("REROLL %d bought for %d mats (wave=%d offers=%d)"),
		RerollCount, Cost, State.Wave, ShopOffers.Num());
}

void AWS2GameMode::PickShop(int32 Index)
{
	if (!ShopOffers.IsValidIndex(Index))
	{
		return;
	}

	SetFrameTag(TEXT("shop-pick"));
	const FWS2Offer Offer = ShopOffers[Index];
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	FString Applied = Offer.Id;

	if (Offer.Kind == TEXT("weapon") && PlayerPawn)
	{
		if (UWS2WeaponComponent* Weapon = PlayerPawn->GetComponentByClass<UWS2WeaponComponent>())
		{
			Weapon->SetWeapon(Offer.Id, Offer.Tier);
			State.WeaponName = Weapon->GetDisplayName();
			Applied = FString::Printf(TEXT("%s T%d"), *Offer.Id, Offer.Tier + 1);
		}
	}
	else if (Offer.Kind == TEXT("item") && PlayerPawn)
	{
		if (const WS2::FItemDef* Item = Db.FindItem(Offer.Id))
		{
			PlayerPawn->ApplyItemStats(Item->Stats);
			++ItemsApplied;
			Applied = FString::Printf(TEXT("item:%s"), *Offer.Id);
		}
	}
	else if (PlayerPawn)
	{
		if (Offer.Stat == TEXT("dmg")) PlayerPawn->DamagePct += Offer.Amount;
		else if (Offer.Stat == TEXT("atkspd")) PlayerPawn->AttackSpeedPct += Offer.Amount;
		else if (Offer.Stat == TEXT("maxhp"))
		{
			PlayerPawn->MaxHp += Offer.Amount;
			PlayerPawn->Hp = FMath::Min(PlayerPawn->MaxHp, PlayerPawn->Hp + Offer.Amount);
		}
		else if (Offer.Stat == TEXT("speed")) PlayerPawn->SpeedPct += Offer.Amount;
		else if (Offer.Stat == TEXT("crit")) PlayerPawn->CritPct += Offer.Amount;
		else if (Offer.Stat == TEXT("range")) PlayerPawn->RangePct += Offer.Amount;
	}

	bShopOpen = false;
	ShopTimer = 0.f;
	State.Materials = FMath::Max(0, State.Materials - Offer.Cost);
	State.Status = FString::Printf(TEXT("%s  (%d)"), *Offer.Label, Offer.Cost);
	AddToast(FString::Printf(TEXT("%s"), *Offer.Label), 2.f);
	ShopPicks += Applied + TEXT(";");

	UE_LOG(LogWS2, Display, TEXT("SHOP pick=[%d] kind=%s id=%s tier=%d cost=%d (level=%d dmg=+%.0f%% speed=+%.0f%% items=%d)"),
		Index, *Offer.Kind, *Offer.Id, Offer.Tier, Offer.Cost, State.Level,
		PlayerPawn ? PlayerPawn->DamagePct : 0.f, PlayerPawn ? PlayerPawn->AttackSpeedPct : 0.f, ItemsApplied);

	if (AutoRequest > 0)
	{
		WaveClearTimer = FMath::Min(WaveClearTimer, 1.1f);
	}
}

void AWS2GameMode::RunWeaponMatrix()
{
	SetFrameTag(TEXT("weapon-matrix"));
	if (!PlayerPawn)
	{
		UE_LOG(LogWS2, Error, TEXT("WEAPON MATRIX skipped: no player pawn"));
		return;
	}
	UWS2WeaponComponent* Weapon = PlayerPawn->GetComponentByClass<UWS2WeaponComponent>();
	if (!Weapon)
	{
		UE_LOG(LogWS2, Error, TEXT("WEAPON MATRIX skipped: no weapon component"));
		return;
	}

	WS2::FDatabase& Db = WS2::FDatabase::Get();
	TMap<FString, int32> KindCounts;
	FString Rows;
	int32 Expected = 0;
	int32 Fired = 0;
	for (const WS2::FWeaponDef& Def : Db.Weapons)
	{
		for (int32 TierIndex = 0; TierIndex < Def.Tiers.Num(); ++TierIndex)
		{
			++Expected;
			Weapon->SetWeapon(Def.Id, TierIndex);
			const bool bFired = Weapon->FireProofOnce();
			if (bFired)
			{
				++Fired;
				++WeaponTiersFired;
			}
			KindCounts.FindOrAdd(Def.Kind)++;
			const FString Behavior = Weapon->GetLastBehavior();
			Rows += FString::Printf(
				TEXT("    { \"weapon\": \"%s\", \"tier\": %d, \"kind\": \"%s\", \"fired\": %s, \"behavior\": \"%s\" }%s\n"),
				*EscapeJson(Def.Id), TierIndex + 1, *EscapeJson(Def.Kind),
				bFired ? TEXT("true") : TEXT("false"), *EscapeJson(Behavior),
				(Expected == Db.NumWeaponTiers()) ? TEXT("") : TEXT(","));
		}
	}

	FString KindJson;
	for (const TPair<FString, int32>& Entry : KindCounts)
	{
		KindJson += FString::Printf(TEXT("\"%s\": %d, "), *Entry.Key, Entry.Value);
	}
	KindJson.RemoveFromEnd(TEXT(", "));

	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WS2"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Json = FString::Printf(
		TEXT("{\n  \"expected\": %d,\n  \"fired\": %d,\n  \"kinds\": { %s },\n  \"rows\": [\n%s  ]\n}\n"),
		Expected, Fired, *KindJson, *Rows);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Dir, TEXT("weapon_matrix.json")));

	UE_LOG(LogWS2, Display, TEXT("WEAPON MATRIX fired=%d/%d kinds=%s -> Saved/WS2/weapon_matrix.json"),
		Fired, Expected, *KindJson);

	// Roster proof: every archetype is spawned at the start and must also leave a
	// kill counter behind. One real area-damage pass clears the ring through the
	// same damage/death path the weapons use (no counter is written by hand).
	if (AllTypesRequest > 0 && Crowd && PlayerPawn)
	{
		const int32 Cleared = Crowd->DamageArea(PlayerPawn->GetArenaPos(), 6000.f, 100000.f, false,
			FString(), 0.f, 0.f, 0.f, 1.f);
		UE_LOG(LogWS2, Display, TEXT("ROSTER PROOF cleared=%d bodies via area damage"), Cleared);
	}

	Weapon->SetWeapon(TEXT("pistol"), 0);
	State.WeaponName = Weapon->GetDisplayName();
	SetFrameTag(TEXT("after-weapon-matrix"));
}

void AWS2GameMode::ApplyAllItems()
{
	if (!PlayerPawn)
	{
		return;
	}
	SetFrameTag(TEXT("items"));
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	for (const WS2::FItemDef& Item : Db.Items)
	{
		PlayerPawn->ApplyItemStats(Item.Stats);
		++ItemsApplied;
	}
	UE_LOG(LogWS2, Display, TEXT("ITEM POOL applied=%d items to pawn (stats=%d elements=%d)"),
		ItemsApplied, ShopPoolStats, Db.Elements.Num());
}

void AWS2GameMode::LogPerformance(float RealTime)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WS2"));
	IFileManager::Get().MakeDirectory(*Dir, true);

	const FString Json = FString::Printf(
		TEXT("{\n  \"t\": %.1f,\n  \"arena\": \"%s\",\n  \"avgFps\": %.2f,\n  \"minFps\": %.2f,\n  \"aliveEnemies\": %d,\n  \"peakEnemies\": %d,\n  \"projectiles\": %d,\n  \"wave\": %d,\n  \"mods\": \"%s\",\n  \"cpu\": \"%s\",\n  \"gpu\": \"%s\"\n}\n"),
		RealTime, *EscapeJson(ArenaId), State.AvgFps, State.MinFps, State.AliveEnemies, State.PeakEnemies,
		State.Projectiles, State.Wave, *EscapeJson(State.ArenaMods),
		*FPlatformMisc::GetCPUBrand(), *FPlatformMisc::GetPrimaryGPUBrand());

	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Dir, TEXT("Perf.json")));
	UE_LOG(LogWS2, Display, TEXT("WS2 PERF t=%.1fs arena=%s avg=%.1ffps min=%.1ffps enemies=%d peak=%d projectiles=%d wave=%d"),
		RealTime, *ArenaId, State.AvgFps, State.MinFps, State.AliveEnemies, State.PeakEnemies, State.Projectiles, State.Wave);
	if (Crowd)
	{
		UE_LOG(LogWS2, Display, TEXT("WS2 RENDER %s"), *Crowd->RenderDiag());
		UE_LOG(LogWS2, Display, TEXT("WS2 SEPARATION pairs=%d worstOverlap=%.2f fires=%d hits=%d kills=%d"),
			Crowd->GetOverlapPairs(), Crowd->GetWorstOverlap(), Crowd->GetProjectileFires(),
			Crowd->GetProjectileHits(), Crowd->GetKillCount());
	}
}

void AWS2GameMode::CheckAutomation(float RealTime)
{
	const int32 ShotAt = bNoScreenshots ? 0 : (ShotRequest > 0 ? ShotRequest : CVarAutoShot.GetValueOnGameThread());
	if (!bShotTaken && ShotAt > 0 && RealTime >= static_cast<float>(ShotAt))
	{
		bShotTaken = true;
		FScreenshotRequest::RequestScreenshot(ScreenshotName, true, false);
		UE_LOG(LogWS2, Display, TEXT("WS2 screenshot requested: %s at %.1fs (arena=%s)"),
			*ScreenshotName, RealTime, *ArenaId);
	}

	const int32 Shot2At = CVarAutoShot2.GetValueOnGameThread();
	if (!bShot2Taken && Shot2At > 0 && RealTime >= static_cast<float>(Shot2At))
	{
		bShot2Taken = true;
		const FString BossShot = ScreenshotName + TEXT("_boss");
		FScreenshotRequest::RequestScreenshot(BossShot, true, false);
		UE_LOG(LogWS2, Display, TEXT("WS2 boss screenshot requested: %s at %.1fs (boss alive=%s)"),
			*BossShot, RealTime, State.bBossAlive ? TEXT("yes") : TEXT("no"));
	}

	const int32 QuitAt = QuitRequest > 0 ? QuitRequest : CVarAutoQuit.GetValueOnGameThread();
	if (QuitAt > 0 && RealTime >= static_cast<float>(QuitAt))
	{
		UE_LOG(LogWS2, Display, TEXT("WS2 auto-quit at %.1fs (avg %.1f fps, peak %d enemies)"),
			RealTime, State.AvgFps, State.PeakEnemies);
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}

void AWS2GameMode::AddKill(int32 Mat, int32 Xp)
{
	++State.Kills;
	State.Xp += FMath::Max(1, Xp);

	while (State.Xp >= State.XpNext)
	{
		State.Xp -= State.XpNext;
		++State.Level;
		State.XpNext = 6 + State.Level * 4;
		if (PlayerPawn)
		{
			PlayerPawn->MaxHp += 6.f;
			PlayerPawn->Hp = FMath::Min(PlayerPawn->MaxHp, PlayerPawn->Hp + 14.f);
			PlayerPawn->DamagePct += 1.f;
		}
		++PendingLevelUps;
		SetFrameTag(TEXT("levelup"));
		UE_LOG(LogWS2, Display, TEXT("LEVELUP level=%d xpNext=%d (hp +6, dmg +1%%)"), State.Level, State.XpNext);
		AddToast(FString::Printf(TEXT("LEVEL %d  -  +6 LP  +1%% SCHADEN"), State.Level), 2.f);
	}
}

void AWS2GameMode::AddMaterials(int32 Amount)
{
	State.Materials += FMath::Max(0, Amount);
}

void AWS2GameMode::OnPlayerDown()
{
	State.Status = TEXT("DOWN - NEUSTART");
	RespawnTimer = 2.6f;
}

void AWS2GameMode::OnWaveCleared()
{
	State.Status = FString::Printf(TEXT("WELLE %d GESCHAFFT"), State.Wave);
	UE_LOG(LogWS2, Display, TEXT("WAVE %d clear: kills=%d level=%d mats=%d hp=%.0f"),
		State.Wave, State.Kills, State.Level, State.Materials, State.Hp);

	const int32 MaxWave = Director ? Director->GetMaxWave() : UWS2WaveDirector::SliceWaves;
	if (State.Wave >= MaxWave)
	{
		OnSliceComplete();
		return;
	}

	OpenShop(PendingLevelUps > 0);
	PendingLevelUps = 0;
	WaveClearTimer = AutoRequest > 0 ? 2.4f : 4.5f;
}

void AWS2GameMode::OnSliceComplete()
{
	if (bSliceDone)
	{
		return;
	}
	bSliceDone = true;
	SliceTimer = 3.f;
	State.Status = FString::Printf(TEXT("ARENA %s GESCHAFFT  -  %d / %d WELLEN"),
		*ArenaId.ToUpper(), State.Wave, Director ? Director->GetMaxWave() : UWS2WaveDirector::SliceWaves);
	AddToast(TEXT("ARENA GESCHAFFT"), 3.f);
	SetFrameTag(TEXT("arena-complete"));
	UE_LOG(LogWS2, Display, TEXT("ARENA COMPLETE arena=%s after wave %d (%d kills, %d shots, %d hits)"),
		*ArenaId, State.Wave, State.Kills, Crowd ? Crowd->GetProjectileFires() : 0,
		Crowd ? Crowd->GetProjectileHits() : 0);
}

void AWS2GameMode::OnWaveStarted(int32 Wave, float Duration)
{
	State.Wave = Wave;
	State.WaveDuration = Duration;
	State.WaveTimeLeft = Duration;
	State.Status = FString::Printf(TEXT("WELLE %d"), Wave);
	UE_LOG(LogWS2, Display, TEXT("WAVE %d start: %.0fs duration"), Wave, Duration);
}

void AWS2GameMode::AddToast(const FString& Text, float Seconds)
{
	State.Toast = Text;
	State.ToastTime = FMath::Max(0.4f, Seconds);
}

void AWS2GameMode::WriteSliceResult()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WS2"));
	IFileManager::Get().MakeDirectory(*Dir, true);

	const FString Json = FString::Printf(
		TEXT("{\n  \"arena\": \"%s\",\n  \"wavesCompleted\": %d,\n  \"sliceComplete\": %s,\n  \"kills\": %d,\n  \"projectilesFired\": %d,\n  \"projectileHits\": %d,\n  \"peakEnemies\": %d,\n  \"avgFps\": %.2f,\n  \"minFps\": %.2f,\n  \"overlapPairs\": %d,\n  \"worstOverlap\": %.2f,\n  \"camera\": { \"dollyBands\": %d, \"bossFrames\": %d, \"shakes\": %d, \"punches\": %d, \"hitStops\": %d },\n  \"shopPicks\": \"%s\",\n  \"rerolls\": %d,\n  \"itemsApplied\": %d,\n  \"weaponTiersFired\": %d,\n  \"shopPool\": { \"weapons\": %d, \"tiers\": %d, \"items\": %d, \"stats\": %d },\n  \"cpu\": \"%s\",\n  \"gpu\": \"%s\"\n}\n"),
		*EscapeJson(ArenaId), State.Wave, bSliceDone ? TEXT("true") : TEXT("false"), State.Kills,
		Crowd ? Crowd->GetProjectileFires() : 0, Crowd ? Crowd->GetProjectileHits() : 0,
		State.PeakEnemies, State.AvgFps, State.MinFps,
		Crowd ? Crowd->GetOverlapPairs() : 0, Crowd ? Crowd->GetWorstOverlap() : 0.f,
		Camera ? Camera->GetDollyBands() : 0, Camera ? Camera->GetBossFrames() : 0,
		Camera ? Camera->GetShakeCount() : 0, Camera ? Camera->GetPunchCount() : 0, HitStopCount,
		*EscapeJson(ShopPicks), RerollCount, ItemsApplied, WeaponTiersFired,
		ShopPoolWeapons, ShopPoolTiers, ShopPoolItems, ShopPoolStats,
		*FPlatformMisc::GetCPUBrand(), *FPlatformMisc::GetPrimaryGPUBrand());

	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Dir, TEXT("slice_result.json")));
	UE_LOG(LogWS2, Display, TEXT("Slice result written to Saved/WS2/slice_result.json"));
}

FString AWS2GameMode::BuildFrameStatsJson(float& OutP99Fps, float& OutWorstWindowFps,
	float& OutStartupMaxMs, float& OutSustainedMaxMs, bool& bOutPass) const
{
	constexpr float WarmupSeconds = 2.f;
	constexpr float BudgetMs = 1000.f / 60.f;

	TArray<float> SustainedMs;
	TArray<FFrameSample> Startup;
	SustainedMs.Reserve(FrameSamples.Num());
	for (const FFrameSample& Sample : FrameSamples)
	{
		if (Sample.Time < WarmupSeconds)
		{
			Startup.Add(Sample);
		}
		else
		{
			SustainedMs.Add(Sample.Ms);
		}
	}

	auto Percentile = [](const TArray<float>& Sorted, float Fraction)
	{
		if (Sorted.Num() == 0)
		{
			return 0.f;
		}
		const int32 Index = FMath::Clamp(FMath::FloorToInt(Fraction * Sorted.Num()), 0, Sorted.Num() - 1);
		return Sorted[Index];
	};

	TArray<float> SortedSustained = SustainedMs;
	SortedSustained.Sort();
	float SustainedSum = 0.f;
	int32 OverBudget = 0;
	for (const float Ms : SustainedMs)
	{
		SustainedSum += Ms;
		if (Ms > BudgetMs) ++OverBudget;
	}
	const float AvgFps = SustainedSum > 0.f ? SustainedMs.Num() * 1000.f / SustainedSum : 0.f;
	const float P50 = Percentile(SortedSustained, 0.50f);
	const float P95 = Percentile(SortedSustained, 0.95f);
	const float P99 = Percentile(SortedSustained, 0.99f);
	const float P999 = Percentile(SortedSustained, 0.999f);
	const float SustainedMax = SortedSustained.Num() > 0 ? SortedSustained.Last() : 0.f;
	OutP99Fps = P99 > 0.f ? 1000.f / P99 : 0.f;
	OutSustainedMaxMs = SustainedMax;

	// One-second windows make a sustained drop visible, while a single compile or
	// allocation hitch only moves the p99 and is listed by name below.
	float WorstWindowFps = 0.f;
	float WorstWindowStart = 0.f;
	int32 Windows = 0;
	int32 WindowsBelow60 = 0;
	for (int32 i = 0; i < FrameSamples.Num(); ++i)
	{
		if (FrameSamples[i].Time < WarmupSeconds)
		{
			continue;
		}
		const float Start = FrameSamples[i].Time;
		int32 End = i;
		while (End < FrameSamples.Num() && FrameSamples[End].Time < Start + 1.f)
		{
			++End;
		}
		const int32 Count = End - i;
		const float Span = FrameSamples[End - 1].Time - Start;
		if (Count < 2 || Span < 0.9f)
		{
			// Do not turn the short tail after the last wave into a fake 2 fps window.
			continue;
		}
		const float Fps = static_cast<float>(Count) / Span;
		++Windows;
		if (WorstWindowFps <= 0.f || Fps < WorstWindowFps)
		{
			WorstWindowFps = Fps;
			WorstWindowStart = Start;
		}
		if (Fps < 60.f) ++WindowsBelow60;
	}
	OutWorstWindowFps = WorstWindowFps;

	float StartupMax = 0.f;
	for (const FFrameSample& Sample : Startup)
	{
		StartupMax = FMath::Max(StartupMax, Sample.Ms);
	}
	OutStartupMaxMs = StartupMax;

	TArray<FFrameSample> Hitches = FrameSamples;
	Hitches.Sort([](const FFrameSample& A, const FFrameSample& B) { return A.Ms > B.Ms; });

	FString StartupHitchRows;
	int32 StartupHitchCount = 0;
	for (const FFrameSample& Sample : Hitches)
	{
		if (Sample.Time >= WarmupSeconds)
		{
			continue;
		}
		if (StartupHitchCount > 0) StartupHitchRows += TEXT(",\n");
		StartupHitchRows += FString::Printf(
			TEXT("    { \"t\": %.3f, \"ms\": %.2f, \"fps\": %.1f, \"tag\": \"%s\" }"),
			Sample.Time, Sample.Ms, Sample.Ms > 0.f ? 1000.f / Sample.Ms : 0.f, *EscapeJson(Sample.Tag));
		if (++StartupHitchCount >= 6) break;
	}

	FString HitchRows;
	int32 HitchCount = 0;
	for (const FFrameSample& Sample : Hitches)
	{
		if (Sample.Ms <= BudgetMs) break;
		if (HitchCount > 0) HitchRows += TEXT(",\n");
		HitchRows += FString::Printf(
			TEXT("    { \"t\": %.3f, \"ms\": %.2f, \"fps\": %.1f, \"startup\": %s, \"tag\": \"%s\" }"),
			Sample.Time, Sample.Ms, Sample.Ms > 0.f ? 1000.f / Sample.Ms : 0.f,
			Sample.Time < WarmupSeconds ? TEXT("true") : TEXT("false"), *EscapeJson(Sample.Tag));
		if (++HitchCount >= 12) break;
	}

	// Coarse histogram: a wide tail with a tiny frame count is a hitch, not a
	// sustained frame-rate problem.
	const float Buckets[] = { 16.667f, 20.f, 25.f, 33.333f, 50.f, 100.f };
	int32 Histogram[7] = { 0, 0, 0, 0, 0, 0, 0 };
	for (const FFrameSample& Sample : FrameSamples)
	{
		int32 Bucket = 0;
		while (Bucket < 6 && Sample.Ms > Buckets[Bucket]) ++Bucket;
		++Histogram[Bucket];
	}

	bOutPass = SustainedMs.Num() > 0 && OutP99Fps >= 60.f && WorstWindowFps >= 60.f;
	return FString::Printf(
		TEXT("{\n    \"samples\": %d,\n    \"warmupSeconds\": %.1f,\n    \"budgetFps\": 60,\n    \"budgetMs\": %.3f,\n    \"startup\": { \"samples\": %d, \"maxMs\": %.2f },\n    \"sustained\": { \"samples\": %d, \"avgFps\": %.2f, \"p50Fps\": %.2f, \"p95Fps\": %.2f, \"p99Fps\": %.2f, \"p99_9Fps\": %.2f, \"maxMs\": %.2f, \"overBudgetFrames\": %d, \"overBudgetPct\": %.3f, \"windows1s\": %d, \"worstWindowFps\": %.2f, \"worstWindowStart\": %.2f, \"windowsBelow60\": %d, \"passes\": %s },\n    \"histogramMs\": [%d, %d, %d, %d, %d, %d, %d],\n    \"startupHitches\": [\n%s\n    ],\n    \"topHitches\": [\n%s\n    ]\n  }"),
		FrameSamples.Num(), WarmupSeconds, BudgetMs, Startup.Num(), StartupMax,
		SustainedMs.Num(), AvgFps, P50 > 0.f ? 1000.f / P50 : 0.f, P95 > 0.f ? 1000.f / P95 : 0.f,
		OutP99Fps, P999 > 0.f ? 1000.f / P999 : 0.f, SustainedMax, OverBudget,
		SustainedMs.Num() > 0 ? 100.f * OverBudget / SustainedMs.Num() : 0.f, Windows, WorstWindowFps,
		WorstWindowStart, WindowsBelow60, bOutPass ? TEXT("true") : TEXT("false"),
		Histogram[0], Histogram[1], Histogram[2], Histogram[3], Histogram[4], Histogram[5], Histogram[6],
		StartupHitchRows.IsEmpty() ? TEXT("") : *StartupHitchRows,
		HitchRows.IsEmpty() ? TEXT("") : *HitchRows);
}

void AWS2GameMode::WriteArenaResult()
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WS2"));
	IFileManager::Get().MakeDirectory(*Dir, true);

	FString SpawnRows;
	FString KillRows;
	int32 SpawnTypes = 0;
	int32 KillTypes = 0;
	int32 SpawnTotal = 0;
	int32 KillTotal = 0;
	if (Crowd)
	{
		const TArray<int32>& Spawns = Crowd->GetEnemySpawnCounts();
		const TArray<int32>& Kills = Crowd->GetEnemyKillCounts();
		for (int32 i = 0; i < Db.Enemies.Num(); ++i)
		{
			const int32 Spawned = Spawns.IsValidIndex(i) ? Spawns[i] : 0;
			const int32 Killed = Kills.IsValidIndex(i) ? Kills[i] : 0;
			SpawnTotal += Spawned;
			KillTotal += Killed;
			if (Spawned > 0) ++SpawnTypes;
			if (Killed > 0) ++KillTypes;
			const FString Comma = (i == Db.Enemies.Num() - 1) ? TEXT("") : TEXT(",");
			SpawnRows += FString::Printf(TEXT("    { \"id\": \"%s\", \"spawn\": %d, \"kill\": %d }%s\n"),
				*EscapeJson(Db.Enemies[i].Id), Spawned, Killed, *Comma);
			KillRows += FString::Printf(TEXT("    \"%s\": %d%s\n"), *EscapeJson(Db.Enemies[i].Id), Killed, *Comma);
		}
	}

	FString BossRows;
	int32 BossCount = 0;
	int32 PhasesReached = 0;
	if (Crowd)
	{
		const TArray<AWS2Crowd::FBossRuntime>& Runtimes = Crowd->GetBossRuntimes();
		for (int32 i = 0; i < Runtimes.Num(); ++i)
		{
			const AWS2Crowd::FBossRuntime& Runtime = Runtimes[i];
			const WS2::FBossDef* Boss = Db.Bosses.IsValidIndex(Runtime.BossDefIndex) ? &Db.Bosses[Runtime.BossDefIndex] : nullptr;
			++BossCount;
			PhasesReached += Runtime.PhaseReached.Num();
			FString PhaseList;
			for (int32 p = 0; p < Runtime.PhaseReached.Num(); ++p)
			{
				PhaseList += FString::Printf(TEXT("%d%s"), Runtime.PhaseReached[p],
					p + 1 < Runtime.PhaseReached.Num() ? TEXT(",") : TEXT(""));
			}
			FString PatternList;
			for (int32 p = 0; p < Runtime.PhasePatterns.Num(); ++p)
			{
				PatternList += Runtime.PhasePatterns[p];
				if (p + 1 < Runtime.PhasePatterns.Num()) PatternList += TEXT(",");
			}
			BossRows += FString::Printf(
				TEXT("    { \"id\": \"%s\", \"name\": \"%s\", \"defeated\": %s, \"phase\": %d, \"phases\": [%s], \"patterns\": \"%s\", \"attacks\": %d, \"hp\": %.1f, \"maxHp\": %.1f }%s\n"),
				Boss ? *EscapeJson(Boss->Id) : TEXT("unknown"),
				Boss ? *EscapeJson(Boss->Name) : TEXT("unknown"),
				Runtime.bDefeated ? TEXT("true") : TEXT("false"), Runtime.Phase,
				*PhaseList, *EscapeJson(PatternList), Runtime.Attacks, Runtime.Hp, Runtime.MaxHp,
				(i == Runtimes.Num() - 1) ? TEXT("") : TEXT(","));
		}
	}

	FString ArenaMods = State.ArenaMods;
	float P99Fps = 0.f, WorstWindowFps = 0.f, StartupMaxMs = 0.f, SustainedMaxMs = 0.f;
	bool bFrameBudgetPass = false;
	const FString FrameStats = BuildFrameStatsJson(P99Fps, WorstWindowFps, StartupMaxMs, SustainedMaxMs, bFrameBudgetPass);
	const FString Json = FString::Printf(
		TEXT("{\n  \"arena\": \"%s\",\n  \"arenaName\": \"%s\",\n  \"kit\": \"%s\",\n  \"mods\": \"%s\",\n  \"complete\": %s,\n  \"waves\": %d,\n  \"maxWaves\": %d,\n  \"kills\": %d,\n  \"killsTotal\": %d,\n  \"spawnsTotal\": %d,\n  \"spawnTypes\": %d,\n  \"killTypes\": %d,\n  \"enemySpawns\": [\n%s  ],\n  \"enemyKills\": {\n%s  },\n  \"bosses\": [\n%s  ],\n  \"bossCount\": %d,\n  \"bossPhaseEntries\": %d,\n  \"weaponTiersFired\": %d,\n  \"itemsApplied\": %d,\n  \"rerolls\": %d,\n  \"shopPicks\": \"%s\",\n  \"overlapPairs\": %d,\n  \"worstOverlap\": %.2f,\n  		\"peakEnemies\": %d,\n  \"avgFps\": %.2f,\n  \"minFps\": %.2f,\n  \"p99Fps\": %.2f,\n  \"worstWindowFps\": %.2f,\n  \"startupMaxFrameMs\": %.2f,\n  \"sustainedMaxFrameMs\": %.2f,\n  \"frameBudgetPass\": %s,\n  \"frameStats\": %s,\n  \"shopPool\": { \"weapons\": %d, \"tiers\": %d, \"items\": %d, \"stats\": %d, \"elements\": %d },\n  \"screenshot\": \"%s\",\n  \"cpu\": \"%s\",\n  \"gpu\": \"%s\"\n}\n"),
		*EscapeJson(ArenaId), *EscapeJson(State.ArenaName), Arena ? *EscapeJson(Arena->GetKitName()) : TEXT("-"),
		*EscapeJson(ArenaMods), bSliceDone ? TEXT("true") : TEXT("false"), State.Wave,
		Director ? Director->GetMaxWave() : 0, State.Kills, KillTotal, SpawnTotal, SpawnTypes, KillTypes,
		*SpawnRows, *KillRows, *BossRows, BossCount, PhasesReached, WeaponTiersFired, ItemsApplied,
		RerollCount, *EscapeJson(ShopPicks), Crowd ? Crowd->GetOverlapPairs() : 0,
		Crowd ? Crowd->GetWorstOverlap() : 0.f, State.PeakEnemies, State.AvgFps, State.MinFps,
		P99Fps, WorstWindowFps, StartupMaxMs, SustainedMaxMs, bFrameBudgetPass ? TEXT("true") : TEXT("false"),
		*FrameStats, ShopPoolWeapons, ShopPoolTiers, ShopPoolItems, ShopPoolStats, Db.Elements.Num(),
		*EscapeJson(ScreenshotName), *FPlatformMisc::GetCPUBrand(), *FPlatformMisc::GetPrimaryGPUBrand());

	const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("arena_%s.json"), *ArenaId));
	FFileHelper::SaveStringToFile(Json, *Path);
	UE_LOG(LogWS2, Display, TEXT("ARENA RESULT written: %s (spawns=%d kills=%d bosses=%d phases=%d tiers=%d items=%d)"),
		*Path, SpawnTotal, KillTotal, BossCount, PhasesReached, WeaponTiersFired, ItemsApplied);
}

void AWS2GameMode::RequestShake(const FVector2D& At, float Amount, float Duration)
{
	if (Camera)
	{
		Camera->AddShake(At, Amount, Duration);
	}
}

void AWS2GameMode::RequestHitStop(float Seconds)
{
	UGameplayStatics::SetGlobalTimeDilation(this, 0.15f);
	HitStopLeft = FMath::Max(HitStopLeft, Seconds);
}
