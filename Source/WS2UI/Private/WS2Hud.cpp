#include "WS2Hud.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor LabelColor(0.56f, 0.66f, 0.77f, 1.f);
	const FLinearColor ValueColor(0.94f, 0.97f, 1.f, 1.f);
	const FLinearColor AccentColor(1.f, 0.82f, 0.32f, 1.f);
	const FLinearColor PanelColor(0.01f, 0.02f, 0.035f, 0.62f);

	TSharedRef<SWidget> StatRow(const FString& Label, TAttribute<FText> Value)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 1.f, 12.f, 1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
				.ColorAndOpacity(LabelColor)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Value)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
				.ColorAndOpacity(ValueColor)
			];
	}

	TSharedRef<SWidget> Card(TSharedRef<SWidget> Content)
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(PanelColor)
			.Padding(FMargin(14.f, 10.f))
			[ Content ];
	}
}

void SWS2Hud::Construct(const FArguments& InArgs)
{
	State = InArgs._State;

	ChildSlot
	[
		SNew(SOverlay)

		// ----------------------------------------------------------- top left
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(24.f, 20.f))
		[
			SNew(SBox).WidthOverride(360.f)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return HeroText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 19))
						.ColorAndOpacity(AccentColor)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
					[
						SNew(SBox).HeightOverride(9.f)
						[
							SNew(SProgressBar)
							.Percent_Lambda([this] { return HpPercent(); })
							.FillColorAndOpacity(FLinearColor(0.92f, 0.24f, 0.30f, 1.f))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 8.f)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return HpText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
						.ColorAndOpacity(ValueColor)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
					[
						StatRow(TEXT("LEVEL"), TAttribute<FText>::CreateLambda([this] { return LevelText(); }))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return PartyText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
						.ColorAndOpacity(LabelColor)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return MetaText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(FLinearColor(0.58f, 0.72f, 0.58f, 0.9f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
					[
						StatRow(TEXT("WAFFE"), TAttribute<FText>::CreateLambda([this] { return WeaponText(); }))
					]
				)
			]
		]

		// --------------------------------------------------------- top center
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 20.f))
		[
			SNew(SBox).WidthOverride(360.f)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return ArenaText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
						.ColorAndOpacity(LabelColor)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return WaveText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
						.ColorAndOpacity(ValueColor)
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return TimerText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
						.ColorAndOpacity(LabelColor)
					]
				)
			]
		]

		// ---------------------------------------------------------- top right
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(FMargin(24.f, 20.f))
		[
			SNew(SBox).WidthOverride(230.f)
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("GEGNER"), TAttribute<FText>::CreateLambda([this] { return EnemyText(); })) ]
					+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("KILLS"), TAttribute<FText>::CreateLambda([this] { return KillsText(); })) ]
					+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("MATERIAL"), TAttribute<FText>::CreateLambda([this] { return MaterialsText(); })) ]
					+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("FPS"), TAttribute<FText>::CreateLambda([this] { return FpsText(); })) ]
				)
			]
		]

		// --------------------------------------------------------- boss frame
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0.f, 128.f, 0.f, 0.f))
		[
			SNew(SBox).WidthOverride(460.f)
			.Visibility_Lambda([this] { return State.IsValid() && State->bBossAlive ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return BossText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
						.ColorAndOpacity(FLinearColor(1.f, 0.45f, 0.35f, 1.f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 5.f, 0.f, 0.f)
					[
						SNew(SBox).HeightOverride(7.f)
						[
							SNew(SProgressBar)
							.Percent_Lambda([this] { return BossPercent(); })
							.FillColorAndOpacity(FLinearColor(1.f, 0.32f, 0.28f, 1.f))
						]
					]
				)
			]
		]

		// --------------------------------------------------------------- shop
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0.f, 90.f, 0.f, 0.f))
		[
			SNew(SBox).WidthOverride(520.f)
			.Visibility_Lambda([this] { return State.IsValid() && State->bShopOpen ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				Card(SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return ShopTitleText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
						.ColorAndOpacity(AccentColor)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return ShopOffersText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
						.ColorAndOpacity(ValueColor)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f).HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([this] { return ShopHintText(); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
						.ColorAndOpacity(LabelColor)
					]
				)
			]
		]

		// -------------------------------------------------------- bottom center
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(FMargin(0.f, 0.f, 0.f, 42.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return ToastText(); })
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
				.ColorAndOpacity(FLinearColor(0.65f, 1.f, 0.78f, 0.95f))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return StatusText(); })
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
				.ColorAndOpacity(FLinearColor(1.f, 0.88f, 0.44f, 0.95f))
			]
		]

		// --------------------------------------------------------- bottom left
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(FMargin(24.f, 0.f, 0.f, 24.f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("WASD / STICK  -  AUTO-FEUER  -  ARENA UEBERLEBEN")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
			.ColorAndOpacity(LabelColor)
		]
	];
}

