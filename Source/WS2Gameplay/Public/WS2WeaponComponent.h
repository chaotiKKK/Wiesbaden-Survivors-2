#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WS2Data.h"
#include "WS2Math.h"
#include "WS2WeaponComponent.generated.h"

class AWS2PlayerPawn;
class AWS2GameMode;

/**
 * Brotato-style auto-fire: finds the nearest enemy in range and fires the
 * weapon's current tier. Tier rows come verbatim from the shipping game.
 */
UCLASS(ClassGroup = (WS2), meta = (BlueprintSpawnableComponent))
class WS2GAMEPLAY_API UWS2WeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWS2WeaponComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetWeapon(const FString& InWeaponId, int32 InTier = 0);
	/** Empties the slot; used by the six-slot loadout before the run starts. */
	void ClearWeapon();
	void SetAutoEquipDefault(bool bInAutoEquip) { bAutoEquipDefault = bInAutoEquip; }
	void SetTier(int32 InTier) { Tier = FMath::Max(0, InTier); }

	const FString& GetWeaponId() const { return WeaponId; }
	const WS2::FWeaponDef* GetDef() const { return Def; }
	int32 GetTier() const { return Tier; }
	FString GetDisplayName() const { return Def ? Def->Name : FString(TEXT("-")); }
	FString GetLastBehavior() const { return LastBehavior; }
	float GetRange() const;

	/** Fires once regardless of cooldown; the content proof uses this on every tier row. */
	bool FireProofOnce();

private:
	void FireAt(const FVector2D& From, const FVector2D& Target, bool bFromInput = false);

	FString WeaponId;
	int32 Tier = 0;
	const WS2::FWeaponDef* Def = nullptr;
	bool bAutoEquipDefault = true;

	float Cooldown = 0.f;
	FString LastBehavior;
	FWS2Rng Rng;

	AWS2PlayerPawn* OwnerPawn = nullptr;
	AWS2GameMode* GameMode = nullptr;
};
