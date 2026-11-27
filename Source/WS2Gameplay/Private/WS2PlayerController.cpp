#include "WS2PlayerController.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetConnection.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "IPAddress.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "HAL/FileManager.h"
#include "SocketSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "WS2ArenaCamera.h"
#include "WS2GameMode.h"
#include "WS2GameState.h"
#include "WS2Hud.h"
#include "WS2PlayerPawn.h"
#include "WS2PlayerState.h"
#include "WS2PackageTrace.h"
#include "WS2SaveGame.h"
#include "WS2WaveDirector.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Net, Log, All);

namespace
{
	FString GPendingJoinHero;
	bool GTestLinkStarted = false;
	bool GTestPauseStarted = false;

	class FWS2PackageTrace : public FOutputDevice
	{
	public:
		explicit FWS2PackageTrace(const FString& InPath) : Path(InPath) {}

		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const FString Line(Message);
			if (Line.Contains(TEXT("NETVERIFY")) || Line.Contains(TEXT("LOBBY MENU")) ||
				Line.Contains(TEXT("procedural SFX")) || Line.Contains(TEXT("PLAYER JOIN")) ||
				Line.Contains(TEXT("Error")) || Line.Contains(TEXT("Fatal")))
			{
				FString Out = Line;
				Out.Append(LINE_TERMINATOR);
				FFileHelper::SaveStringToFile(Out, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
					&IFileManager::Get(), EFileWrite::FILEWRITE_Append);
			}
		}

	private:
		FString Path;
	};

	TUniquePtr<FWS2PackageTrace> GPackageTrace;

	void EnsurePackageTrace()
	{
		if (GPackageTrace.IsValid())
		{
			return;
		}
		FString TracePath;
		if (FParse::Value(FCommandLine::Get(), TEXT("ws2trace="), TracePath) && !TracePath.IsEmpty())
		{
			GPackageTrace = MakeUnique<FWS2PackageTrace>(TracePath);
			GLog->AddOutputDevice(GPackageTrace.Get());
		}
	}

	void TraceNet(const FString& Line)
	{
		WS2PackageTrace::Write(Line);
	}

	FString LocalLanAddress()
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSubsystem)
		{
			return TEXT("127.0.0.1");
		}

		// GetLocalHostAddr() reports the loopback adapter on some Windows setups, so
		// walk the real adapters and take the first routable IPv4 address instead.
		TArray<TSharedPtr<FInternetAddr>> Adapters;
		SocketSubsystem->GetLocalAdapterAddresses(Adapters);
		for (const TSharedPtr<FInternetAddr>& Adapter : Adapters)
		{
			if (!Adapter.IsValid())
			{
				continue;
			}
			FString Text = Adapter->ToString(false);
			int32 ScopeIndex = INDEX_NONE;
			if (Text.FindChar(TEXT('%'), ScopeIndex))
			{
				Text.LeftInline(ScopeIndex);
			}
			if (Text.IsEmpty() || !Text.Contains(TEXT(".")) || Text.Contains(TEXT(":"))
				|| Text.StartsWith(TEXT("127.")) || Text.StartsWith(TEXT("169.254.")))
			{
				continue;
			}
			return Text;
		}

		bool bCanBindAll = false;
		const TSharedRef<FInternetAddr> Address = SocketSubsystem->GetLocalHostAddr(*GLog, bCanBindAll);
		FString Fallback = Address->ToString(false);
		int32 ScopeIndex = INDEX_NONE;
		if (Fallback.FindChar(TEXT('%'), ScopeIndex))
		{
			Fallback.LeftInline(ScopeIndex);
		}
		return Fallback.IsEmpty() ? FString(TEXT("127.0.0.1")) : Fallback;
	}

	struct FWS2LobbyModel
	{
		TArray<FString> HeroIds;
		TArray<FString> HeroNames;
		TArray<FString> ArenaIds;
		TArray<FString> ArenaNames;
		int32 HeroIndex = 0;
		int32 ArenaIndex = 0;
		FString Address = TEXT("127.0.0.1:7777");
		FString Message;
	};

	TSharedRef<SWidget> MakeLobbyMenu(const TSharedPtr<FWS2LobbyModel>& Lobby,
		TFunction<void()> OnSolo, TFunction<void()> OnHost, TFunction<void(const FString&)> OnJoin)
	{
		const FLinearColor Accent(1.f, 0.82f, 0.32f, 1.f);
		const FLinearColor Muted(0.56f, 0.66f, 0.77f, 1.f);
		const FLinearColor Value(0.94f, 0.97f, 1.f, 1.f);
		FString ProjectVersion = TEXT("0.2.0");
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), ProjectVersion, GGameIni);

		auto CycleRow = [Value](const TCHAR* Label, const TArray<FString>& Labels, int32* Index)
		{
			const TArray<FString>* LabelsPtr = &Labels;
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 4.f, 16.f, 4.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Label))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
					.ColorAndOpacity(FLinearColor(0.56f, 0.66f, 0.77f, 1.f))
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.OnClicked_Lambda([Index, LabelsPtr]()
					{
						if (LabelsPtr->Num() > 0)
						{
							*Index = (*Index + 1) % LabelsPtr->Num();
						}
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Text_Lambda([LabelsPtr, Index]()
						{
							return FText::FromString(LabelsPtr->IsValidIndex(*Index) ? (*LabelsPtr)[*Index] : TEXT("-"));
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
						.ColorAndOpacity(Value)
					]
				];
		};

		return SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.008f, 0.015f, 0.03f, 0.97f))
				.Padding(FMargin(72.f, 48.f))
				[
					SNew(SBox)
					.WidthOverride(620.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("WIESBADEN SURVIVORS 2.0")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
							.ColorAndOpacity(Accent)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 26.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("VERSION %s  -  LAN / WLAN KOOP  -  2 BIS 4 SPIELER"), *ProjectVersion)))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
							.ColorAndOpacity(Muted)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							CycleRow(TEXT("HELD"), Lobby->HeroNames, &Lobby->HeroIndex)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							CycleRow(TEXT("ARENA"), Lobby->ArenaNames, &Lobby->ArenaIndex)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 22.f, 0.f, 6.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("SOLO ODER LAN")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(Muted)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 6.f, 0.f)
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.OnClicked_Lambda([OnSolo]() { OnSolo(); return FReply::Handled(); })
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("SOLO STARTEN")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
								]
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f, 0.f, 0.f, 0.f)
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.OnClicked_Lambda([OnHost]() { OnHost(); return FReply::Handled(); })
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("LAN HOSTEN")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 22.f, 0.f, 6.f)
						[
							SNew(STextBlock)
									.Text(FText::FromString(TEXT("IP BEITRETEN")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(Muted)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 8.f, 0.f)
							[
								SNew(SEditableTextBox)
								.Text_Lambda([Lobby]() { return FText::FromString(Lobby->Address); })
								.OnTextChanged_Lambda([Lobby](const FText& Text) { Lobby->Address = Text.ToString(); })
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.OnClicked_Lambda([Lobby, OnJoin]() { OnJoin(Lobby->Address); return FReply::Handled(); })
								[
									SNew(STextBlock)
									.Text(FText::FromString(TEXT("VERBINDEN")))
									.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
								]
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 22.f, 0.f, 0.f)
						[
							SNew(STextBlock)
							.Text_Lambda([Lobby]() { return FText::FromString(Lobby->Message); })
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
							.ColorAndOpacity(Muted)
							.AutoWrapText(true)
						]
					]
				]
			];
	}
}

