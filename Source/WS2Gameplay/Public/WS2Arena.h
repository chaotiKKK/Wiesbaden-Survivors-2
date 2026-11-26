#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WS2Data.h"
#include "WS2Math.h"
#include "WS2Arena.generated.h"

class UStaticMeshComponent;

/**
 * Runtime-built arena. Everything (floor, perimeter, blocks, cover, hazards) is
 * generated from the extracted arena data, so no hand-authored level is required.
 * Coordinates are local 2D: (0,0) top-left to (W,H) bottom-right.
 */
UCLASS()
class WS2GAMEPLAY_API AWS2Arena : public AActor
{
	GENERATED_BODY()

public:
	AWS2Arena();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void Configure(const WS2::FArenaDef& InDef);
	bool ResolveCircle(FVector2D& Pos, float Radius) const;
	FVector2D ClampToBounds(const FVector2D& Pos, float Radius) const;
	FVector2D GetRandomOpenPoint(FWS2Rng& Rng, float Radius) const;

	FVector2D GetExtent() const { return FVector2D(Def.W, Def.H); }
	FVector2D GetCenter() const { return FVector2D(Def.W * 0.5f, Def.H * 0.5f); }
	const WS2::FArenaDef& GetDef() const { return Def; }
	FString GetKitName() const { return KitName; }

	FVector2D ArenaToWorld(const FVector2D& P) const { return FVector2D(P.X - Def.W * 0.5f, P.Y - Def.H * 0.5f); }
	FVector2D WorldToArena(const FVector2D& W) const { return FVector2D(W.X + Def.W * 0.5f, W.Y + Def.H * 0.5f); }

	/** Arena modifiers from the source data, actually sampled by the simulation. */
	float GetPoisonDpsAt(const FVector2D& Pos) const;
	float GetSpeedMultiplierAt(const FVector2D& Pos) const;
	FString GetModSummary() const;

	struct FBlock
	{
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;
		bool bCover = false;
		int32 MovingWall = -1;
		UStaticMeshComponent* Visual = nullptr;
	};

	struct FPoisonPool
	{
		FVector2D Center = FVector2D::ZeroVector;
		float Radius = 180.f;
		float Dps = 4.f;
	};

	struct FSpeedField
	{
		FVector2D Center = FVector2D::ZeroVector;
		float Radius = 150.f;
		float Multiplier = 1.35f;
	};

	struct FMovingWall
	{
		int32 BlockIndex = INDEX_NONE;
		FVector2D BaseMin = FVector2D::ZeroVector;
		FVector2D BaseMax = FVector2D::ZeroVector;
		FVector2D Axis = FVector2D(1.f, 0.f);
		float Amplitude = 180.f;
		float Speed = 0.6f;
		float Phase = 0.f;
	};

	TArray<FBlock> Blocks;

private:
	void Build();
	void BuildKurparkLayout();
	void BuildStreetLayout();
	void BuildPromenadeLayout();
	void BuildRoomLayout();
	void BuildBossLayout();
	void AddArenaMods();
	void AddBlock(const FVector2D& Min, const FVector2D& Max, bool bCover, float Height, const FString& Piece);
	bool OverlapsAny(const FVector2D& Min, const FVector2D& Max, float Padding) const;

	UStaticMeshComponent* AddKit(const FString& Piece, const FVector2D& Center, const FVector2D& Size,
		float UniformScale, float Yaw, const FLinearColor& Tint, float TargetHeight = 0.f);
	UStaticMesh* LoadKitMesh(const FString& Piece);
	UStaticMeshComponent* AddDisc(const FVector2D& Center, float Radius, const FLinearColor& Color, float Z);
	void UpdateMovingWalls(float DeltaSeconds);
	FLinearColor PieceTint(const FString& Piece) const;

	TMap<FString, UStaticMesh*> KitMeshes;
	TMap<FString, int32> KitCounts;

	WS2::FArenaDef Def;
	FString KitName;
	bool bConfigured = false;
	float ElapsedTime = 0.f;

	TArray<FPoisonPool> PoisonPools;
	TArray<FSpeedField> SpeedFields;
	TArray<FMovingWall> MovingWalls;

	UPROPERTY()
	USceneComponent* Root = nullptr;

	UPROPERTY()
	UStaticMeshComponent* Ground = nullptr;

	UPROPERTY()
	TArray<UStaticMeshComponent*> WallMeshes;

	UPROPERTY()
	TArray<UStaticMeshComponent*> BlockMeshes;

	UPROPERTY()
	TArray<UStaticMeshComponent*> HazardMeshes;
};
