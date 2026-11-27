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
#include "WS2PackageTrace.h"
#include "WS2Audio.h"
#include "WS2Crowd.h"
#include "WS2GameState.h"
#include "WS2PlayerController.h"
#include "WS2PlayerPawn.h"
#include "WS2PlayerState.h"
#include "WS2SaveGame.h"
#include "WS2WaveDirector.h"
#include "WS2WeaponComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2, Log, All);

bool AWS2GameMode::IsNetVerifyRequested()
{
	int32 Value = 0;
	return FParse::Value(FCommandLine::Get(), TEXT("ws2netverify="), Value) && Value > 0;
}

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
	PlayerStateClass = AWS2PlayerState::StaticClass();
	GameStateClass = AWS2GameState::StaticClass();
	bPauseable = true;
	DefaultPawnClass = nullptr; // the slice spawns and possesses its pawn explicitly
}

void AWS2GameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	StartupOptions = Options;

	const FString AutoOption = UGameplayStatics::ParseOption(Options, TEXT("autostart"));
	bAutostart = AutoOption == TEXT("1") || AutoOption.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	const FString ArenaOption = UGameplayStatics::ParseOption(Options, TEXT("arena"));
	if (!ArenaOption.IsEmpty())
	{
		ArenaId = ArenaOption;
	}
	const FString HeroOption = UGameplayStatics::ParseOption(Options, TEXT("hero"));
	if (!HeroOption.IsEmpty())
	{
		RequestedHeroId = HeroOption;
	}
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
	FParse::Value(FCommandLine::Get(), TEXT("ws2netverify="), NetVerifyRequest);
	bNetVerify = IsNetVerifyRequested() || NetVerifyRequest > 0;
	int32 FullRunRequest = 0;
	FParse::Value(FCommandLine::Get(), TEXT("ws2full="), FullRunRequest);
	bForceFullRun = FullRunRequest > 0;
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
	bProofMode = AutoRequest > 0 || QuitRequest > 0 || AllTypesRequest > 0 || BossProofRequest > 0 ||
		ArmsRequest > 0 || ItemsRequest > 0 || bNetVerify;

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
	LoadProfile();

	SpawnWorld();
	SetFrameTag(TEXT("gameplay"));

	NetState = GetWorld() ? GetWorld()->GetGameState<AWS2GameState>() : nullptr;
	if (NetState)
	{
		NetState->ArenaId = ArenaId;
		NetState->ArenaName = State.ArenaName;
		NetState->ArenaExtent = Arena ? Arena->GetExtent() : FVector2D(1900.f, 1500.f);
		NetState->ArenaMods = State.ArenaMods;
		NetState->Materials = State.Materials;
		NetState->bRunStarted = false;
	}

	// The local player normally logs in before StartPlay. Spawn any controller
	// that is already waiting now that arena and crowd exist.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		EnsurePlayerForController(It->Get());
	}

	if ((bAutostart || bProofMode) && PlayerPawns.Num() > 0)
	{
		StartRun();
	}
}

void AWS2GameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (!NewPlayer)
	{
		return;
	}

	UE_LOG(LogWS2, Display, TEXT("POSTLOGIN %s local=%d"),
		*NewPlayer->GetName(), NewPlayer->IsLocalController() ? 1 : 0);
	WS2PackageTrace::Write(FString::Printf(TEXT("POSTLOGIN %s local=%d worldReady=%d players=%d"),
		*NewPlayer->GetName(), NewPlayer->IsLocalController() ? 1 : 0, bWorldReady ? 1 : 0, PlayerPawns.Num()));
	if (bWorldReady)
	{
		EnsurePlayerForController(NewPlayer);
	}
	if ((bAutostart || bProofMode) && !bRunStarted)
	{
		StartRun();
	}
}

void AWS2GameMode::Logout(AController* Exiting)
{
	APlayerController* PC = Cast<APlayerController>(Exiting);
	if (PC)
	{
		TWeakObjectPtr<AWS2PlayerPawn> WeakPawn = ControllerPawns.FindRef(PC);
		AWS2PlayerPawn* Pawn = WeakPawn.Get();
		if (Pawn)
		{
			PlayerPawns.Remove(Pawn);
			PlayerRespawnTimers.Remove(Pawn);
			if (Crowd)
			{
				Crowd->RemovePlayer(Pawn);
			}
			if (PlayerPawn == Pawn)
		{
				PlayerPawn = PlayerPawns.Num() > 0 ? PlayerPawns[0] : nullptr;
			}
			Pawn->Destroy();
		}
		ControllerPawns.Remove(PC);
		// A client that leaves through QuitGame can lose its pawn before this
		// callback runs, which used to leave a destroyed pawn on the authority
		// roster and crashed the host a few frames later.
		PruneInvalidPlayerPawns();
		if (Crowd)
		{
			Crowd->PrunePlayers();
		}
		UE_LOG(LogWS2, Display, TEXT("LOGOUT %s pawn=%d players=%d"),
			*PC->GetName(), Pawn ? 1 : 0, PlayerPawns.Num());
		WS2PackageTrace::Write(FString::Printf(TEXT("LOGOUT %s pawn=%d players=%d"),
			*PC->GetName(), Pawn ? 1 : 0, PlayerPawns.Num()));
	}
	Super::Logout(Exiting);
}

void AWS2GameMode::PruneInvalidPlayerPawns()
{
	const int32 Before = PlayerPawns.Num();
	PlayerPawns.RemoveAll([](AWS2PlayerPawn* Candidate) { return !IsValid(Candidate); });
	if (!IsValid(PlayerPawn))
	{
		PlayerPawn = PlayerPawns.Num() > 0 ? PlayerPawns[0] : nullptr;
		if (Camera)
		{
			Camera->SetTargetPlayer(PlayerPawn);
		}
	}
	if (PlayerPawns.Num() != Before)
	{
		UE_LOG(LogWS2, Display, TEXT("PRUNE player pawns %d -> %d"), Before, PlayerPawns.Num());
		WS2PackageTrace::Write(FString::Printf(TEXT("PRUNE player pawns %d -> %d"), Before, PlayerPawns.Num()));
	}
}