AWS2PlayerController::AWS2PlayerController()
{
	bShouldPerformFullTickWhenPaused = true;
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void AWS2PlayerController::BeginPlay()
{
	Super::BeginPlay();
	EnsurePackageTrace();
	TraceNet(FString::Printf(TEXT("TRACE HEADER package=%s exe=%s"),
		*FPaths::ProjectDir(), FPlatformProcess::ExecutablePath()));
	TraceNet(TEXT("NETVERIFY TRACE START"));

	HudState = MakeShared<FWS2RunState>();
	SetInputMode(FInputModeGameOnly());
	RefreshHudState();
	if (!HasAuthority() && !GPendingJoinHero.IsEmpty())
	{
		PendingHero = GPendingJoinHero;
		GPendingJoinHero.Reset();
	}
	FString CommandLineHero;
	if (FParse::Value(FCommandLine::Get(), TEXT("ws2hero="), CommandLineHero) && !CommandLineHero.IsEmpty())
	{
		PendingHero = CommandLineHero;
	}
	bNetVerify = AWS2GameMode::IsNetVerifyRequested();
	if (FParse::Value(FCommandLine::Get(), TEXT("ws2netwindow="), NetVerifyWindow))
	{
		// A two-player proof needs more than the movement window: waves have to
		// advance and at least one level-up has to replicate. -ws2netwindow=<s>
		// keeps the deterministic netverify sequence but stretches its end.
		NetVerifyWindow = FMath::Clamp(NetVerifyWindow, 5.f, 600.f);
	}
	TraceNet(FString::Printf(TEXT("TRACE PC pid=%d auth=%d netmode=%d netverify=%d window=%.1f pawn=%s"),
		FPlatformProcess::GetCurrentProcessId(), HasAuthority() ? 1 : 0, GetNetMode(), bNetVerify ? 1 : 0,
		NetVerifyWindow, GetPawn() ? *GetPawn()->GetName() : TEXT("none")));
	int32 PauseVerifyRequest = 0;
	bPauseVerify = FParse::Value(FCommandLine::Get(), TEXT("ws2pausetest="), PauseVerifyRequest) && PauseVerifyRequest > 0;

	if (const AWS2GameState* NetState = GetWorld() ? GetWorld()->GetGameState<AWS2GameState>() : nullptr)
	{
		if (NetState->bRunStarted)
		{
			ShowHud(HudState);
			return;
		}
	}
	ShowMenu();

	if (bNetVerify && !GTestLinkStarted)
	{
		int32 TestHost = 0;
		FString TestJoin;
		if (HasAuthority() && FParse::Value(FCommandLine::Get(), TEXT("ws2testhost="), TestHost) && TestHost > 0)
		{
			GTestLinkStarted = true;
			HostLan(TEXT("manni"), TEXT("kurpark"));
		}
		else if (FParse::Value(FCommandLine::Get(), TEXT("ws2testjoin="), TestJoin) && !TestJoin.IsEmpty())
		{
			GTestLinkStarted = true;
			JoinLan(TestJoin, TEXT("manni"));
		}
	}
	if (bPauseVerify && !GTestPauseStarted)
	{
		GTestPauseStarted = true;
		StartSolo(TEXT("leonidas"), TEXT("kurpark"));
	}
}

void AWS2PlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::One, IE_Pressed, this, &AWS2PlayerController::PickShopOne);
		InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AWS2PlayerController::PickShopTwo);
		InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AWS2PlayerController::PickShopThree);
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &AWS2PlayerController::RerollShop);
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &AWS2PlayerController::TogglePause);
		InputComponent->BindKey(EKeys::Gamepad_Special_Right, IE_Pressed, this, &AWS2PlayerController::TogglePause);
		InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AWS2PlayerController::RequestFire);
	}
}

