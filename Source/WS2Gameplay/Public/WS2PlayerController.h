#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "WS2RunState.h"
#include "WS2PlayerController.generated.h"

class SWidget;

UCLASS()
class WS2GAMEPLAY_API AWS2PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AWS2PlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void ShowHud(const TSharedPtr<FWS2RunState>& InState);

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

private:
	TSharedPtr<SWidget> HudWidget;
	TSharedPtr<FWS2RunState> HudState;
};