void AWS2GameMode::EnsurePlayerForController(APlayerController* Controller)
{
	if (!Controller || !Arena || !bWorldReady)
	{
		return;
	}
	if (const TWeakObjectPtr<AWS2PlayerPawn>* Existing = ControllerPawns.Find(Controller))
	{
		if (Existing->IsValid())
		{
			return;
		}
	}

	const int32 Slot = NextPlayerSlot++;
	AWS2PlayerState* PS = Controller->GetPlayerState<AWS2PlayerState>();
	if (PS)
	{
		PS->Slot = Slot;
	}

	FString HeroId = HeroForSlot(Slot);
	if (Slot == 0 && !RequestedHeroId.IsEmpty())
	{
		if (const WS2::FHeroDef* Requested = WS2::FDatabase::Get().FindHero(RequestedHeroId))
		{
			HeroId = Requested->Id;
		}
	}
	const WS2::FHeroDef* Hero = WS2::FDatabase::Get().FindHero(HeroId);
	if (PS && Hero)
	{
		PS->HeroId = Hero->Id;
		PS->HeroName = Hero->Name;
	}

	const FVector Location = SpawnLocationForSlot(Slot);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Controller;
	AWS2PlayerPawn* Pawn = GetWorld()->SpawnActorDeferred<AWS2PlayerPawn>(
		AWS2PlayerPawn::StaticClass(), FTransform(FRotator::ZeroRotator, Location),
		Controller, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pawn)
	{
		return;
	}
	Pawn->SelectedHeroId = HeroId;
	Pawn->FinishSpawning(FTransform(FRotator::ZeroRotator, Location));
	Controller->Possess(Pawn);

	ControllerPawns.Add(Controller, Pawn);
	PlayerPawns.AddUnique(Pawn);
	if (!PlayerPawn)
	{
		PlayerPawn = Pawn;
		State.HeroName = Hero ? Hero->Name : TEXT("Leonidas");
	}
	if (Crowd)
	{
		Crowd->AddPlayer(Pawn);
	}
	if (Camera)
	{
		Camera->SetTargetPlayer(PlayerPawn);
		if (Controller->IsLocalController())
		{
			Controller->SetViewTarget(Camera);
		}
	}

	UE_LOG(LogWS2, Display, TEXT("PLAYER JOIN slot=%d name=%s hero=%s pawn=%s"),
		Slot, *Controller->GetName(), *HeroId, *Pawn->GetName());
	WS2PackageTrace::Write(FString::Printf(TEXT("PLAYER JOIN slot=%d name=%s hero=%s pawn=%s players=%d"),
		Slot, *Controller->GetName(), *HeroId, *Pawn->GetName(), PlayerPawns.Num()));
}

void AWS2GameMode::StartRun()
{
	if (bRunStarted || !Director || !bWorldReady || PlayerPawns.Num() == 0)
	{
		return;
	}
	bRunStarted = true;
	bProfileCommitted = false;
	if (NetState)
	{
		NetState->bRunStarted = true;
	}

	const float BossHpScale = BossProofRequest > 0 ? 0.001f : 1.f;
	const float ProofBossHp = BossProofRequest > 0 ? 600.f : 0.f;
	Director->StartSlice(AllTypesRequest > 0, BossHpScale, ProofBossHp, bProofMode && !bForceFullRun, ShouldSpawnBoss());

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

	if ((EliteRequest > 0 || CVarElite.GetValueOnGameThread() > 0) && Crowd && PlayerPawn)
	{
		if (const WS2::FEnemyDef* BossDef = WS2::FDatabase::Get().FindEnemy(TEXT("juggernaut")))
		{
			const FVector2D PlayerPos = PlayerPawn->GetArenaPos();
			FVector2D BossPos = PlayerPos + FVector2D(0.f, -520.f);
			Arena->ResolveCircle(BossPos, BossDef->Radius * 2.4f);
			Crowd->SpawnEnemy(*BossDef, BossPos, true);
		}
	}

	if (PerfRequest > 0 || CVarPerfMode.GetValueOnGameThread() > 0)
	{
		const int32 Count = PerfRequest > 0 ? PerfRequest : CVarPerfCount.GetValueOnGameThread();
		Director->SpawnPerfHorde(Count);
	}

	State.Status = bProofMode ? TEXT("PROOF RUN") : TEXT("WELLE 1");
	AddToast(TEXT("MULTIPLAYER-KOOP AKTIV"), 2.8f);
	UE_LOG(LogWS2, Display, TEXT("RUN START players=%d full=%d arena=%s hero=%s"),
		PlayerPawns.Num(), IsFullRun() ? 1 : 0, *ArenaId, *State.HeroName);
	WS2PackageTrace::Write(FString::Printf(TEXT("RUN START players=%d full=%d arena=%s hero=%s"),
		PlayerPawns.Num(), IsFullRun() ? 1 : 0, *ArenaId, *State.HeroName));
}

void AWS2GameMode::SetPlayerHero(AWS2PlayerPawn* Pawn, const FString& HeroId)
{
	if (!Pawn || !WS2::FDatabase::Get().FindHero(HeroId))
	{
		return;
	}
	Pawn->ApplyHeroStats(HeroId);
	UE_LOG(LogWS2, Display, TEXT("HERO SET pawn=%s hero=%s"), *Pawn->GetName(), *HeroId);
	if (APlayerController* PC = Pawn->GetController<APlayerController>())
	{
		if (AWS2PlayerState* PS = PC->GetPlayerState<AWS2PlayerState>())
		{
			PS->HeroId = HeroId;
			const WS2::FHeroDef* Hero = WS2::FDatabase::Get().FindHero(HeroId);
			PS->HeroName = Hero ? Hero->Name : HeroId;
		}
	}
}

AWS2PlayerPawn* AWS2GameMode::GetPlayerPawnFor(APlayerController* Controller) const
{
	if (const TWeakObjectPtr<AWS2PlayerPawn>* Found = ControllerPawns.Find(Controller))
	{
		return Found->Get();
	}
	return nullptr;
}