void AWS2PlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	EnsureLocalView();
	RefreshHudState();

	const AWS2GameState* NetState = GetWorld() ? GetWorld()->GetGameState<AWS2GameState>() : nullptr;
	if (NetState && NetState->bRunStarted && bMenuVisible)
	{
		HideMenu();
	}
	if (NetState && NetState->bRunStarted && !bLoggedRunState)
	{
		bLoggedRunState = true;
		UE_LOG(LogWS2Net, Display, TEXT("CLIENT RUN state: wave=%d players=%d enemies=%d boss=%s"),
			NetState->Wave, NetState->NumPlayers(), NetState->AliveEnemies, *NetState->BossName);
	}
	if (!HasAuthority() && !PendingHero.IsEmpty() && GetPawn())
	{
		ServerSetHero(PendingHero);
		PendingHero.Reset();
	}
	if (!bMenuVisible && !bHudCreated)
	{
		ShowHud(HudState);
	}
	UpdateNetVerify(DeltaSeconds);
	UpdatePauseVerify();
}

void AWS2PlayerController::RefreshHudState()
{
	if (!HudState.IsValid())
	{
		HudState = MakeShared<FWS2RunState>();
	}

	if (const AWS2GameState* NetState = GetWorld() ? GetWorld()->GetGameState<AWS2GameState>() : nullptr)
	{
		const AWS2PlayerState* PS = GetPlayerState<AWS2PlayerState>();
		NetState->BuildHudState(PS ? PS->Slot : 0, *HudState);
		return;
	}

	if (const AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		*HudState = GameMode->GetState();
	}
}

void AWS2PlayerController::ShowHud(const TSharedPtr<FWS2RunState>& InState)
{
	if (InState.IsValid())
	{
		HudState = InState;
	}
	if (!HudState.IsValid() || bHudCreated || bMenuVisible || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	HudWidget = SNew(SWS2Hud).State(HudState);
	GEngine->GameViewport->AddViewportWidgetContent(HudWidget.ToSharedRef(), 10);
	bHudCreated = true;

	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		GameMode->SetFrameTag(TEXT("hud-rebuild"));
	}
}

void AWS2PlayerController::ShowMenu()
{
	if (bMenuVisible || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	TSharedPtr<FWS2LobbyModel> Lobby = MakeShared<FWS2LobbyModel>();
	WS2::FDatabase& Db = WS2::FDatabase::Get();
	Db.EnsureLoaded();
	for (const WS2::FHeroDef& Hero : Db.Heroes)
	{
		Lobby->HeroIds.Add(Hero.Id);
		Lobby->HeroNames.Add(FString::Printf(TEXT("%s  -  %s"), *Hero.Name, *Hero.Role));
	}
	for (const WS2::FArenaDef& Arena : Db.Arenas)
	{
		Lobby->ArenaIds.Add(Arena.Id);
		Lobby->ArenaNames.Add(Arena.Name);
	}
	for (int32 Index = 0; Index < Lobby->HeroIds.Num(); ++Index)
	{
		if (Lobby->HeroIds[Index] == TEXT("leonidas"))
		{
			Lobby->HeroIndex = Index;
			break;
		}
	}
	for (int32 Index = 0; Index < Lobby->ArenaIds.Num(); ++Index)
	{
		if (Lobby->ArenaIds[Index] == TEXT("kurpark"))
		{
			Lobby->ArenaIndex = Index;
			break;
		}
	}
	FString Record = TEXT("Noch kein Rekord");
	if (UWS2SaveGame* Save = Cast<UWS2SaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("WS2Profile"), 0)))
	{
		Record = FString::Printf(TEXT("Rekord: Welle %d / %d Kills / %d Runs"),
			Save->BestWave, Save->BestKills, Save->Runs);
	}
	Lobby->Message = FString::Printf(
		TEXT("Host: LAN HOSTEN  -  Freunde verbinden sich mit %s:7777 (oder IP im Feld eintragen).\n%s\nSolo startet ohne Netzwerk; Joiner erhalten automatisch einen freien Helden-Slot."),
		*LocalLanAddress(), *Record);
	UE_LOG(LogWS2Net, Display, TEXT("LAN lobby address: %s:7777"), *LocalLanAddress());

	MenuWidget = MakeLobbyMenu(
		Lobby,
		[this, Lobby]()
		{
			const FString Hero = Lobby->HeroIds.IsValidIndex(Lobby->HeroIndex) ? Lobby->HeroIds[Lobby->HeroIndex] : TEXT("leonidas");
			const FString Arena = Lobby->ArenaIds.IsValidIndex(Lobby->ArenaIndex) ? Lobby->ArenaIds[Lobby->ArenaIndex] : TEXT("kurpark");
			StartSolo(Hero, Arena);
		},
		[this, Lobby]()
		{
			const FString Hero = Lobby->HeroIds.IsValidIndex(Lobby->HeroIndex) ? Lobby->HeroIds[Lobby->HeroIndex] : TEXT("leonidas");
			const FString Arena = Lobby->ArenaIds.IsValidIndex(Lobby->ArenaIndex) ? Lobby->ArenaIds[Lobby->ArenaIndex] : TEXT("kurpark");
			HostLan(Hero, Arena);
		},
		[this, Lobby](const FString& Address)
		{
			const FString Hero = Lobby->HeroIds.IsValidIndex(Lobby->HeroIndex) ? Lobby->HeroIds[Lobby->HeroIndex] : TEXT("leonidas");
			JoinLan(Address, Hero);
		});

	GEngine->GameViewport->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 20);
	bMenuVisible = true;
	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());
	UE_LOG(LogWS2Net, Display, TEXT("LOBBY MENU shown buttons=SOLO,LAN_HOST,IP_JOIN heroes=%d arenas=%d address=%s"),
		Lobby->HeroIds.Num(), Lobby->ArenaIds.Num(), *Lobby->Address);
	TraceNet(FString::Printf(TEXT("LOBBY MENU shown buttons=SOLO,LAN_HOST,IP_JOIN version=0.2.0 heroes=%d arenas=%d address=%s"),
		Lobby->HeroIds.Num(), Lobby->ArenaIds.Num(), *Lobby->Address));

	if (bHudCreated && HudWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
		HudWidget.Reset();
		bHudCreated = false;
	}
}

