#pragma once

#include "CoreMinimal.h"

/** xorshift64* — same job as the source game's seeded RNG, seedable per run. */
struct FWS2Rng
{
	uint64 State = 0x9E3779B97F4A7C15ull;

	FWS2Rng() = default;
	explicit FWS2Rng(uint64 Seed)
	{
		State = Seed ? Seed : 0x9E3779B97F4A7C15ull;
	}

	uint32 NextU32()
	{
		State ^= State << 13;
		State ^= State >> 7;
		State ^= State << 17;
		return static_cast<uint32>(State >> 32);
	}

	float FRand() { return (NextU32() >> 8) * (1.0f / 16777216.0f); }
	float Range(float A, float B) { return A + (B - A) * FRand(); }
	int32 RangeI(int32 A, int32 B)
	{
		const int32 Span = FMath::Max(1, B - A + 1);
		return A + static_cast<int32>(NextU32() % static_cast<uint32>(Span));
	}
	bool Chance(float Probability) { return FRand() < Probability; }

	template <typename T>
	const T& Pick(const TArray<T>& Array)
	{
		return Array[static_cast<int32>(NextU32() % static_cast<uint32>(FMath::Max(1, Array.Num())))];
	}
};

/** Uniform grid for enemy separation, projectile hits and auto-aim lookups. */
struct FWS2SpatialGrid
{
	float CellSize = 96.f;
	TMap<int64, TArray<int32>> Cells;

	static int64 MakeKey(int32 X, int32 Y)
	{
		return (static_cast<int64>(static_cast<uint32>(X)) << 32) | static_cast<uint32>(Y);
	}

	int32 Coord(float Value) const { return FMath::FloorToInt(Value / CellSize); }

	void Reset() { Cells.Reset(); }

	void Insert(int32 Index, const FVector2D& Position)
	{
		Cells.FindOrAdd(MakeKey(Coord(Position.X), Coord(Position.Y))).Add(Index);
	}

	void Query(const FVector2D& Position, float Radius, TArray<int32>& Out) const
	{
		Out.Reset();
		const int32 X0 = Coord(Position.X - Radius);
		const int32 X1 = Coord(Position.X + Radius);
		const int32 Y0 = Coord(Position.Y - Radius);
		const int32 Y1 = Coord(Position.Y + Radius);
		for (int32 X = X0; X <= X1; ++X)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				if (const TArray<int32>* Bucket = Cells.Find(MakeKey(X, Y)))
				{
					Out.Append(*Bucket);
				}
			}
		}
	}
};