FString AWS2GameMode::HeroForSlot(int32 Slot) const
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	if (Db.Heroes.Num() == 0)
	{
		return TEXT("leonidas");
	}
	return Db.Heroes[FMath::Abs(Slot) % Db.Heroes.Num()].Id;
}

FVector AWS2GameMode::SpawnLocationForSlot(int32 Slot) const
{
	FVector2D Pos = Arena ? Arena->GetCenter() : FVector2D(950.f, 750.f);
	if (Arena)
	{
		const float Angle = Slot * PI * 0.5f + PI * 0.25f;
		Pos += FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 110.f;
		Arena->ResolveCircle(Pos, 40.f);
		const FVector2D World = Arena->ArenaToWorld(Pos);
		return FVector(World.X, World.Y, 42.f);
	}
	return FVector(Pos.X, Pos.Y, 42.f);
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
		Crowd->Init(Arena, nullptr, this);
	}
	if (Camera)
	{
		Camera->SetArenaExtent(Arena ? Arena->GetExtent() : FVector2D(1900.f, 1500.f));
	}

	Director = NewObject<UWS2WaveDirector>(this, TEXT("WaveDirector"));
	AddInstanceComponent(Director);
	Director->RegisterComponent();

	State.ArenaName = ArenaDef->Name;
	State.ArenaMods = Arena ? Arena->GetModSummary() : FString();
	State.Materials = 80;
	State.HeroName = TEXT("Leonidas");

	if (ScreenshotName.IsEmpty())
	{
		ScreenshotName = FString::Printf(TEXT("WS2_%s"), *ArenaId);
	}
	bWorldReady = true;

	UE_LOG(LogWS2, Display,
		TEXT("WS2 arena up: arena=%s name=%s extent=%.0fx%.0f kit=%s mods[%s] waiting for players bossproof=%d alltypes=%d arms=%d items=%d"),
		*ArenaDef->Id, *ArenaDef->Name, ArenaDef->W, ArenaDef->H,
		Arena ? *Arena->GetKitName() : TEXT("-"), *State.ArenaMods,
		BossProofRequest, AllTypesRequest, ArmsRequest, ItemsRequest);
	UE_LOG(LogWS2, Display, TEXT("SHOP POOL weapons=%d tiers=%d items=%d stats=%d elements=%d arenas=%d bosses=%d enemies=%d"),
		ShopPoolWeapons, ShopPoolTiers, ShopPoolItems, ShopPoolStats, Db.Elements.Num(), Db.Arenas.Num(),
		Db.Bosses.Num(), Db.Enemies.Num());
}

void AWS2GameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	PruneInvalidPlayerPawns();

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
	UpdateNetState();

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
			// A netverify run ends through its own COMPLETE line after the window
			// elapses; the slice-end quit would otherwise cut long LAN windows
			// (for example two devices) short before the proof is written.
			if ((AutoRequest > 0 || QuitRequest > 0) && !bNetVerify)
			{
				UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
			}
		}
	}

	for (auto It = PlayerRespawnTimers.CreateIterator(); It; ++It)
	{
		AWS2PlayerPawn* Downed = It.Key();
		if (!IsValid(Downed))
		{
			It.RemoveCurrent();
			continue;
		}
		It.Value() -= DeltaSeconds;
		if (It.Value() <= 0.f)
		{
			const AWS2PlayerState* PS = Downed->GetController() ? Downed->GetController()->GetPlayerState<AWS2PlayerState>() : nullptr;
			Downed->SetActorLocation(SpawnLocationForSlot(PS ? PS->Slot : 0));
			Downed->Heal(Downed->MaxHp);
			It.RemoveCurrent();
		}
	}

	if (RespawnTimer > 0.f)
	{
		RespawnTimer -= DeltaSeconds;
		if (RespawnTimer <= 0.f)
		{
			ReviveAllPlayers();
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
	NetVerifyTick(RealTime);
}

void AWS2GameMode::SpawnNetVerifyTargets()
{
	if (!Crowd || !Arena)
	{
		return;
	}
	const WS2::FEnemyDef* Runner = WS2::FDatabase::Get().FindEnemy(TEXT("runner"));
	if (!Runner)
	{
		return;
	}
	for (int32 Index = 0; Index < PlayerPawns.Num(); ++Index)
	{
		AWS2PlayerPawn* Pawn = PlayerPawns[Index];
		if (!IsValid(Pawn))
		{
			continue;
		}
		const FVector2D Dir = Index == 0 ? FVector2D(-1.f, 0.f) : FVector2D(1.f, 0.f);
		FVector2D Pos = Pawn->GetArenaPos() + Dir * 850.f;
		Arena->ResolveCircle(Pos, 30.f);
		const int32 EnemyIndex = Crowd->SpawnEnemy(*Runner, Pos);
		UE_LOG(LogWS2, Display, TEXT("NETVERIFY SERVER TARGET pawn=%s slot=%d enemy=%d pos=%.0f,%.0f hp=%.0f"),
			*Pawn->GetName(), Index, EnemyIndex, Pos.X, Pos.Y, Runner->Hp);
	}
}

void AWS2GameMode::NotifyNetVerifyPickup(AWS2PlayerPawn* Collector, int32 Amount)
{
	if (!bNetVerify)
	{
		return;
	}
	UE_LOG(LogWS2, Display, TEXT("NETVERIFY SERVER PICKUP collected by=%s amount=%d materials=%d"),
		Collector ? *Collector->GetName() : TEXT("none"), Amount, State.Materials);
	WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY SERVER PICKUP collected by=%s amount=%d materials=%d"),
		Collector ? *Collector->GetName() : TEXT("none"), Amount, State.Materials));
}

