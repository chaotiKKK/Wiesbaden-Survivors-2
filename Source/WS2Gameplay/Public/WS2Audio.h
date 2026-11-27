#pragma once

#include "CoreMinimal.h"

class UWorld;

/** Tiny synthesized SFX layer: no imported audio assets, no extra runtime dependency. */
namespace WS2Audio
{
	enum class ESfx : uint8
	{
		Shot = 0,
		Hit = 1,
		Kill = 2,
		Pickup = 3,
		LevelUp = 4,
		Wave = 5,
		Boss = 6,
		Down = 7,
		Shop = 8,
		Win = 9
	};

	/** Plays locally; dedicated servers stay silent. Repeated events are throttled. */
	WS2GAMEPLAY_API void Play(UWorld* World, ESfx Sfx, const FVector& Location = FVector::ZeroVector, float Volume = 1.f, const AActor* Source = nullptr);
}