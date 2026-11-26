#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WS2ArenaCamera.generated.h"

class UCameraComponent;
class AWS2PlayerPawn;

/**
 * Framing rules lifted from the shipping game: the arena fits in frame, the
 * camera dollies out under pressure, leads slightly towards the aim, punches on
 * heavy hits and shakes directionally.
 */
UCLASS()
class WS2GAMEPLAY_API AWS2ArenaCamera : public AActor
{
	GENERATED_BODY()

public:
	AWS2ArenaCamera();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void SetTargetPlayer(AWS2PlayerPawn* InPlayer) { Player = InPlayer; }
	void SetArenaExtent(const FVector2D& InExtent) { Extent = InExtent; CurrentHeight = ComputeHeight(0); }
	void AddShake(const FVector2D& At, float Amount, float Duration);
	void Punch(float Amount);
	void SetBossTarget(bool bActive, const FVector2D& At, float HpPct);
	bool IsBossFrame() const { return bBossFrame; }
	int32 GetShakeCount() const { return ShakeCount; }
	int32 GetPunchCount() const { return PunchCount; }
	int32 GetBossFrames() const { return BossFrames; }
	int32 GetDollyBands() const { return DollyBands; }

private:
	float ComputeHeight(int32 AliveEnemies) const;
	float FitHeight() const;
	const TCHAR* DensityBand(int32 AliveEnemies) const;

	UPROPERTY()
	USceneComponent* Root = nullptr;

	UPROPERTY()
	UCameraComponent* Camera = nullptr;

	TWeakObjectPtr<AWS2PlayerPawn> Player;

	FVector2D Extent = FVector2D(1900.f, 1500.f);
	float CurrentHeight = 3200.f;
	float AimLead = 0.f;
	float ShakeAmount = 0.f;
	float ShakeTime = 0.f;
	float ShakeDuration = 0.f;
	FVector2D ShakeFrom = FVector2D::ZeroVector;
	float PunchAmount = 0.f;
	bool bBossFrame = false;
	FVector2D BossPos = FVector2D::ZeroVector;
	float BossHp = 0.f;
	bool bLeadLogged = false;
	FString LastBand = TEXT("");
	int32 ShakeCount = 0;
	int32 PunchCount = 0;
	int32 BossFrames = 0;
	int32 DollyBands = 0;
};
