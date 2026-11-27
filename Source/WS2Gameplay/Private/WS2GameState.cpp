#include "WS2GameState.h"

#include "Net/UnrealNetwork.h"
#include "WS2Audio.h"

AWS2GameState::AWS2GameState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	SetMinNetUpdateFrequency(5.f);
}

void AWS2GameState::MulticastSfx_Implementation(uint8 Sfx, FVector Location)
{
	WS2Audio::Play(GetWorld(), static_cast<WS2Audio::ESfx>(Sfx), Location, 0.5f);
}

void AWS2GameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWS2GameState, bRunStarted);
	DOREPLIFETIME(AWS2GameState, Wave);
	DOREPLIFETIME(AWS2GameState, WaveTimeLeft);
	DOREPLIFETIME(AWS2GameState, WaveDuration);
	DOREPLIFETIME(AWS2GameState, bWaveActive);
	DOREPLIFETIME(AWS2GameState, Kills);
	DOREPLIFETIME(AWS2GameState, Materials);
	DOREPLIFETIME(AWS2GameState, AliveEnemies);
	DOREPLIFETIME(AWS2GameState, PeakEnemies);
	DOREPLIFETIME(AWS2GameState, Projectiles);
	DOREPLIFETIME(AWS2GameState, PickupsOnFloor);
	DOREPLIFETIME(AWS2GameState, AvgFps);
	DOREPLIFETIME(AWS2GameState, bBossAlive);
	DOREPLIFETIME(AWS2GameState, BossName);
	DOREPLIFETIME(AWS2GameState, BossHpPct);
	DOREPLIFETIME(AWS2GameState, BossPhase);
	DOREPLIFETIME(AWS2GameState, BossWorldPos);
	DOREPLIFETIME(AWS2GameState, ArenaName);
	DOREPLIFETIME(AWS2GameState, ArenaId);
	DOREPLIFETIME(AWS2GameState, ArenaMods);
	DOREPLIFETIME(AWS2GameState, ArenaExtent);
	DOREPLIFETIME(AWS2GameState, Status);
	DOREPLIFETIME(AWS2GameState, Toast);
	DOREPLIFETIME(AWS2GameState, ToastTime);
	DOREPLIFETIME(AWS2GameState, bShopOpen);
	DOREPLIFETIME(AWS2GameState, bLevelUp);
	DOREPLIFETIME(AWS2GameState, ShopOffers);
	DOREPLIFETIME(AWS2GameState, Players);
	DOREPLIFETIME(AWS2GameState, BestWave);
	DOREPLIFETIME(AWS2GameState, BestKills);
	DOREPLIFETIME(AWS2GameState, TotalRuns);
}

void AWS2GameState::BuildHudState(int32 InSlot, FWS2RunState& OutState) const
{
	OutState = FWS2RunState();
	OutState.Wave = Wave;
	OutState.WaveTimeLeft = WaveTimeLeft;
	OutState.WaveDuration = WaveDuration;
	OutState.bWaveActive = bWaveActive;
	OutState.Kills = Kills;
	OutState.Materials = Materials;
	OutState.AliveEnemies = AliveEnemies;
	OutState.PeakEnemies = PeakEnemies;
	OutState.Projectiles = Projectiles;
	OutState.AvgFps = AvgFps;
	OutState.bBossAlive = bBossAlive;
	OutState.BossName = BossName;
	OutState.BossHpPct = BossHpPct;
	OutState.BossPhase = BossPhase;
	OutState.ArenaName = ArenaName;
	OutState.ArenaMods = ArenaMods;
	OutState.Status = Status;
	OutState.Toast = Toast;
	OutState.ToastTime = ToastTime;
	OutState.bShopOpen = bShopOpen;
	OutState.bLevelUp = bLevelUp;
	OutState.ShopOffers = ShopOffers;
	OutState.ShopHint = bShopOpen ? TEXT("1 / 2 / 3  -  WAHL    R  -  REROLL") : FString();

	OutState.PartyLines.Reset(Players.Num());
	const FWS2CoopPlayer* LocalPlayer = nullptr;
	for (const FWS2CoopPlayer& Player : Players)
	{
		if (Player.Slot == InSlot)
		{
			LocalPlayer = &Player;
		}
		OutState.PartyLines.Add(FString::Printf(TEXT("%s  %s  %.0f/%.0f%s"),
			*Player.HeroName,
			*Player.PlayerName,
			Player.Hp,
			Player.MaxHp,
			Player.bDown ? TEXT("  DOWN") : TEXT("")));
	}
	OutState.MetaText = BestWave > 0
		? FString::Printf(TEXT("REKORD  Welle %d  -  %d Kills  -  %d Runs"), BestWave, BestKills, TotalRuns)
		: FString::Printf(TEXT("NOCH KEIN REKORD  -  %d Runs"), TotalRuns);

	if (LocalPlayer)
	{
		OutState.Hp = LocalPlayer->Hp;
		OutState.MaxHp = LocalPlayer->MaxHp;
		OutState.HeroName = LocalPlayer->HeroName;
		OutState.WeaponName = LocalPlayer->WeaponName;
		OutState.Level = LocalPlayer->Level;
		OutState.Xp = LocalPlayer->Xp;
		OutState.XpNext = LocalPlayer->XpNext;
		OutState.bPlayerDown = LocalPlayer->bDown;
	}
	else if (Players.Num() > 0)
	{
		const FWS2CoopPlayer& First = Players[0];
		OutState.Hp = First.Hp;
		OutState.MaxHp = First.MaxHp;
		OutState.HeroName = First.HeroName;
		OutState.WeaponName = First.WeaponName;
		OutState.Level = First.Level;
		OutState.Xp = First.Xp;
		OutState.XpNext = First.XpNext;
		OutState.bPlayerDown = First.bDown;
	}

	if (OutState.HeroName.IsEmpty())
	{
		OutState.HeroName = TEXT("LEONIDAS");
	}
	if (OutState.WeaponName.IsEmpty())
	{
		OutState.WeaponName = TEXT("-");
	}
}