void AWS2PlayerController::HideMenu()
{
	if (!bMenuVisible)
	{
		return;
	}
	if (MenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	}
	MenuWidget.Reset();
	bMenuVisible = false;
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
	ShowHud(HudState);
}

void AWS2PlayerController::TogglePause()
{
	if (bMenuVisible)
	{
		return;
	}
	if (bPausedByMe)
	{
		HidePause();
		return;
	}
	// A client cannot pause the authoritative run, and a LAN host must not stop
	// its friends' simulation. Pause is therefore a solo convenience only.
	if (GetNetMode() != NM_Standalone)
	{
		return;
	}
	if (!UGameplayStatics::IsGamePaused(this))
	{
		SetPause(true);
		bPausedByMe = true;
		ShowPause();
	}
}

void AWS2PlayerController::ShowPause()
{
	if (PauseWidget.IsValid() || !GEngine || !GEngine->GameViewport)
	{
		return;
	}

	PauseWidget = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(360.f)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.01f, 0.02f, 0.035f, 0.92f))
				.Padding(FMargin(24.f, 18.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("PAUSE")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
						.ColorAndOpacity(FLinearColor(1.f, 0.82f, 0.32f, 1.f))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 6.f, 0.f, 14.f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("SOLO  -  ESC ODER BUTTON ZUM WEITERSPIELEN")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(FLinearColor(0.56f, 0.66f, 0.77f, 1.f))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(SButton)
						.OnClicked_Lambda([this]() { HidePause(); return FReply::Handled(); })
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("WEITER")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						]
					]
				]
			]
		];
	GEngine->GameViewport->AddViewportWidgetContent(PauseWidget.ToSharedRef(), 40);
	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
}

