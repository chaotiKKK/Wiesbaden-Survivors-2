#include "WS2PlayerController.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "WS2GameMode.h"
#include "WS2Hud.h"
#include "WS2WaveDirector.h"

AWS2PlayerController::AWS2PlayerController()
{
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
}

void AWS2PlayerController::BeginPlay()
{
	Super::BeginPlay();

	SetInputMode(FInputModeGameOnly());

	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		if (GameMode->GetHudState().IsValid())
		{
			ShowHud(GameMode->GetHudState());
		}
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
	}
}

void AWS2PlayerController::PickShop(int32 Index)
{
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		if (GameMode->IsShopOpen())
		{
			GameMode->PickShop(Index);
		}
	}
}

void AWS2PlayerController::RerollShop()
{
	if (AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr)
	{
		if (GameMode->IsShopOpen())
		{
			GameMode->RerollShop();
		}
	}
}

void AWS2PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HudWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
	}
	HudWidget.Reset();
	Super::EndPlay(EndPlayReason);
}

void AWS2PlayerController::ShowHud(const TSharedPtr<FWS2RunState>& InState)
{
	if (!GEngine || !GEngine->GameViewport || !InState.IsValid())
	{
		return;
	}

	if (HudWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(HudWidget.ToSharedRef());
	}

	HudState = InState;
	if (AWS2GameMode* GameMode = Cast<AWS2GameMode>(GetWorld()->GetAuthGameMode()))
	{
		GameMode->SetFrameTag(HudWidget.IsValid() ? TEXT("hud-rebuild") : TEXT("first-hud"));
	}
	HudWidget = SNew(SWS2Hud).State(InState);
	GEngine->GameViewport->AddViewportWidgetContent(HudWidget.ToSharedRef(), 10);
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
