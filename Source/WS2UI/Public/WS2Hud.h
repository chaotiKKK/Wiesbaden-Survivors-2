#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WS2RunState.h"

/** Minimal slice HUD: HP, wave clock, materials, kills, enemy count, FPS. */
class WS2UI_API SWS2Hud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWS2Hud) {}
		SLATE_ARGUMENT(TSharedPtr<FWS2RunState>, State)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FText WaveText() const;
	FText TimerText() const;
	FText HpText() const;
	FText MaterialsText() const;
	FText KillsText() const;
	FText EnemyText() const;
	FText FpsText() const;
	FText ArenaText() const;
	FText HeroText() const;
	FText WeaponText() const;
	FText StatusText() const;
	FText LevelText() const;
	FText PartyText() const;
	FText MetaText() const;
	FText BossText() const;
	FText ShopTitleText() const;
	FText ShopOffersText() const;
	FText ShopHintText() const;
	FText ToastText() const;
	float HpPercent() const;
	float BossPercent() const;

	TSharedPtr<FWS2RunState> State;
};