void AWS2PlayerController::HidePause()
{
	if (PauseWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(PauseWidget.ToSharedRef());
	}
	PauseWidget.Reset();
	if (bPausedByMe)
	{
		bPausedByMe = false;
		SetPause(false);
	}
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void AWS2PlayerController::RequestFire()
{
	if (AWS2PlayerPawn* LocalPawn = Cast<AWS2PlayerPawn>(GetPawn()))
	{
		LocalPawn->FireInput();
	}
}

void AWS2PlayerController::SendVerifyKey(const FKey& Key, bool bPressed)
{
	UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	FViewport* Viewport = ViewportClient ? ViewportClient->GetGameViewport() : nullptr;
	if (!Viewport)
	{
		UE_LOG(LogWS2Net, Warning, TEXT("NETVERIFY input failed: no viewport for key=%s"), *Key.ToString());
		return;
	}
	const FInputKeyEventArgs Args(Viewport, INPUTDEVICEID_NONE, Key,
		bPressed ? IE_Pressed : IE_Released, 1.f, false, FPlatformTime::Cycles64());
	InputKey(Args);
}

void AWS2PlayerController::UpdateNetVerify(float DeltaSeconds)
{
	if (!bNetVerify)
	{
		return;
	}
	if (!IsLocalController())
	{
		return;
	}
	const AWS2GameState* NetState = GetWorld() ? GetWorld()->GetGameState<AWS2GameState>() : nullptr;
	if (!NetState || !NetState->bRunStarted || bMenuVisible)
	{
		return;
	}
	if (!bNetVerifyStarted && NetState->NumPlayers() < 2)
	{
		// Wait for the joiner, but once the shared run is being verified keep
		// ticking after a client leaves so the host still reports its window.
		return;
	}
	NetVerifyPeakPlayers = FMath::Max(NetVerifyPeakPlayers, NetState->NumPlayers());
	if (!bNetVerifyStarted)
	{
		bNetVerifyStarted = true;
		NetVerifyElapsed = 0.f;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s BEGIN players=%d wave=%d"),
			HasAuthority() ? TEXT("HOST") : TEXT("CLIENT"), NetState->NumPlayers(), NetState->Wave);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s BEGIN players=%d wave=%d"),
			HasAuthority() ? TEXT("HOST") : TEXT("CLIENT"), NetState->NumPlayers(), NetState->Wave));
		if (UNetConnection* Link = GetNetConnection())
		{
			TraceNet(FString::Printf(TEXT("NETVERIFY %s LINK remote=%s"),
				HasAuthority() ? TEXT("HOST") : TEXT("CLIENT"), *Link->LowLevelGetRemoteAddress(true)));
		}
		if (const APawn* LocalPawn = GetPawn())
		{
			UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s LOCAL pawn=%s pos=%s"),
				HasAuthority() ? TEXT("HOST") : TEXT("CLIENT"), *LocalPawn->GetName(), *LocalPawn->GetActorLocation().ToCompactString());
		}
	}

	NetVerifyElapsed += DeltaSeconds;
	const TCHAR* VerifyRole = HasAuthority() ? TEXT("HOST") : TEXT("CLIENT");
	if (!bNetVerifyKeyDown && NetVerifyElapsed >= 2.f)
	{
		bNetVerifyKeyDown = true;
		const FKey MoveKey = HasAuthority() ? EKeys::A : EKeys::D;
		SendVerifyKey(MoveKey, true);
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s KEY %s pressed move=1"), VerifyRole, *MoveKey.ToString());
		TraceNet(FString::Printf(TEXT("NETVERIFY %s KEY %s pressed move=1"), VerifyRole, *MoveKey.ToString()));
	}
	if (bNetVerifyKeyDown && !bNetVerifyKeyUp && NetVerifyElapsed >= 4.f)
	{
		bNetVerifyKeyUp = true;
		const FKey MoveKey = HasAuthority() ? EKeys::A : EKeys::D;
		SendVerifyKey(MoveKey, false);
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s KEY %s released"), VerifyRole, *MoveKey.ToString());
		TraceNet(FString::Printf(TEXT("NETVERIFY %s KEY %s released"), VerifyRole, *MoveKey.ToString()));
	}
	if (!bNetVerifyFired && NetVerifyElapsed >= 5.f)
	{
		bNetVerifyFired = true;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s FIRE KEY pressed"), VerifyRole);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s FIRE KEY pressed"), VerifyRole));
		SendVerifyKey(EKeys::SpaceBar, true);
		SendVerifyKey(EKeys::SpaceBar, false);
	}

	if (NetVerifyLastWave != NetState->Wave)
	{
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s WAVE %d -> %d"), VerifyRole, NetVerifyLastWave, NetState->Wave);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s WAVE %d -> %d"), VerifyRole, NetVerifyLastWave, NetState->Wave));
		NetVerifyLastWave = NetState->Wave;
	}
	if (NetVerifyLastPlayers != NetState->NumPlayers())
	{
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s PLAYERS %d -> %d"),
			VerifyRole, NetVerifyLastPlayers, NetState->NumPlayers());
		TraceNet(FString::Printf(TEXT("NETVERIFY %s PLAYERS %d -> %d"),
			VerifyRole, NetVerifyLastPlayers, NetState->NumPlayers()));
		NetVerifyLastPlayers = NetState->NumPlayers();
	}
	const int32 LevelRows = NetState->Players.Num();
	if (NetVerifyLastLevels.Num() != LevelRows)
	{
		// First snapshot only: a fresh row already has its starting level, so
		// only later rises are reported as replicated level-ups.
		NetVerifyLastLevels.SetNum(LevelRows);
		for (int32 Index = 0; Index < LevelRows; ++Index)
		{
			NetVerifyLastLevels[Index] = NetState->Players[Index].Level;
		}
	}
	for (int32 Index = 0; Index < LevelRows; ++Index)
	{
		const FWS2CoopPlayer& Row = NetState->Players[Index];
		if (Row.Level != NetVerifyLastLevels[Index])
		{
			UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s LEVEL slot=%d %d -> %d xp=%d/%d"),
				VerifyRole, Row.Slot, NetVerifyLastLevels[Index], Row.Level, Row.Xp, Row.XpNext);
			TraceNet(FString::Printf(TEXT("NETVERIFY %s LEVEL slot=%d %d -> %d xp=%d/%d"),
				VerifyRole, Row.Slot, NetVerifyLastLevels[Index], Row.Level, Row.Xp, Row.XpNext));
			NetVerifyLastLevels[Index] = Row.Level;
		}
	}
	NetVerifyStateTimer += DeltaSeconds;
	if (NetVerifyStateTimer >= 2.f)
	{
		NetVerifyStateTimer = 0.f;
		FString Levels;
		for (const FWS2CoopPlayer& Row : NetState->Players)
		{
			Levels += FString::Printf(TEXT("%s%d"), Levels.IsEmpty() ? TEXT("") : TEXT(","), Row.Level);
		}
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s STATE players=%d wave=%d levels=%s kills=%d materials=%d enemies=%d"),
			VerifyRole, NetState->NumPlayers(), NetState->Wave, *Levels, NetState->Kills, NetState->Materials, NetState->AliveEnemies);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s STATE players=%d wave=%d levels=%s kills=%d materials=%d enemies=%d"),
			VerifyRole, NetState->NumPlayers(), NetState->Wave, *Levels, NetState->Kills, NetState->Materials, NetState->AliveEnemies));
	}
	if (NetVerifyLastMaterials != NetState->Materials)
	{
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s MATERIALS %d -> %d"), VerifyRole, NetVerifyLastMaterials, NetState->Materials);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s MATERIALS %d -> %d"), VerifyRole, NetVerifyLastMaterials, NetState->Materials));
		NetVerifyLastMaterials = NetState->Materials;
	}
	if (NetVerifyLastPickups != NetState->PickupsOnFloor)
	{
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s PICKUPS FLOOR %d -> %d"), VerifyRole, NetVerifyLastPickups, NetState->PickupsOnFloor);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s PICKUPS FLOOR %d -> %d"), VerifyRole, NetVerifyLastPickups, NetState->PickupsOnFloor));
		NetVerifyLastPickups = NetState->PickupsOnFloor;
	}
	if (NetVerifyLastAlive != NetState->AliveEnemies)
	{
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s ENEMIES %d -> %d"), VerifyRole, NetVerifyLastAlive, NetState->AliveEnemies);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s ENEMIES %d -> %d"), VerifyRole, NetVerifyLastAlive, NetState->AliveEnemies));
		NetVerifyLastAlive = NetState->AliveEnemies;
	}
	if (NetVerifyLastProjectiles != NetState->Projectiles)
	{
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s PROJECTILES %d -> %d"), VerifyRole, NetVerifyLastProjectiles, NetState->Projectiles);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s PROJECTILES %d -> %d"), VerifyRole, NetVerifyLastProjectiles, NetState->Projectiles));
		NetVerifyLastProjectiles = NetState->Projectiles;
	}
	if (!HasAuthority() && !bNetVerifyOtherMoved && GetWorld())
	{
		AWS2PlayerPawn* LocalPawn = Cast<AWS2PlayerPawn>(GetPawn());
		if (!LocalPawn)
		{
			return;
		}
		for (TActorIterator<AWS2PlayerPawn> It(GetWorld()); It; ++It)
		{
			AWS2PlayerPawn* Other = *It;
			if (!Other || Other == LocalPawn || Other->IsLocallyControlled())
			{
				continue;
			}
			if (!NetVerifyOtherPawn.IsValid())
			{
				NetVerifyOtherPawn = Other;
				NetVerifyOtherStart = Other->GetActorLocation();
				if (!bNetVerifyHostSeen)
				{
					bNetVerifyHostSeen = true;
					UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY CLIENT HOST VISIBLE pawn=%s pos=%s"),
						*Other->GetName(), *Other->GetActorLocation().ToCompactString());
					TraceNet(FString::Printf(TEXT("NETVERIFY CLIENT HOST VISIBLE pawn=%s pos=%s"),
						*Other->GetName(), *Other->GetActorLocation().ToCompactString()));
				}
				continue;
			}
			if (Other == NetVerifyOtherPawn.Get() &&
				FVector::Dist2D(Other->GetActorLocation(), NetVerifyOtherStart) > 80.f)
			{
				bNetVerifyOtherMoved = true;
				UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY CLIENT HOST MOVED pawn=%s distance=%.0f pos=%s"),
					*Other->GetName(), FVector::Dist2D(Other->GetActorLocation(), NetVerifyOtherStart),
					*Other->GetActorLocation().ToCompactString());
				TraceNet(FString::Printf(TEXT("NETVERIFY CLIENT HOST MOVED pawn=%s distance=%.0f pos=%s"),
					*Other->GetName(), FVector::Dist2D(Other->GetActorLocation(), NetVerifyOtherStart),
					*Other->GetActorLocation().ToCompactString()));
			}
		}
	}
	if (!HasAuthority() && !bNetVerifyLocalProbe && NetVerifyElapsed >= 6.f)
	{
		bNetVerifyLocalProbe = true;
		if (const AWS2PlayerPawn* Local = Cast<AWS2PlayerPawn>(GetPawn()))
		{
			UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY CLIENT LOCAL PROBE pawn=%s local=%d pos=%s start=%s"),
				*Local->GetName(), Local->IsLocallyControlled() ? 1 : 0,
				*Local->GetActorLocation().ToCompactString(), *Local->NetVerifyStartLocation.ToCompactString());
			TraceNet(FString::Printf(TEXT("NETVERIFY CLIENT LOCAL PROBE pawn=%s local=%d pos=%s start=%s"),
				*Local->GetName(), Local->IsLocallyControlled() ? 1 : 0,
				*Local->GetActorLocation().ToCompactString(), *Local->NetVerifyStartLocation.ToCompactString()));
		}
	}

	if (!bNetVerifyQuit && NetVerifyElapsed >= NetVerifyWindow)
	{
		bNetVerifyQuit = true;
		int32 MaxLevel = 1;
		for (const FWS2CoopPlayer& Row : NetState->Players)
		{
			MaxLevel = FMath::Max(MaxLevel, Row.Level);
		}
		const int32 PeakPlayers = FMath::Max(NetVerifyPeakPlayers, NetState->NumPlayers());
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY %s COMPLETE players=%d playersNow=%d wave=%d maxLevel=%d materials=%d pickups=%d enemies=%d"),
			VerifyRole, PeakPlayers, NetState->NumPlayers(), NetState->Wave, MaxLevel, NetState->Materials, NetState->PickupsOnFloor, NetState->AliveEnemies);
		TraceNet(FString::Printf(TEXT("NETVERIFY %s COMPLETE players=%d playersNow=%d wave=%d maxLevel=%d materials=%d pickups=%d enemies=%d"),
			VerifyRole, PeakPlayers, NetState->NumPlayers(), NetState->Wave, MaxLevel, NetState->Materials, NetState->PickupsOnFloor, NetState->AliveEnemies));
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}

