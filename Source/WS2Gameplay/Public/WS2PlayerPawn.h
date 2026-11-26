#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "WS2Data.h"
#include "WS2PlayerPawn.generated.h"

class UCameraComponent;
class USphereComponent;
class UStaticMeshComponent;
class UWS2WeaponComponent;
class AWS2Crowd;

/** Auto-aiming top-down survivor: movement is kinematic, fire control is in the weapon component. */
UCLASS()
class WS2GAMEPLAY_API AWS2PlayerPawn : public APawn
{
	GENERATED_BODY()

public:
	AWS2PlayerPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void MoveForward(float Value);
	void MoveRight(float Value);

	void ApplyContactDamage(float Amount);
	void Heal(float Amount);
	/** Fired by the weapon: switches the VAT atlas to the attack clip. */
	void PlayAttackAnim();

	FVector2D GetPos2D() const;
	/** Player position in arena space (top-left origin), used by the simulation. */
	FVector2D GetArenaPos() const;
	FVector2D GetAimDir() const { return AimDir; }
	float GetRadius() const { return Radius; }
	float GetAttackSpeedPct() const { return AttackSpeedPct; }
	float GetDamagePct() const { return DamagePct; }
	float GetRangedPct() const { return RangedPct; }
	float GetCritPct() const { return CritPct; }
	float GetCritDmgPct() const { return CritDmgPct; }
	float GetMeleePct() const { return MeleePct; }
	float GetElementPct() const { return ElementPct; }
	float GetKnockbackPct() const { return KnockbackPct; }
	float GetLifestealPct() const { return LifestealPct; }
	float GetRangeMultiplier() const { return 1.f + RangePct / 100.f; }
	float GetPickupRadius() const { return PickupRadius; }

	/** Applies one item's stat block; the shop calls this for every purchased item. */
	void ApplyItemStats(const TMap<FString, float>& Stats);
	UStaticMeshComponent* GetBody() const { return Body; }

	float Hp = 100.f;
	float MaxHp = 100.f;
	float HpRegen = 0.f;
	float AttackSpeedPct = 0.f;
	float DamagePct = 0.f;
	float RangedPct = 0.f;
	float CritPct = 0.f;
	float RangePct = 0.f;
	float Armor = 0.f;
	float SpeedPct = 0.f;
	float CritDmgPct = 0.f;
	float MeleePct = 0.f;
	float ElementPct = 0.f;
	float KnockbackPct = 0.f;
	float LifestealPct = 0.f;
	float DodgePct = 0.f;
	float Luck = 0.f;
	float Harvest = 0.f;
	float XpGainPct = 0.f;
	float ExplosionSizePct = 0.f;
	float PierceBonus = 0.f;
	float BounceBonus = 0.f;
	float Engineering = 0.f;
	float PickupRadius = 260.f;
	bool bDown = false;
	bool bInvulnerable = false;

private:
	void FindTarget();
	void SetHeroClip(const FString& Clip);

	UPROPERTY()
	USphereComponent* Collision = nullptr;

	UPROPERTY()
	UStaticMeshComponent* Body = nullptr;

	UPROPERTY()
	UWS2WeaponComponent* Weapon = nullptr;

	FVector2D InputVector = FVector2D::ZeroVector;
	FVector2D AimDir = FVector2D(1.f, 0.f);
	FVector2D AimTarget = FVector2D::ZeroVector;
	float Radius = 34.f;
	float FireAnimTime = 0.f;

	// Vertex-animation playback for the hero body.
	UPROPERTY()
	UMaterialInstanceDynamic* BodyMid = nullptr;

	const WS2::FVatInfo* HeroVat = nullptr;
	FString HeroClip = TEXT("idle");
	float AttackAnimLeft = 0.f;
	bool bWasMoving = false;
};