FText SWS2Hud::HeroText() const
{
	return State.IsValid() ? FText::FromString(State->HeroName) : FText::GetEmpty();
}

FText SWS2Hud::HpText() const
{
	return State.IsValid()
		? FText::FromString(FString::Printf(TEXT("%.0f / %.0f LP"), State->Hp, State->MaxHp))
		: FText::GetEmpty();
}

FText SWS2Hud::LevelText() const
{
	return State.IsValid()
		? FText::FromString(FString::Printf(TEXT("%d  (%d / %d EP)"), State->Level, State->Xp, State->XpNext))
		: FText::GetEmpty();
}

FText SWS2Hud::PartyText() const
{
	if (!State.IsValid() || State->PartyLines.Num() == 0)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(FString::Join(State->PartyLines, TEXT("\n")));
}

FText SWS2Hud::MetaText() const
{
	return State.IsValid() ? FText::FromString(State->MetaText) : FText::GetEmpty();
}

FText SWS2Hud::WeaponText() const
{
	return State.IsValid() ? FText::FromString(State->WeaponName) : FText::GetEmpty();
}

FText SWS2Hud::ArenaText() const
{
	return State.IsValid() ? FText::FromString(State->ArenaName) : FText::GetEmpty();
}

FText SWS2Hud::WaveText() const
{
	return State.IsValid() ? FText::FromString(FString::Printf(TEXT("WELLE %d"), State->Wave)) : FText::GetEmpty();
}

FText SWS2Hud::TimerText() const
{
	return State.IsValid()
		? FText::FromString(FString::Printf(TEXT("%.0f s  |  %d in der Warteschlange"), FMath::CeilToFloat(State->WaveTimeLeft), State->AliveEnemies))
		: FText::GetEmpty();
}

FText SWS2Hud::EnemyText() const
{
	return State.IsValid() ? FText::FromString(FString::Printf(TEXT("%d  (peak %d)"), State->AliveEnemies, State->PeakEnemies)) : FText::GetEmpty();
}

FText SWS2Hud::KillsText() const
{
	return State.IsValid() ? FText::FromString(FString::Printf(TEXT("%d"), State->Kills)) : FText::GetEmpty();
}

FText SWS2Hud::MaterialsText() const
{
	return State.IsValid() ? FText::FromString(FString::Printf(TEXT("%d"), State->Materials)) : FText::GetEmpty();
}

FText SWS2Hud::FpsText() const
{
	return State.IsValid() ? FText::FromString(FString::Printf(TEXT("%.0f"), State->AvgFps)) : FText::GetEmpty();
}

FText SWS2Hud::StatusText() const
{
	return State.IsValid() ? FText::FromString(State->Status) : FText::GetEmpty();
}

FText SWS2Hud::BossText() const
{
	return State.IsValid()
		? FText::FromString(FString::Printf(TEXT("%s  P%d  %.0f%%"), *State->BossName, State->BossPhase, State->BossHpPct * 100.f))
		: FText::GetEmpty();
}

float SWS2Hud::BossPercent() const
{
	return State.IsValid() ? FMath::Clamp(State->BossHpPct, 0.f, 1.f) : 0.f;
}

FText SWS2Hud::ShopTitleText() const
{
	if (!State.IsValid())
	{
		return FText::GetEmpty();
	}
	return FText::FromString(State->bLevelUp ? TEXT("LEVEL-UP  -  BELOHNUNG") : TEXT("SHOP"));
}

FText SWS2Hud::ShopOffersText() const
{
	if (!State.IsValid())
	{
		return FText::GetEmpty();
	}

	FString Text;
	for (int32 i = 0; i < State->ShopOffers.Num(); ++i)
	{
		Text += FString::Printf(TEXT("%d)  %s\n"), i + 1, *State->ShopOffers[i]);
	}
	return FText::FromString(Text.TrimEnd());
}

FText SWS2Hud::ShopHintText() const
{
	return State.IsValid() ? FText::FromString(State->ShopHint) : FText::GetEmpty();
}

FText SWS2Hud::ToastText() const
{
	return State.IsValid() && State->ToastTime > 0.f ? FText::FromString(State->Toast) : FText::GetEmpty();
}

float SWS2Hud::HpPercent() const
{
	return State.IsValid() && State->MaxHp > 0.f ? FMath::Clamp(State->Hp / State->MaxHp, 0.f, 1.f) : 0.f;
}