void AWS2PlayerController::UpdatePauseVerify()
{
	if (!bPauseVerify)
	{
		return;
	}
	const AWS2GameState* NetState = GetWorld() ? GetWorld()->GetGameState<AWS2GameState>() : nullptr;
	const double Now = FPlatformTime::Seconds();
	if (PauseVerifyStage == 0)
	{
		if (!NetState || !NetState->bRunStarted || bMenuVisible || !GetPawn())
		{
			return;
		}
		if (PauseVerifyWall <= 0.0)
		{
			PauseVerifyWall = Now;
			return;
		}
		if (Now - PauseVerifyWall < 2.0)
		{
			return;
		}
		PauseVerifyLocation = GetPawn()->GetActorLocation();
		PauseVerifyWorldTime = GetWorld()->GetTimeSeconds();
		PauseVerifyWaveTime = NetState->WaveTimeLeft;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY PAUSE before world=%.3f waveTime=%.3f loc=%s"),
			PauseVerifyWorldTime, PauseVerifyWaveTime, *PauseVerifyLocation.ToCompactString());
		TraceNet(FString::Printf(TEXT("NETVERIFY PAUSE before world=%.3f waveTime=%.3f loc=%s"),
			PauseVerifyWorldTime, PauseVerifyWaveTime, *PauseVerifyLocation.ToCompactString()));
		TogglePause();
		PauseVerifyStage = 1;
		PauseVerifyWall = Now;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY PAUSE set paused=%d"), UGameplayStatics::IsGamePaused(this) ? 1 : 0);
		TraceNet(FString::Printf(TEXT("NETVERIFY PAUSE set paused=%d"), UGameplayStatics::IsGamePaused(this) ? 1 : 0));
		return;
	}

	if (PauseVerifyStage == 1 && Now - PauseVerifyWall >= 1.5)
	{
		const FVector CurrentLocation = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
		const float CurrentWorld = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const float LocationDelta = FVector::Dist(CurrentLocation, PauseVerifyLocation);
		const float WorldDelta = CurrentWorld - PauseVerifyWorldTime;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY PAUSE HOLD paused=%d locDelta=%.3f worldDelta=%.3f"),
			UGameplayStatics::IsGamePaused(this) ? 1 : 0, LocationDelta, WorldDelta);
		TraceNet(FString::Printf(TEXT("NETVERIFY PAUSE HOLD paused=%d locDelta=%.3f worldDelta=%.3f"),
			UGameplayStatics::IsGamePaused(this) ? 1 : 0, LocationDelta, WorldDelta));
		const bool bHeld = UGameplayStatics::IsGamePaused(this) && LocationDelta < 0.01f && WorldDelta < 0.01f;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY PAUSE RESULT hold=%s"), bHeld ? TEXT("PASS") : TEXT("FAIL"));
		TraceNet(FString::Printf(TEXT("NETVERIFY PAUSE RESULT hold=%s"), bHeld ? TEXT("PASS") : TEXT("FAIL")));
		HidePause();
		PauseVerifyStage = 2;
		PauseVerifyWall = Now;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY PAUSE resume paused=%d"), UGameplayStatics::IsGamePaused(this) ? 1 : 0);
		TraceNet(FString::Printf(TEXT("NETVERIFY PAUSE resume paused=%d"), UGameplayStatics::IsGamePaused(this) ? 1 : 0));
		return;
	}

	if (PauseVerifyStage == 2 && Now - PauseVerifyWall >= 0.75)
	{
		const float CurrentWorld = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		const float WorldDelta = CurrentWorld - PauseVerifyWorldTime;
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY PAUSE AFTER RESUME worldDelta=%.3f paused=%d"),
			WorldDelta, UGameplayStatics::IsGamePaused(this) ? 1 : 0);
		TraceNet(FString::Printf(TEXT("NETVERIFY PAUSE AFTER RESUME worldDelta=%.3f paused=%d"),
			WorldDelta, UGameplayStatics::IsGamePaused(this) ? 1 : 0));
		UE_LOG(LogWS2Net, Display, TEXT("NETVERIFY PAUSE RESULT resume=%s"), WorldDelta > 0.1f ? TEXT("PASS") : TEXT("FAIL"));
		TraceNet(FString::Printf(TEXT("NETVERIFY PAUSE RESULT resume=%s"), WorldDelta > 0.1f ? TEXT("PASS") : TEXT("FAIL")));
		PauseVerifyStage = 3;
		UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
	}
}

