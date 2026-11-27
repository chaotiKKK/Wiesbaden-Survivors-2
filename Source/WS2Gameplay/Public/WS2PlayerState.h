#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "WS2PlayerState.generated.h"

/** Per-player identity for LAN co-op. The run itself lives in AWS2GameState. */
UCLASS()
class WS2GAMEPLAY_API AWS2PlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AWS2PlayerState();

	UPROPERTY(Replicated)
	int32 Slot = 0;

	UPROPERTY(Replicated)
	FString HeroId;

	UPROPERTY(Replicated)
	FString HeroName;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