void AWS2GameMode::NetVerifyTick(float RealTime)
{
	if (!bNetVerify || !bRunStarted || !Crowd || !Director || !Arena)
	{
		return;
	}
	// A friend can join after the host already started. Add that late player to
	// the authoritative pawn list before the coop verification begins.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		EnsurePlayerForController(It->Get());
	}
	if (PlayerPawns.Num() < 2)
	{
		static bool bLoggedWait = false;
		if (!bLoggedWait)
		{
			bLoggedWait = true;
			WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY SERVER WAIT players=%d"), PlayerPawns.Num()));
		}
		return;
	}
	if (NetVerifyStart < 0.f)
	{
		NetVerifyStart = RealTime;
		NetVerifySpawnTime = RealTime;
		for (AWS2PlayerPawn* Pawn : PlayerPawns)
		{
			if (IsValid(Pawn))
			{
				Pawn->bInvulnerable = true;
			}
		}
		UE_LOG(LogWS2, Display, TEXT("NETVERIFY SERVER START players=%d arena=%s"), PlayerPawns.Num(), *ArenaId);
		WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY SERVER START players=%d arena=%s"), PlayerPawns.Num(), *ArenaId));
		return;
	}

	const float T = RealTime - NetVerifyStart;
	if (!bNetVerifySpawned && T >= 0.6f)
	{
		bNetVerifySpawned = true;
		Crowd->ClearAll();
		SpawnNetVerifyTargets();
	}

	if (bNetVerifySpawned && !bNetVerifyPickupSpawned && T >= 3.0f && PlayerPawns.Num() > 1 && IsValid(PlayerPawns[1]))
	{
		bNetVerifyPickupSpawned = true;
		const FVector2D PickupPos = PlayerPawns[1]->GetArenaPos() + FVector2D(220.f, 0.f);
		Crowd->SpawnPickupAt(PickupPos, 7);
		UE_LOG(LogWS2, Display, TEXT("NETVERIFY SERVER PICKUP spawned pos=%.0f,%.0f amount=7"), PickupPos.X, PickupPos.Y);
	}

	if (bNetVerifySpawned && !bNetVerifyReserveSpawned && T >= 3.8f)
	{
		bNetVerifyReserveSpawned = true;
		const WS2::FEnemyDef* Tank = WS2::FDatabase::Get().FindEnemy(TEXT("tank"));
		if (Tank)
		{
			for (int32 Index = 0; Index < PlayerPawns.Num(); ++Index)
			{
				AWS2PlayerPawn* Pawn = PlayerPawns[Index];
				if (!IsValid(Pawn))
				{
					continue;
				}
				const FVector2D Dir = Index == 0 ? FVector2D(-1.f, 0.f) : FVector2D(1.f, 0.f);
				FVector2D Pos = Pawn->GetArenaPos() + Dir * 420.f;
				Arena->ResolveCircle(Pos, Tank->Radius * 1.5f);
				const int32 EnemyIndex = Crowd->SpawnEnemy(*Tank, Pos);
				UE_LOG(LogWS2, Display, TEXT("NETVERIFY SERVER RESERVE pawn=%s slot=%d enemy=%d pos=%.0f,%.0f hp=%.0f"),
					*Pawn->GetName(), Index, EnemyIndex, Pos.X, Pos.Y, Tank->Hp);
			}
		}
	}

	if (bNetVerifySpawned && !bNetVerifyWaveShifted && T >= 6.5f)
	{
		bNetVerifyWaveShifted = true;
		const int32 PreviousWave = Director->GetWave();
		Director->StartWave(2);
		UE_LOG(LogWS2, Display, TEXT("NETVERIFY SERVER WAVE SHIFT %d -> 2"), PreviousWave);
		WS2PackageTrace::Write(FString::Printf(TEXT("NETVERIFY SERVER WAVE SHIFT %d -> 2"), PreviousWave));
	}
}