void AWS2PlayerController::StartSolo(const FString& HeroId, const FString& ArenaId)
{
	HideMenu();
	if (!HasAuthority())
	{
		ServerStartRun();
		return;
	}

	// Reload the arena with the lobby's choice. This also keeps solo and LAN
	// hosting on the same initialization path, so hero and arena cannot diverge.
	const FString Options = FString::Printf(TEXT("autostart=1?arena=%s?hero=%s"), *ArenaId, *HeroId);
	UGameplayStatics::OpenLevel(this, FName(TEXT("L_Arena")), true, Options);
}

void AWS2PlayerController::WS2Start()
{
	if (HasAuthority())
	{
		StartSolo(TEXT("leonidas"), TEXT("kurpark"));
	}
	else
	{
		ServerStartRun();
	}
}

void AWS2PlayerController::HostLan(const FString& HeroId, const FString& ArenaId)
{
	if (!HasAuthority())
	{
		return;
	}
	if (MenuWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	}
	MenuWidget.Reset();
	bMenuVisible = false;

	FString BindAddress;
	if (!FParse::Value(FCommandLine::Get(), TEXT("multihome="), BindAddress) || BindAddress.IsEmpty())
	{
		BindAddress = TEXT("0.0.0.0");
	}
	UE_LOG(LogWS2Net, Display, TEXT("LAN host bind=%s:7777 local=%s:7777"), *BindAddress, *LocalLanAddress());
	WS2PackageTrace::Write(FString::Printf(TEXT("LAN HOST bind=%s:7777 local=%s:7777 arena=%s"),
		*BindAddress, *LocalLanAddress(), *ArenaId));

	const FString Options = FString::Printf(TEXT("listen?autostart=1?arena=%s?hero=%s"), *ArenaId, *HeroId);
	UGameplayStatics::OpenLevel(this, FName(TEXT("L_Arena")), true, Options);
}

