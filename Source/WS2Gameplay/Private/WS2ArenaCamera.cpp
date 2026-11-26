#include "WS2ArenaCamera.h"

#include "Camera/CameraComponent.h"
#include "WS2GameMode.h"
#include "WS2PlayerPawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Cam, Log, All);

AWS2ArenaCamera::AWS2ArenaCamera()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetFieldOfView(35.f);
	Camera->bConstrainAspectRatio = false;
	Camera->SetProjectionMode(ECameraProjectionMode::Perspective);
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.35f;
	// Fixed exposure: equal min/max EV100 locks the eye adaptation, so the flat
	// emissive blockout palette never re-exposes between frames.
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.f;
	Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Camera->PostProcessSettings.AutoExposureMinBrightness = 1.2f;
	Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.2f;
}

void AWS2ArenaCamera::BeginPlay()
{
	Super::BeginPlay();
	CurrentHeight = ComputeHeight(0);
	SetActorRotation(FRotator(-89.5f, 0.f, 0.f));
}

float AWS2ArenaCamera::FitHeight() const
{
	const float HalfFov = FMath::DegreesToRadians(35.f * 0.5f);
	const float Fit = FMath::Max(Extent.Y, Extent.X / 1.7778f) * 0.5f / FMath::Tan(HalfFov);
	return Fit * 1.06f;
}

float AWS2ArenaCamera::ComputeHeight(int32 AliveEnemies) const
{
	float Height = FitHeight();

	// Dolly out under pressure (source game: .16 / 1.06).
	const float Density = FMath::Clamp((AliveEnemies - 60.f) / 240.f, 0.f, 1.f);
	Height *= 1.f + Density * 0.14f;
	return Height;
}

void AWS2ArenaCamera::SetBossTarget(bool bActive, const FVector2D& At, float HpPct)
{
	if (bActive != bBossFrame)
	{
		UE_LOG(LogWS2Cam, Display, TEXT("CAMERA boss-frame %s at (%.0f, %.0f) hp=%.0f%%"),
			bActive ? TEXT("enter") : TEXT("exit"), At.X, At.Y, HpPct * 100.f);
		if (bActive)
		{
			++BossFrames;
		}
	}
	bBossFrame = bActive;
	BossPos = At;
	BossHp = HpPct;
}

const TCHAR* AWS2ArenaCamera::DensityBand(int32 AliveEnemies) const
{
	if (AliveEnemies < 60)
	{
		return TEXT("calm");
	}
	return AliveEnemies < 180 ? TEXT("pressure") : TEXT("crowded");
}

void AWS2ArenaCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Player.IsValid())
	{
		return;
	}

	AWS2GameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AWS2GameMode>() : nullptr;
	const FWS2RunState& State = GameMode ? GameMode->GetState() : FWS2RunState();
	const int32 Alive = State.AliveEnemies;

	const TCHAR* Band = DensityBand(Alive);
	if (Band != LastBand)
	{
		UE_LOG(LogWS2Cam, Display, TEXT("CAMERA dolly band=%s alive=%d height=%.0f"),
			Band, Alive, ComputeHeight(Alive));
		LastBand = Band;
		++DollyBands;
	}

	AimLead = FMath::FInterpTo(AimLead, 74.f, DeltaSeconds, 3.f);
	const FVector2D Lead = Player->GetAimDir() * AimLead;
	if (!bLeadLogged && AimLead > 60.f)
	{
		bLeadLogged = true;
		UE_LOG(LogWS2Cam, Display, TEXT("CAMERA aim-lead active offset=%.0f"), AimLead);
	}

	// Boss framing: pull the pair (player + boss) into one shot instead of the
	// whole arena, so the elite reads at the moment it matters.
	float TargetHeight = ComputeHeight(Alive);
	FVector2D Center = Player->GetPos2D() + Lead;
	if (bBossFrame)
	{
		// Frame the pair without changing the zoom: the whole arena stays in shot,
		// the elite is simply pulled towards the centre of the frame.
		Center = (Player->GetPos2D() + BossPos) * 0.5f + Lead * 0.4f;
	}
	CurrentHeight = FMath::FInterpTo(CurrentHeight, TargetHeight, DeltaSeconds, 2.2f);

	const float HalfHeight = FMath::Tan(FMath::DegreesToRadians(17.5f)) * CurrentHeight;
	const float HalfWidth = HalfHeight * 1.7778f;

	if (Extent.X > HalfWidth * 2.f)
	{
		Center.X = FMath::Clamp(Center.X, HalfWidth, Extent.X - HalfWidth);
	}
	else
	{
		Center.X = Extent.X * 0.5f;
	}

	if (Extent.Y > HalfHeight * 2.f)
	{
		Center.Y = FMath::Clamp(Center.Y, HalfHeight, Extent.Y - HalfHeight);
	}
	else
	{
		Center.Y = Extent.Y * 0.5f;
	}

	FVector2D Shake = FVector2D::ZeroVector;
	if (ShakeTime > 0.f)
	{
		ShakeTime = FMath::Max(0.f, ShakeTime - DeltaSeconds);
		const float Falloff = ShakeDuration > 0.f ? ShakeTime / ShakeDuration : 0.f;
		const FVector2D Away = (Player->GetPos2D() - ShakeFrom).GetSafeNormal();
		const float Wobble = FMath::Sin(ShakeTime * 90.f) + FMath::Sin(ShakeTime * 37.f) * 0.5f;
		Shake = (Away * ShakeAmount + FVector2D(Wobble, -Wobble) * ShakeAmount * 0.45f) * Falloff;
	}

	PunchAmount = FMath::Max(0.f, PunchAmount - DeltaSeconds * 2.5f);

	const float Height = CurrentHeight * (1.f - PunchAmount * 0.07f);
	const FVector World(
		Center.X - Extent.X * 0.5f + Shake.X,
		Center.Y - Extent.Y * 0.5f + Shake.Y,
		Height);

	SetActorLocation(World);
	SetActorRotation(FRotator(-89.5f, 0.f, 0.f));
}

void AWS2ArenaCamera::AddShake(const FVector2D& At, float Amount, float Duration)
{
	UE_LOG(LogWS2Cam, Display, TEXT("CAMERA shake amount=%.1f from=(%.0f, %.0f) duration=%.2f"),
		Amount, At.X, At.Y, Duration);
	++ShakeCount;
	ShakeAmount = FMath::Max(ShakeAmount * 0.5f, Amount);
	ShakeDuration = FMath::Max(0.05f, Duration);
	ShakeTime = ShakeDuration;
	ShakeFrom = At;
}

void AWS2ArenaCamera::Punch(float Amount)
{
	if (Amount > 0.f)
	{
		UE_LOG(LogWS2Cam, Display, TEXT("CAMERA zoom-punch amount=%.2f"), Amount);
		++PunchCount;
	}
	PunchAmount = FMath::Max(PunchAmount, Amount);
}