void AWS2GameMode::UpdateState(float DeltaSeconds)
{
	if (PlayerPawn)
	{
		PlayerPawn->bInvulnerable = InvulnRequest > 0 || CVarInvulnerable.GetValueOnGameThread() > 0;
		State.Hp = PlayerPawn->Hp;
		State.MaxHp = PlayerPawn->MaxHp;
		State.bPlayerDown = PlayerPawn->bDown;

	}
	// Hazards act on every co-op player, not just the host's pawn.
	if (Arena)
	{
		for (AWS2PlayerPawn* Pawn : PlayerPawns)
		{
			if (!IsValid(Pawn) || Pawn->bInvulnerable)
			{
				continue;
			}
			const float Dps = Arena->GetPoisonDpsAt(Pawn->GetArenaPos());
			if (Dps > 0.f)
			{
				Pawn->ApplyContactDamage(Dps * DeltaSeconds);
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
		if (bBoss && !bBossWasAlive && NetState)
		{
			const FVector2D BossWorld = Arena ? Arena->ArenaToWorld(BossPos) : FVector2D::ZeroVector;
			NetState->MulticastSfx(static_cast<uint8>(WS2Audio::ESfx::Boss), FVector(BossWorld.X, BossWorld.Y, 0.f));
		}

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

void AWS2GameMode::UpdateNetState()
{
	if (!NetState)
	{
		NetState = GetWorld() ? GetWorld()->GetGameState<AWS2GameState>() : nullptr;
		if (!NetState)
		{
			return;
		}
	}

	NetState->bRunStarted = bRunStarted;
	NetState->Wave = State.Wave;
	NetState->WaveTimeLeft = State.WaveTimeLeft;
	NetState->WaveDuration = State.WaveDuration;
	NetState->bWaveActive = State.bWaveActive;
	NetState->Kills = State.Kills;
	NetState->Materials = State.Materials;
	NetState->AliveEnemies = State.AliveEnemies;
	NetState->PeakEnemies = State.PeakEnemies;
	NetState->Projectiles = State.Projectiles;
	NetState->PickupsOnFloor = Crowd ? Crowd->GetPickupCount() : 0;
	NetState->AvgFps = State.AvgFps;
	NetState->bBossAlive = State.bBossAlive;
	NetState->BossName = State.BossName;
	NetState->BossHpPct = State.BossHpPct;
	NetState->BossPhase = State.BossPhase;
	NetState->ArenaName = State.ArenaName;
	NetState->ArenaMods = State.ArenaMods;
	NetState->ArenaExtent = Arena ? Arena->GetExtent() : FVector2D(1900.f, 1500.f);
	NetState->Status = State.Status;
	NetState->Toast = State.Toast;
	NetState->ToastTime = State.ToastTime;
	NetState->bShopOpen = bShopOpen;
	NetState->bLevelUp = bLevelUpShop;
	NetState->BestWave = Profile ? Profile->BestWave : 0;
	NetState->BestKills = Profile ? Profile->BestKills : 0;
	NetState->TotalRuns = Profile ? Profile->Runs : 0;
	NetState->ShopOffers.Reset(ShopOffers.Num());
	for (const FWS2Offer& Offer : ShopOffers)
	{
		NetState->ShopOffers.Add(FString::Printf(TEXT("%s  (%d)"), *Offer.Label, Offer.Cost));
	}

	if (Crowd && Arena)
	{
		FString BossName;
		FVector2D BossPos;
		float BossHp = 0.f;
		if (Crowd->GetBossInfo(BossName, BossPos, BossHp))
		{
			const FVector2D World = Arena->ArenaToWorld(BossPos);
			NetState->BossWorldPos = World;
		}
	}

	TArray<FWS2CoopPlayer> Rows;
	Rows.Reserve(PlayerPawns.Num());
	for (int32 Index = 0; Index < PlayerPawns.Num(); ++Index)
	{
		AWS2PlayerPawn* Pawn = PlayerPawns[Index];
		if (!IsValid(Pawn))
		{
			continue;
		}
		AWS2PlayerState* PS = Pawn->GetController() ? Pawn->GetController()->GetPlayerState<AWS2PlayerState>() : nullptr;
		FWS2CoopPlayer Row;
		Row.Slot = PS ? PS->Slot : Index;
		Row.PlayerName = PS ? PS->GetPlayerName() : FString::Printf(TEXT("Spieler %d"), Index + 1);
		Row.HeroId = PS ? PS->HeroId : Pawn->SelectedHeroId;
		Row.HeroName = PS && !PS->HeroName.IsEmpty() ? PS->HeroName : Pawn->SelectedHeroId;
		if (Row.HeroName.IsEmpty())
		{
			Row.HeroName = Pawn->GetHeroName();
		}
		Row.WeaponName = Pawn->GetWeaponDisplayName();
		Row.Hp = Pawn->Hp;
		Row.MaxHp = Pawn->MaxHp;
		Row.Level = State.Level;
		Row.Xp = State.Xp;
		Row.XpNext = State.XpNext;
		Row.Kills = State.Kills;
		Row.bDown = Pawn->bDown;
		Rows.Add(Row);
	}
	NetState->Players = Rows;
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

	// A level-up reward spends its third slot on a stat so that the whole pool
	// (weapons, tiers, items, stats, elements) is really offered, not just loaded.
	const int32 ItemSlots = bLevelUpShop ? 1 : 2;
	for (int32 Slot = 0; Slot < ItemSlots && Db.Items.Num() > 0; ++Slot)
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

	TrackShopOffers();
}

void AWS2GameMode::TrackShopOffers()
{
	const WS2::FDatabase& Db = WS2::FDatabase::Get();
	for (const FWS2Offer& Offer : ShopOffers)
	{
		if (Offer.Kind == TEXT("weapon"))
		{
			OfferedWeaponIds.Add(Offer.Id);
			OfferedTiers.Add(FString::Printf(TEXT("%s@T%d"), *Offer.Id, Offer.Tier + 1));
			if (const WS2::FWeaponDef* Weapon = Db.FindWeapon(Offer.Id))
			{
				if (Weapon->Tiers.IsValidIndex(Offer.Tier) && !Weapon->Tiers[Offer.Tier].Element.IsEmpty())
				{
					OfferedElements.Add(Weapon->Tiers[Offer.Tier].Element);
				}
			}
		}
		else if (Offer.Kind == TEXT("item"))
		{
			OfferedItemIds.Add(Offer.Id);
		}
		else
		{
			OfferedStatIds.Add(Offer.Id);
		}
	}
}

FString AWS2GameMode::DescribeOffersJson() const
{
	FString Out;
	for (int32 i = 0; i < ShopOffers.Num(); ++i)
	{
		const FWS2Offer& Offer = ShopOffers[i];
		FString Token;
		if (Offer.Kind == TEXT("weapon"))
		{
			Token = FString::Printf(TEXT("weapon:%s@T%d"), *Offer.Id, Offer.Tier + 1);
		}
		else if (Offer.Kind == TEXT("item"))
		{
			Token = FString::Printf(TEXT("item:%s"), *Offer.Id);
		}
		else
		{
			Token = FString::Printf(TEXT("stat:%s"), *Offer.Id);
		}
		if (i > 0)
		{
			Out += TEXT(", ");
		}
		Out += FString::Printf(TEXT("\"%s\""), *EscapeJson(Token));
	}
	return Out;
}

void AWS2GameMode::OpenShop(bool bLevelUp)
{
	SetFrameTag(bLevelUp ? TEXT("levelup-shop") : TEXT("shop-open"));
	bShopOpen = true;
	bLevelUpShop = bLevelUp;
	BuildShopOffers();
	ShopTimer = AutoRequest > 0 ? 1.4f : 9.f;
	State.Status = bLevelUp ? TEXT("LEVEL-UP  -  WAHL") : TEXT("SHOP  -  WAHL");
	AddToast(bLevelUp ? TEXT("LEVEL-UP BELOHNUNG") : TEXT("SHOP OFFEN"), 2.2f);
	if (NetState)
	{
		NetState->MulticastSfx(static_cast<uint8>(WS2Audio::ESfx::Shop), FVector::ZeroVector);
	}

	FShopEvent Event;
	Event.Wave = State.Wave;
	Event.Level = State.Level;
	Event.bLevelUp = bLevelUp;
	Event.OffersJson = DescribeOffersJson();
	ShopEvents.Add(Event);
	if (bLevelUp)
	{
		++LevelUpShops;
	}

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
	if (ShopEvents.Num() > 0)
	{
		FShopEvent& Event = ShopEvents.Last();
		const FString Rerolled = DescribeOffersJson();
		if (!Event.OffersJson.IsEmpty() && !Rerolled.IsEmpty())
		{
			Event.OffersJson += TEXT(", ");
		}
		Event.OffersJson += Rerolled;
	}
	ShopTimer = FMath::Max(ShopTimer, 0.6f);
	State.Status = FString::Printf(TEXT("REROLL %d  (-%d)"), RerollCount, Cost);
	UE_LOG(LogWS2, Display, TEXT("REROLL %d bought for %d mats (wave=%d offers=%d)"),
		RerollCount, Cost, State.Wave, ShopOffers.Num());
}

void AWS2GameMode::PickShop(int32 Index, AWS2PlayerPawn* Buyer)
{
	if (!ShopOffers.IsValidIndex(Index))
	{
		return;
	}

	SetFrameTag(TEXT("shop-pick"));
	const FWS2Offer Offer = ShopOffers[Index];
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	FString Applied = Offer.Id;
	AWS2PlayerPawn* Target = Buyer ? Buyer : PlayerPawn;

	if (Offer.Kind == TEXT("weapon") && Target)
	{
		if (Target->AddOrUpgradeWeapon(Offer.Id, Offer.Tier))
		{
			Applied = FString::Printf(TEXT("%s T%d"), *Offer.Id, Offer.Tier + 1);
		}
	}
	else if (Offer.Kind == TEXT("item") && Target)
	{
		if (const WS2::FItemDef* Item = Db.FindItem(Offer.Id))
		{
			Target->ApplyItemStats(Item->Stats);
			++ItemsApplied;
			Applied = FString::Printf(TEXT("item:%s"), *Offer.Id);
		}
	}
	else if (Target)
	{
		if (Offer.Stat == TEXT("dmg")) Target->DamagePct += Offer.Amount;
		else if (Offer.Stat == TEXT("atkspd")) Target->AttackSpeedPct += Offer.Amount;
		else if (Offer.Stat == TEXT("maxhp"))
		{
			Target->MaxHp += Offer.Amount;
			Target->Hp = FMath::Min(Target->MaxHp, Target->Hp + Offer.Amount);
		}
		else if (Offer.Stat == TEXT("speed")) Target->SpeedPct += Offer.Amount;
		else if (Offer.Stat == TEXT("crit")) Target->CritPct += Offer.Amount;
		else if (Offer.Stat == TEXT("range")) Target->RangePct += Offer.Amount;
	}

	bShopOpen = false;
	ShopTimer = 0.f;
	State.Materials = FMath::Max(0, State.Materials - Offer.Cost);
	State.Status = FString::Printf(TEXT("%s  (%d)"), *Offer.Label, Offer.Cost);
	AddToast(FString::Printf(TEXT("%s"), *Offer.Label), 2.f);
	ShopPicks += Applied + TEXT(";");
	if (ShopEvents.Num() > 0)
	{
		ShopEvents.Last().Pick = Applied;
	}

	UE_LOG(LogWS2, Display, TEXT("SHOP pick=[%d] kind=%s id=%s tier=%d cost=%d (level=%d dmg=+%.0f%% speed=+%.0f%% items=%d)"),
		Index, *Offer.Kind, *Offer.Id, Offer.Tier, Offer.Cost, State.Level,
		Target ? Target->DamagePct : 0.f, Target ? Target->AttackSpeedPct : 0.f, ItemsApplied);

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
	UWS2WeaponComponent* Weapon = PlayerPawn->GetWeapon();
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
		for (AWS2PlayerPawn* Pawn : PlayerPawns)
		{
			if (IsValid(Pawn))
			{
				Pawn->MaxHp += 6.f;
				Pawn->Hp = FMath::Min(Pawn->MaxHp, Pawn->Hp + 14.f);
				Pawn->DamagePct += 1.f;
			}
		}
		++PendingLevelUps;
		++LevelUpCount;
		SetFrameTag(TEXT("levelup"));
		UE_LOG(LogWS2, Display, TEXT("LEVELUP level=%d xpNext=%d (hp +6, dmg +1%%)"), State.Level, State.XpNext);
		if (NetState)
		{
			NetState->MulticastSfx(static_cast<uint8>(WS2Audio::ESfx::LevelUp), FVector::ZeroVector);
		}
		AddToast(FString::Printf(TEXT("LEVEL %d  -  +6 LP  +1%% SCHADEN"), State.Level), 2.f);
	}
}

void AWS2GameMode::AddMaterials(int32 Amount)
{
	State.Materials += FMath::Max(0, Amount);
}

void AWS2GameMode::OnPlayerDown(AWS2PlayerPawn* DownedPawn)
{
	if (!DownedPawn || RespawnTimer > 0.f)
	{
		return;
	}
	if (NetState)
	{
		NetState->MulticastSfx(static_cast<uint8>(WS2Audio::ESfx::Down), DownedPawn->GetActorLocation());
	}

	int32 Standing = 0;
	for (AWS2PlayerPawn* Pawn : PlayerPawns)
	{
		if (IsValid(Pawn) && !Pawn->bDown)
		{
			++Standing;
		}
	}

	if (Standing == 0)
	{
		State.Status = TEXT("TEAM DOWN - NEUSTART");
		RespawnTimer = 4.5f;
		AddToast(TEXT("TEAM DOWN"), 3.f);
		return;
	}

	PlayerRespawnTimers.FindOrAdd(DownedPawn) = 3.2f;
	State.Status = FString::Printf(TEXT("%s DOWN - %.0fs"), *DownedPawn->GetHeroName(), 3.2f);
	AddToast(FString::Printf(TEXT("%s wird wiederbelebt"), *DownedPawn->GetHeroName()), 2.2f);
}

void AWS2GameMode::ReviveAllPlayers()
{
	for (int32 Index = 0; Index < PlayerPawns.Num(); ++Index)
	{
		AWS2PlayerPawn* Pawn = PlayerPawns[Index];
		if (!IsValid(Pawn))
		{
			continue;
		}
		Pawn->SetActorLocation(SpawnLocationForSlot(Index));
		Pawn->bDown = false;
		Pawn->Heal(Pawn->MaxHp);
	}
	PlayerRespawnTimers.Reset();
	RespawnTimer = 0.f;
	State.Status = TEXT("NEUSTART");
	if (Crowd)
	{
		Crowd->ClearAll();
	}
	if (Director)
	{
		Director->StartWave(1);
	}
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
	WaveClearTimer = bForceFullRun ? 0.4f : (AutoRequest > 0 ? 2.4f : 4.5f);
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
	if (NetState)
	{
		NetState->MulticastSfx(static_cast<uint8>(WS2Audio::ESfx::Win), FVector::ZeroVector);
	}
	SetFrameTag(TEXT("arena-complete"));
	UE_LOG(LogWS2, Display, TEXT("ARENA COMPLETE arena=%s after wave %d (%d kills, %d shots, %d hits)"),
		*ArenaId, State.Wave, State.Kills, Crowd ? Crowd->GetProjectileFires() : 0,
		Crowd ? Crowd->GetProjectileHits() : 0);
	CommitProfile();
}

void AWS2GameMode::LoadProfile()
{
	Profile = UGameplayStatics::DoesSaveGameExist(TEXT("WS2Profile"), 0)
		? Cast<UWS2SaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("WS2Profile"), 0))
		: nullptr;
	if (!Profile)
	{
		Profile = Cast<UWS2SaveGame>(UGameplayStatics::CreateSaveGameObject(UWS2SaveGame::StaticClass()));
	}
	UE_LOG(LogWS2, Display, TEXT("PROFILE loaded: bestWave=%d bestKills=%d runs=%d totalKills=%d"),
		Profile ? Profile->BestWave : 0, Profile ? Profile->BestKills : 0,
		Profile ? Profile->Runs : 0, Profile ? Profile->TotalKills : 0);
}

void AWS2GameMode::CommitProfile()
{
	if (!Profile || bProfileCommitted || !IsFullRun())
	{
		return;
	}
	bProfileCommitted = true;
	++Profile->Runs;
	Profile->TotalKills += State.Kills;
	if (State.Wave > Profile->BestWave)
	{
		Profile->BestWave = State.Wave;
		Profile->BestKills = State.Kills;
		Profile->BestArena = ArenaId;
	}
	Profile->bArenaCleared = true;
	UGameplayStatics::SaveGameToSlot(Profile, TEXT("WS2Profile"), 0);
	UE_LOG(LogWS2, Display, TEXT("PROFILE saved: bestWave=%d bestKills=%d runs=%d"),
		Profile->BestWave, Profile->BestKills, Profile->Runs);
}

void AWS2GameMode::OnWaveStarted(int32 Wave, float Duration)
{
	State.Wave = Wave;
	State.WaveDuration = Duration;
	State.WaveTimeLeft = Duration;
	State.Status = FString::Printf(TEXT("WELLE %d"), Wave);
	UE_LOG(LogWS2, Display, TEXT("WAVE %d start: %.0fs duration"), Wave, Duration);
	if (NetState)
	{
		NetState->MulticastSfx(static_cast<uint8>(WS2Audio::ESfx::Wave), FVector::ZeroVector);
	}
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

FString AWS2GameMode::BuildLevelUpsJson() const
{
	FString Rows;
	for (int32 i = 0; i < ShopEvents.Num(); ++i)
	{
		const FShopEvent& Event = ShopEvents[i];
		Rows += FString::Printf(
			TEXT("    { \"wave\": %d, \"level\": %d, \"levelUp\": %s, \"offers\": [%s], \"pick\": \"%s\" }%s\n"),
			Event.Wave, Event.Level, Event.bLevelUp ? TEXT("true") : TEXT("false"),
			*Event.OffersJson, *EscapeJson(Event.Pick), (i + 1 < ShopEvents.Num()) ? TEXT(",") : TEXT(""));
	}
	return FString::Printf(
		TEXT("{ \"count\": %d, \"finalLevel\": %d, \"levelUpShops\": %d, \"shopOpens\": %d, \"pendingAtEnd\": %d, \"events\": [\n%s    ] }"),
		LevelUpCount, State.Level, LevelUpShops, ShopEvents.Num(), PendingLevelUps, *Rows);
}

FString AWS2GameMode::BuildShopPoolProofJson() const
{
	WS2::FDatabase& Db = WS2::FDatabase::Get();

	// The level-up reward screen draws from the complete database through the
	// same deterministic seed as BuildShopOffers(). Sweeping the run's wave /
	// level / reroll space shows that every pool entry can be presented, and
	// therefore that the whole pool is offerable - not just the pool sizes.
	TSet<int32> ReachWeapons;
	TSet<int32> ReachTiers;
	TSet<int32> ReachItems;
	TSet<int32> ReachStats;
	TSet<FString> ReachElements;
	for (int32 Wave = 1; Wave <= 32; ++Wave)
	{
		for (int32 Level = 0; Level <= 48; ++Level)
		{
			for (int32 Reroll = 0; Reroll <= 12; ++Reroll)
			{
				const int32 Seed = Wave * 31 + Level * 7 + Reroll * 101;
				if (Db.Weapons.Num() > 0)
				{
					const int32 WeaponIndex = Seed % Db.Weapons.Num();
					const WS2::FWeaponDef& Weapon = Db.Weapons[WeaponIndex];
					const int32 TierIndex = (Seed + Level) % FMath::Max(1, Weapon.Tiers.Num());
					ReachWeapons.Add(WeaponIndex);
					ReachTiers.Add(WeaponIndex * 1000 + TierIndex);
					if (Weapon.Tiers.IsValidIndex(TierIndex) && !Weapon.Tiers[TierIndex].Element.IsEmpty())
					{
						ReachElements.Add(Weapon.Tiers[TierIndex].Element);
					}
				}
				for (int32 Slot = 0; Slot < 2 && Db.Items.Num() > 0; ++Slot)
				{
					ReachItems.Add((Seed * (Slot + 2) + Slot * 17) % Db.Items.Num());
				}
				if (Db.Stats.Num() > 0)
				{
					ReachStats.Add(Seed % Db.Stats.Num());
				}
			}
		}
	}

	const int32 PoolWeapons = Db.Weapons.Num();
	const int32 PoolTiers = Db.NumWeaponTiers();
	const int32 PoolItems = Db.Items.Num();
	const int32 PoolStats = Db.Stats.Num();
	const int32 PoolElements = Db.Elements.Num();
	const bool bComplete = PoolWeapons == 40 && PoolTiers == 160 && PoolItems == 44 && PoolStats == 24 && PoolElements == 5;
	const bool bOfferable = ReachWeapons.Num() == PoolWeapons && ReachTiers.Num() == PoolTiers &&
		ReachItems.Num() == PoolItems && ReachStats.Num() == PoolStats && ReachElements.Num() == PoolElements;

	return FString::Printf(
		TEXT("{ \"weapons\": %d, \"tiers\": %d, \"items\": %d, \"stats\": %d, \"elements\": %d,\n")
		TEXT("  \"expected\": { \"weapons\": 40, \"tiers\": 160, \"items\": 44, \"stats\": 24, \"elements\": 5 },\n")
		TEXT("  \"complete\": %s,\n")
		TEXT("  \"offerable\": { \"weapons\": %d, \"tiers\": %d, \"items\": %d, \"stats\": %d, \"elements\": %d },\n")
		TEXT("  \"offerableMethod\": \"BuildShopOffers seed sweep (wave 1-32, level 0-48, reroll 0-12)\",\n")
		TEXT("  \"fullPoolOfferable\": %s,\n")
		TEXT("  \"offeredInRun\": { \"weapons\": %d, \"tiers\": %d, \"items\": %d, \"stats\": %d, \"elements\": %d } }"),
		PoolWeapons, PoolTiers, PoolItems, PoolStats, PoolElements,
		bComplete ? TEXT("true") : TEXT("false"),
		ReachWeapons.Num(), ReachTiers.Num(), ReachItems.Num(), ReachStats.Num(), ReachElements.Num(),
		bOfferable ? TEXT("true") : TEXT("false"),
		OfferedWeaponIds.Num(), OfferedTiers.Num(), OfferedItemIds.Num(), OfferedStatIds.Num(), OfferedElements.Num());
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
	const FString LevelUpsJson = BuildLevelUpsJson();
	const FString ShopPoolJson = BuildShopPoolProofJson();
	const FString Json = FString::Printf(
		TEXT("{\n  \"arena\": \"%s\",\n  \"arenaName\": \"%s\",\n  \"kit\": \"%s\",\n  \"mods\": \"%s\",\n  \"complete\": %s,\n  \"waves\": %d,\n  \"maxWaves\": %d,\n  \"kills\": %d,\n  \"killsTotal\": %d,\n  \"spawnsTotal\": %d,\n  \"spawnTypes\": %d,\n  \"killTypes\": %d,\n  \"enemySpawns\": [\n%s  ],\n  \"enemyKills\": {\n%s  },\n  \"bosses\": [\n%s  ],\n  \"bossCount\": %d,\n  \"bossPhaseEntries\": %d,\n  \"weaponTiersFired\": %d,\n  \"itemsApplied\": %d,\n  \"rerolls\": %d,\n  \"shopPicks\": \"%s\",\n  \"overlapPairs\": %d,\n  \"worstOverlap\": %.2f,\n  		\"peakEnemies\": %d,\n  \"avgFps\": %.2f,\n  \"minFps\": %.2f,\n  \"p99Fps\": %.2f,\n  \"worstWindowFps\": %.2f,\n  \"startupMaxFrameMs\": %.2f,\n  \"sustainedMaxFrameMs\": %.2f,\n  \"frameBudgetPass\": %s,\n  \"frameStats\": %s,\n  \"levelUps\": %s,\n  \"shopPool\": %s,\n  \"screenshot\": \"%s\",\n  \"cpu\": \"%s\",\n  \"gpu\": \"%s\"\n}\n"),
		*EscapeJson(ArenaId), *EscapeJson(State.ArenaName), Arena ? *EscapeJson(Arena->GetKitName()) : TEXT("-"),
		*EscapeJson(ArenaMods), bSliceDone ? TEXT("true") : TEXT("false"), State.Wave,
		Director ? Director->GetMaxWave() : 0, State.Kills, KillTotal, SpawnTotal, SpawnTypes, KillTypes,
		*SpawnRows, *KillRows, *BossRows, BossCount, PhasesReached, WeaponTiersFired, ItemsApplied,
		RerollCount, *EscapeJson(ShopPicks), Crowd ? Crowd->GetOverlapPairs() : 0,
		Crowd ? Crowd->GetWorstOverlap() : 0.f, State.PeakEnemies, State.AvgFps, State.MinFps,
		P99Fps, WorstWindowFps, StartupMaxMs, SustainedMaxMs, bFrameBudgetPass ? TEXT("true") : TEXT("false"),
		*FrameStats, *LevelUpsJson, *ShopPoolJson,
		*EscapeJson(ScreenshotName), *FPlatformMisc::GetCPUBrand(), *FPlatformMisc::GetPrimaryGPUBrand());

	const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("arena_%s.json"), *ArenaId));
	FFileHelper::SaveStringToFile(Json, *Path);
	UE_LOG(LogWS2, Display, TEXT("ARENA RESULT written: %s (spawns=%d kills=%d bosses=%d phases=%d tiers=%d items=%d)"),
		*Path, SpawnTotal, KillTotal, BossCount, PhasesReached, WeaponTiersFired, ItemsApplied);
	UE_LOG(LogWS2, Display, TEXT("LEVEL-UP PROOF: levelUps=%d finalLevel=%d levelUpShops=%d pool(weapons=%d tiers=%d items=%d stats=%d elements=%d)"),
		LevelUpCount, State.Level, LevelUpShops, ShopPoolWeapons, ShopPoolTiers, ShopPoolItems, ShopPoolStats,
		WS2::FDatabase::Get().Elements.Num());
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