void AWS2PlayerController::JoinLan(const FString& Address, const FString& HeroId)
{
	FString Target = Address.TrimStartAndEnd();
	if (Target.IsEmpty())
	{
		return;
	}
	if (!Target.Contains(TEXT(":")))
	{
		Target += TEXT(":7777");
	}
	UE_LOG(LogWS2Net, Display, TEXT("LAN join target=%s hero=%s"), *Target, *HeroId);
	WS2PackageTrace::Write(FString::Printf(TEXT("LAN JOIN target=%s hero=%s"), *Target, *HeroId));
	GPendingJoinHero = HeroId;
	HideMenu();
	ClientTravel(Target, TRAVEL_Absolute);
}

void AWS2PlayerController::ServerStartRun_Implementation()
{
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		GameMode->StartRun();
	}
}

void AWS2PlayerController::PickShop(int32 Index)
{
	if (HasAuthority())
	{
		if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
		{
			if (GameMode->IsShopOpen())
			{
				GameMode->PickShop(Index, Cast<AWS2PlayerPawn>(GetPawn()));
			}
		}
		return;
	}
	ServerPickShop(Index);
}

void AWS2PlayerController::ServerPickShop_Implementation(int32 Index)
{
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		if (GameMode->IsShopOpen())
		{
			GameMode->PickShop(Index, Cast<AWS2PlayerPawn>(GetPawn()));
		}
	}
}

void AWS2PlayerController::RerollShop()
{
	if (HasAuthority())
	{
		if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
		{
			if (GameMode->IsShopOpen())
			{
				GameMode->RerollShop();
			}
		}
		return;
	}
	ServerRerollShop();
}

void AWS2PlayerController::ServerRerollShop_Implementation()
{
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		if (GameMode->IsShopOpen())
		{
			GameMode->RerollShop();
		}
	}
}

void AWS2PlayerController::ServerSetHero_Implementation(const FString& HeroId)
{
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		GameMode->SetPlayerHero(Cast<AWS2PlayerPawn>(GetPawn()), HeroId);
	}
}

void AWS2PlayerController::EnsureLocalView()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (HasAuthority())
	{
		if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
		{
			if (AWS2ArenaCamera* Camera = GameMode->GetArenaCamera())
			{
				Camera->SetTargetPlayer(GameMode->GetPlayerPawn());
				if (GetPawn() == GameMode->GetPlayerPawn())
				{
					SetViewTarget(Camera);
				}
			}
		}
		return;
	}

	if (!LocalCamera.IsValid())
	{
		if (AWS2ArenaCamera* Spawned = GetWorld()->SpawnActor<AWS2ArenaCamera>(AWS2ArenaCamera::StaticClass(),
			FVector(0.f, 0.f, 3200.f), FRotator(-89.5f, 0.f, 0.f)))
		{
			LocalCamera = Spawned;
			SetViewTarget(Spawned);
		}
	}
	if (AWS2ArenaCamera* Camera = LocalCamera.Get())
	{
		Camera->SetTargetPlayer(Cast<AWS2PlayerPawn>(GetPawn()));
		const AWS2GameState* NetState = GetWorld()->GetGameState<AWS2GameState>();
		if (NetState && NetState->ArenaExtent != LastArenaExtent)
		{
			LastArenaExtent = NetState->ArenaExtent;
			Camera->SetArenaExtent(LastArenaExtent);
		}
	}
	ShowClientLighting();
}

void AWS2PlayerController::ShowClientLighting()
{
	if (bClientLightsSpawned || HasAuthority())
	{
		return;
	}
	bClientLightsSpawned = true;

	if (ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(),
		FVector(0.f, 0.f, 3000.f), FRotator(-52.f, 38.f, 0.f)))
	{
		Sun->SetMobility(EComponentMobility::Movable);
		if (UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			LightComponent->SetIntensity(8.f);
			LightComponent->SetLightColor(FLinearColor(1.f, 0.94f, 0.86f));
		}
	}
	if (ASkyLight* Sky = GetWorld()->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator))
	{
		if (USkyLightComponent* SkyComponent = Sky->GetLightComponent())
		{
			SkyComponent->SetIntensity(2.f);
			SkyComponent->SetLightColor(FLinearColor(0.35f, 0.45f, 0.62f));
			SkyComponent->bLowerHemisphereIsBlack = false;
			SkyComponent->RecaptureSky();
		}
	}
}

void AWS2PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GEngine && GEngine->GameViewport)
	{
		if (HudWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
		}
		if (MenuWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
		}
		if (PauseWidget.IsValid())
		{
			GEngine->GameViewport->RemoveViewportWidgetContent(PauseWidget.ToSharedRef());
		}
	}
	HudWidget.Reset();
	MenuWidget.Reset();
	PauseWidget.Reset();
	if (LocalCamera.IsValid())
	{
		LocalCamera->Destroy();
	}
	Super::EndPlay(EndPlayReason);
}

void AWS2PlayerController::WS2Perf(int32 Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("ws2.PerfMode")))
	{
		Variable->Set(Value, ECVF_SetByConsole);
	}
}

void AWS2PlayerController::WS2Spawn(int32 Count)
{
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		if (UWS2WaveDirector* Director = GameMode->GetDirector())
		{
			Director->SpawnPerfHorde(FMath::Clamp(Count, 1, 1200));
		}
	}
}

void AWS2PlayerController::WS2Host(const FString& ArenaId)
{
	HostLan(TEXT("leonidas"), ArenaId.IsEmpty() ? TEXT("kurpark") : ArenaId);
}

void AWS2PlayerController::WS2Join(const FString& Address)
{
	JoinLan(Address, TEXT("leonidas"));
}
