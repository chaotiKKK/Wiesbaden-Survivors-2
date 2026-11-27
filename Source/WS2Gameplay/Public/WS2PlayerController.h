#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "WS2RunState.h"
#include "WS2PlayerController.generated.h"

class SWidget;
class AWS2ArenaCamera;
class AWS2PlayerPawn;

UCLASS()
class WS2GAMEPLAY_API AWS2PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AWS2PlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	void ShowHud(const TSharedPtr<FWS2RunState>& InState);
	void ShowMenu();
	void HideMenu();
	void TogglePause();
	void RequestFire();

	virtual void SetupInputComponent() override;

	void PickShop(int32 Index);
	void PickShopOne() { PickShop(0); }
	void PickShopTwo() { PickShop(1); }
	void PickShopThree() { PickShop(2); }
	void RerollShop();

	UFUNCTION(Exec)
	void WS2Perf(int32 Value);

	UFUNCTION(Exec)
	void WS2Spawn(int32 Count);

	UFUNCTION(Exec)
	void WS2Host(const FString& ArenaId);

	UFUNCTION(Exec)
	void WS2Join(const FString& Address);

	UFUNCTION(Exec)
	void WS2Start();

protected:
	UFUNCTION(Server, Reliable)
	void ServerStartRun();

	UFUNCTION(Server, Reliable)
	void ServerPickShop(int32 Index);

	UFUNCTION(Server, Reliable)
	void ServerRerollShop();

	UFUNCTION(Server, Reliable)
	void ServerSetHero(const FString& HeroId);

private:
	void RefreshHudState();
	void EnsureLocalView();
	void ShowClientLighting();
	void ShowPause();
	void HidePause();
	void StartSolo(const FString& HeroId, const FString& ArenaId);
	void HostLan(const FString& HeroId, const FString& ArenaId);
	void JoinLan(const FString& Address, const FString& HeroId);
	void UpdateNetVerify(float DeltaSeconds);
	void UpdatePauseVerify();
	void SendVerifyKey(const FKey& Key, bool bPressed);

	TSharedPtr<SWidget> MenuWidget;
	TSharedPtr<SWidget> HudWidget;
	TSharedPtr<SWidget> PauseWidget;
	TSharedPtr<FWS2RunState> HudState;

	TWeakObjectPtr<AWS2ArenaCamera> LocalCamera;
	FVector2D LastArenaExtent = FVector2D::ZeroVector;
	bool bHudCreated = false;
	bool bMenuVisible = false;
	bool bClientLightsSpawned = false;
	bool bLoggedRunState = false;
	bool bPausedByMe = false;
	bool bNetVerify = false;
	bool bNetVerifyStarted = false;
	float NetVerifyElapsed = 0.f;
	bool bNetVerifyKeyDown = false;
	bool bNetVerifyKeyUp = false;
	bool bNetVerifyFired = false;
	bool bNetVerifyQuit = false;
	bool bNetVerifyOtherMoved = false;
	bool bNetVerifyHostSeen = false;
	bool bNetVerifyLocalProbe = false;
	FVector NetVerifyOtherStart = FVector::ZeroVector;
	TWeakObjectPtr<AWS2PlayerPawn> NetVerifyOtherPawn;
	int32 NetVerifyLastWave = -1;
	int32 NetVerifyLastMaterials = -1;
	int32 NetVerifyLastPickups = -1;
	int32 NetVerifyLastAlive = -1;
	int32 NetVerifyLastProjectiles = -1;
	int32 NetVerifyLastPlayers = -1;
	int32 NetVerifyPeakPlayers = 0;
	float NetVerifyWindow = 10.5f;
	float NetVerifyStateTimer = 0.f;
	TArray<int32> NetVerifyLastLevels;
	bool bPauseVerify = false;
	int32 PauseVerifyStage = 0;
	double PauseVerifyWall = 0.0;
	FVector PauseVerifyLocation = FVector::ZeroVector;
	float PauseVerifyWorldTime = 0.f;
	float PauseVerifyWaveTime = 0.f;
	FString PendingHero;
};
