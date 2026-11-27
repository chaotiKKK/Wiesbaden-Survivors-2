#include "WS2Arena.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "WS2Data.h"
#include "WS2Math.h"

DEFINE_LOG_CATEGORY_STATIC(LogWS2Arena, Log, All);

namespace
{
	FLinearColor HexToColor(const FString& Hex, const FLinearColor& Fallback)
	{
		FString Value = Hex.TrimStartAndEnd();
		if (Value.StartsWith(TEXT("#")))
		{
			Value = Value.RightChop(1);
		}
		if (Value.Len() >= 6)
		{
			const int32 R = FParse::HexNumber(*Value.Mid(0, 2));
			const int32 G = FParse::HexNumber(*Value.Mid(2, 2));
			const int32 B = FParse::HexNumber(*Value.Mid(4, 2));
			return FLinearColor(R / 255.f, G / 255.f, B / 255.f);
		}
		return Fallback;
	}

	UMaterialInterface* LoadColorMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			UMaterialInterface* Found = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/M_WS2Color.M_WS2Color"));
			if (!Found)
			{
				Found = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
			}
			Cached = Found;
		}
		return Cached.Get();
	}

	struct FKitSpec
	{
		FString Perimeter;
		FString Building;
		FString Lamp;
		TArray<FString> Cover;
	};

	FKitSpec SpecFor(const FString& Id)
	{
		if (Id == TEXT("innenstadt"))
		{
			return { TEXT("Facade"), TEXT("Arcade"), TEXT("Lamp"), { TEXT("Column"), TEXT("Gate"), TEXT("Planter") } };
		}
		if (Id == TEXT("rheinufer"))
		{
			return { TEXT("Railing"), TEXT("Kiosk"), TEXT("Lamp"), { TEXT("Ponton"), TEXT("Crane"), TEXT("Bollard") } };
		}
		if (Id == TEXT("labor"))
		{
			return { TEXT("LabWall"), TEXT("Tank"), TEXT("Lamp"), { TEXT("Console"), TEXT("Pipe"), TEXT("Crate") } };
		}
		if (Id == TEXT("neroberg"))
		{
			return { TEXT("Pine"), TEXT("Monopteros"), TEXT("Lamp"), { TEXT("Rail"), TEXT("Funicular"), TEXT("Vista") } };
		}
		if (Id == TEXT("warmerdamm"))
		{
			return { TEXT("Hedge"), TEXT("Bandstand"), TEXT("Lamp"), { TEXT("Palm"), TEXT("Pond"), TEXT("Bench") } };
		}
		if (Id == TEXT("schlachthof"))
		{
			return { TEXT("Hall"), TEXT("Boiler"), TEXT("Lamp"), { TEXT("Hook"), TEXT("Chain"), TEXT("Trough") } };
		}
		return { TEXT("Hedge"), TEXT("Pavilion"), TEXT("Lantern"), { TEXT("Tree"), TEXT("Fountain"), TEXT("Bench") } };
	}

	FString KitNameFor(const FString& Id)
	{
		if (Id == TEXT("innenstadt")) return TEXT("Innenstadt");
		if (Id == TEXT("rheinufer")) return TEXT("Rheinufer");
		if (Id == TEXT("labor")) return TEXT("Labor");
		if (Id == TEXT("neroberg")) return TEXT("Neroberg");
		if (Id == TEXT("warmerdamm")) return TEXT("Warmerdamm");
		if (Id == TEXT("schlachthof")) return TEXT("Schlachthof");
		return TEXT("Kurpark");
	}
}

AWS2Arena::AWS2Arena()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(2.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AWS2Arena::Configure(const WS2::FArenaDef& InDef)
{
	Def = InDef;
	KitName = KitNameFor(Def.Id);
	bConfigured = true;
	NetArenaId = Def.Id;
}

void AWS2Arena::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWS2Arena, NetArenaId);
}

void AWS2Arena::OnRep_NetArenaId()
{
	if (NetArenaId.IsEmpty())
	{
		return;
	}
	if (const WS2::FArenaDef* DefRow = WS2::FDatabase::Get().FindArena(NetArenaId))
	{
		Configure(*DefRow);
		Build();
	}
}

void AWS2Arena::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		if (!NetArenaId.IsEmpty())
		{
			OnRep_NetArenaId();
		}
		return;
	}

	if (!bConfigured)
	{
		if (WS2::FDatabase::Get().EnsureLoaded() && WS2::FDatabase::Get().Arenas.Num() > 0)
		{
			Def = WS2::FDatabase::Get().Arenas[0];
			KitName = KitNameFor(Def.Id);
			bConfigured = true;
		}
	}

	Build();
}

void AWS2Arena::Build()
{
	if (!bConfigured || Blocks.Num() > 0)
	{
		return;
	}

	const float W = Def.W;
	const float H = Def.H;
	const float WallThickness = 70.f;

	// floor ------------------------------------------------------------------
	Ground = NewObject<UStaticMeshComponent>(this, TEXT("Ground"));
	AddInstanceComponent(Ground);
	Ground->SetupAttachment(Root);
	Ground->RegisterComponent();
	Ground->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
	Ground->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ground->SetCastShadow(false);
	Ground->SetRelativeLocation(FVector(0.f, 0.f, -4.f));
	Ground->SetRelativeScale3D(FVector(W / 100.f, H / 100.f, 1.f));

	if (UMaterialInterface* BaseMat = LoadColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, this);
		const FLinearColor GroundColor = HexToColor(Def.Ground, FLinearColor(0.05f, 0.09f, 0.07f));
		// The floor must stay readable under the HUD: lift the raw palette a little.
		Mid->SetVectorParameterValue(TEXT("Color"),
			FLinearColor(GroundColor.R * 0.30f, GroundColor.G * 0.34f, GroundColor.B * 0.32f, 1.f));
		Ground->SetMaterial(0, Mid);

		UStaticMeshComponent* Backdrop = NewObject<UStaticMeshComponent>(this, TEXT("Backdrop"));
		Backdrop->SetupAttachment(Root);
		AddInstanceComponent(Backdrop);
		Backdrop->RegisterComponent();
		Backdrop->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
		Backdrop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Backdrop->SetCastShadow(false);
		Backdrop->SetRelativeLocation(FVector(0.f, 0.f, -18.f));
		Backdrop->SetRelativeScale3D(FVector(120.f, 120.f, 1.f));
		UMaterialInstanceDynamic* BackdropMid = UMaterialInstanceDynamic::Create(BaseMat, this);
		BackdropMid->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.025f, 0.04f, 0.07f, 1.f));
		Backdrop->SetMaterial(0, BackdropMid);
	}

	// perimeter: the kit's own wall module, not a shared hedge ------------------
	const FKitSpec Spec = SpecFor(Def.Id);
	AddBlock(FVector2D(0.f, 0.f), FVector2D(W, WallThickness), false, 190.f, Spec.Perimeter);
	AddBlock(FVector2D(0.f, H - WallThickness), FVector2D(W, H), false, 190.f, Spec.Perimeter);
	AddBlock(FVector2D(0.f, 0.f), FVector2D(WallThickness, H), false, 190.f, Spec.Perimeter);
	AddBlock(FVector2D(W - WallThickness, 0.f), FVector2D(W, H), false, 190.f, Spec.Perimeter);

	if (Def.Theme == TEXT("eng"))
	{
		BuildStreetLayout();
	}
	else if (Def.Theme == TEXT("weit"))
	{
		BuildPromenadeLayout();
	}
	else if (Def.Theme == TEXT("räume") || Def.Theme == TEXT("raeume"))
	{
		BuildRoomLayout();
	}
	else if (Def.Theme == TEXT("boss"))
	{
		BuildBossLayout();
	}
	else
	{
		BuildKurparkLayout();
	}

	AddArenaMods();

	FString KitSummary;
	for (const TPair<FString, int32>& Entry : KitCounts)
	{
		KitSummary += FString::Printf(TEXT("%s=%d "), *Entry.Key, Entry.Value);
	}
	UE_LOG(LogWS2Arena, Display,
		TEXT("Arena %s built: %.0fx%.0f theme=%s kit=%s blocks=%d mods[%s] kit[%s]"),
		*Def.Id, W, H, *Def.Theme, *KitName, Blocks.Num(), *GetModSummary(), *KitSummary);
}

void AWS2Arena::BuildKurparkLayout()
{
	const float W = Def.W;
	const float H = Def.H;
	const float WallThickness = 70.f;
	const FVector2D Center = GetCenter();
	FWS2Rng Rng(FCrc::StrCrc32(*Def.Id));
	const FKitSpec Spec = SpecFor(Def.Id);

	// paved centre
	for (int32 gx = -1; gx <= 1; ++gx)
	{
		for (int32 gy = -1; gy <= 1; ++gy)
		{
			const FVector2D TileCenter = Center + FVector2D(gx, gy) * 380.f;
			AddKit(TEXT("Plaza"), TileCenter, FVector2D(380.f, 380.f), 1.f, 0.f,
				HexToColor(Def.Grid, FLinearColor(0.1f, 0.14f, 0.12f)) * 1.2f);
		}
	}

	int32 Placed = 0;
	int32 Attempts = 0;
	while (Placed < Def.Buildings && Attempts < 400)
	{
		++Attempts;
		const float Bw = Rng.Range(150.f, 360.f);
		const float Bh = Rng.Range(150.f, 360.f);
		const FVector2D Min(Rng.Range(WallThickness + 40.f, W - WallThickness - Bw - 40.f),
			Rng.Range(WallThickness + 40.f, H - WallThickness - Bh - 40.f));
		const FVector2D Max = Min + FVector2D(Bw, Bh);
		if (FVector2D::Distance((Min + Max) * 0.5f, Center) < 460.f + FMath::Max(Bw, Bh) * 0.5f)
		{
			continue;
		}
		if (OverlapsAny(Min, Max, 90.f))
		{
			continue;
		}
		AddBlock(Min, Max, false, 320.f, Spec.Building);
		++Placed;
	}

	Placed = 0;
	Attempts = 0;
	while (Placed < Def.Cover && Attempts < 400)
	{
		++Attempts;
		const float Size = Rng.Range(90.f, 150.f);
		const FVector2D Min(Rng.Range(WallThickness + 30.f, W - WallThickness - Size - 30.f),
			Rng.Range(WallThickness + 30.f, H - WallThickness - Size - 30.f));
		const FVector2D Max = Min + FVector2D(Size, Size);
		if (FVector2D::Distance((Min + Max) * 0.5f, Center) < 260.f)
		{
			continue;
		}
		if (OverlapsAny(Min, Max, 60.f))
		{
			continue;
		}
		AddBlock(Min, Max, true, 95.f, Spec.Cover[Placed % Spec.Cover.Num()]);
		++Placed;
	}

	const int32 Lanterns = FMath::Max(4, FMath::RoundToInt(W / 420.f));
	for (int32 i = 0; i < Lanterns; ++i)
	{
		const float X = (W / (Lanterns + 1.f)) * (i + 1);
		AddKit(Spec.Lamp, FVector2D(X, WallThickness + 46.f), FVector2D::ZeroVector, 1.f, 0.f, FLinearColor(0.55f, 0.5f, 0.35f));
		AddKit(Spec.Lamp, FVector2D(X, H - WallThickness - 46.f), FVector2D::ZeroVector, 1.f, 180.f, FLinearColor(0.55f, 0.5f, 0.35f));
	}
}

void AWS2Arena::BuildStreetLayout()
{
	// Gründerzeit blocks and Schlachthof halls: aligned rows with narrow lanes,
	// so the player can actually feel the difference from the open park.
	const float W = Def.W;
	const float H = Def.H;
	const float WallThickness = 70.f;
	const FVector2D Center = GetCenter();
	const FKitSpec Spec = SpecFor(Def.Id);
	FWS2Rng Rng(FCrc::StrCrc32(*Def.Id));

	const int32 Columns = Def.Buildings >= 12 ? 4 : 3;
	const int32 Rows = FMath::Max(2, FMath::CeilToInt(Def.Buildings / static_cast<float>(Columns)));
	const float CellW = (W - 2.f * WallThickness - 140.f) / Columns;
	const float CellH = (H - 2.f * WallThickness - 140.f) / Rows;
	int32 Placed = 0;
	for (int32 Row = 0; Row < Rows && Placed < Def.Buildings; ++Row)
	{
		for (int32 Col = 0; Col < Columns && Placed < Def.Buildings; ++Col)
		{
			const float Bw = CellW * Rng.Range(0.62f, 0.82f);
			const float Bh = CellH * Rng.Range(0.62f, 0.82f);
			const FVector2D CellMin(WallThickness + 70.f + Col * CellW, WallThickness + 70.f + Row * CellH);
			const FVector2D Min(CellMin.X + (CellW - Bw) * 0.5f, CellMin.Y + (CellH - Bh) * 0.5f);
			const FVector2D Max = Min + FVector2D(Bw, Bh);
			if (FVector2D::Distance((Min + Max) * 0.5f, Center) < 300.f)
			{
				continue;
			}
			AddBlock(Min, Max, false, 340.f, Spec.Building);
			++Placed;
		}
	}

	const int32 CoverCount = Def.Cover;
	for (int32 i = 0; i < CoverCount; ++i)
	{
		const float Size = Rng.Range(70.f, 120.f);
		const FVector2D Min(Rng.Range(WallThickness + 50.f, W - WallThickness - Size - 50.f),
			Rng.Range(WallThickness + 50.f, H - WallThickness - Size - 50.f));
		const FVector2D Max = Min + FVector2D(Size, Size);
		if (FVector2D::Distance((Min + Max) * 0.5f, Center) < 240.f || OverlapsAny(Min, Max, 50.f))
		{
			continue;
		}
		AddBlock(Min, Max, true, 90.f, Spec.Cover[i % Spec.Cover.Num()]);
	}

	for (int32 i = 0; i < 8; ++i)
	{
		const float X = (W / 9.f) * (i + 1);
		AddKit(Spec.Lamp, FVector2D(X, WallThickness + 40.f), FVector2D::ZeroVector, 1.f, 0.f, FLinearColor(0.55f, 0.5f, 0.35f));
		AddKit(Spec.Lamp, FVector2D(X, H - WallThickness - 40.f), FVector2D::ZeroVector, 1.f, 180.f, FLinearColor(0.55f, 0.5f, 0.35f));
	}
}

void AWS2Arena::BuildPromenadeLayout()
{
	// Rheinufer / Warmer Damm: long rows along the water / avenue, open middle.
	const float W = Def.W;
	const float H = Def.H;
	const float WallThickness = 70.f;
	const FKitSpec Spec = SpecFor(Def.Id);
	FWS2Rng Rng(FCrc::StrCrc32(*Def.Id));

	const int32 PerRow = FMath::Max(2, Def.Buildings / 2);
	for (int32 Row = 0; Row < 2; ++Row)
	{
		for (int32 Col = 0; Col < PerRow; ++Col)
		{
			const float Bw = Rng.Range(140.f, 240.f);
			const float Bh = Rng.Range(120.f, 200.f);
			const float X = WallThickness + 80.f + Col * ((W - 2.f * WallThickness - 160.f) / FMath::Max(1, PerRow - 1));
			const float Y = Row == 0 ? WallThickness + 60.f : H - WallThickness - 60.f - Bh;
			const FVector2D Min(FMath::Clamp(X, WallThickness + 40.f, W - WallThickness - Bw - 40.f), Y);
			AddBlock(Min, Min + FVector2D(Bw, Bh), false, 280.f, Spec.Building);
		}
	}

	for (int32 i = 0; i < Def.Cover; ++i)
	{
		const float Size = Rng.Range(60.f, 110.f);
		const FVector2D Min(Rng.Range(WallThickness + 40.f, W - WallThickness - Size - 40.f),
			Rng.Range(WallThickness + 40.f, H - WallThickness - Size - 40.f));
		const FVector2D Max = Min + FVector2D(Size, Size);
		if (FVector2D::Distance((Min + Max) * 0.5f, GetCenter()) < 300.f || OverlapsAny(Min, Max, 50.f))
		{
			continue;
		}
		AddBlock(Min, Max, true, 85.f, Spec.Cover[i % Spec.Cover.Num()]);
	}

	for (int32 i = 0; i < 6; ++i)
	{
		const float X = (W / 7.f) * (i + 1);
		AddKit(Spec.Lamp, FVector2D(X, WallThickness + 36.f), FVector2D::ZeroVector, 1.f, 0.f, FLinearColor(0.55f, 0.5f, 0.35f));
		AddKit(Spec.Lamp, FVector2D(X, H - WallThickness - 36.f), FVector2D::ZeroVector, 1.f, 180.f, FLinearColor(0.55f, 0.5f, 0.35f));
	}
}

void AWS2Arena::BuildRoomLayout()
{
	// Forschungslabor: a grid of chambers separated by wall segments with doors.
	const float W = Def.W;
	const float H = Def.H;
	const float WallThickness = 70.f;
	const FKitSpec Spec = SpecFor(Def.Id);
	const FVector2D Center = GetCenter();

	const int32 Cells = 3;
	for (int32 Col = 1; Col < Cells; ++Col)
	{
		const float X = W * Col / Cells;
		for (int32 Row = 0; Row < 3; ++Row)
		{
			const float Y0 = WallThickness + Row * (H - 2.f * WallThickness) / 3.f;
			const float Y1 = Y0 + (H - 2.f * WallThickness) / 3.f;
			const float Gap = 150.f;
			AddBlock(FVector2D(X - 20.f, Y0 + Gap * 0.5f), FVector2D(X + 20.f, Y1 - Gap * 0.5f), false, 300.f, Spec.Perimeter);
		}
	}
	for (int32 Row = 1; Row < 3; ++Row)
	{
		const float Y = H * Row / 3.f;
		for (int32 Col = 0; Col < 3; ++Col)
		{
			const float X0 = WallThickness + Col * (W - 2.f * WallThickness) / 3.f;
			const float X1 = X0 + (W - 2.f * WallThickness) / 3.f;
			const float Gap = 170.f;
			AddBlock(FVector2D(X0 + Gap * 0.5f, Y - 18.f), FVector2D(X1 - Gap * 0.5f, Y + 18.f), false, 280.f, Spec.Perimeter);
		}
	}

	const int32 Tanks = FMath::Max(3, Def.Buildings / 3);
	for (int32 i = 0; i < Tanks; ++i)
	{
		const float Size = 140.f;
		const FVector2D Min(WallThickness + 100.f + (i % 3) * 320.f, WallThickness + 100.f + (i / 3) * 360.f);
		const FVector2D Max = Min + FVector2D(Size, Size);
		if (FVector2D::Distance((Min + Max) * 0.5f, Center) < 220.f || OverlapsAny(Min, Max, 50.f))
		{
			continue;
		}
		AddBlock(Min, Max, false, 280.f, Spec.Building);
	}

	const int32 Covers = FMath::Max(4, Def.Cover / 2);
	for (int32 i = 0; i < Covers; ++i)
	{
		const float Size = 80.f;
		const FVector2D Min(WallThickness + 130.f + (i % 4) * 300.f, H * 0.58f + (i / 4) * 160.f);
		const FVector2D Max = Min + FVector2D(Size, Size);
		if (OverlapsAny(Min, Max, 40.f))
		{
			continue;
		}
		AddBlock(Min, Max, true, 90.f, Spec.Cover[i % Spec.Cover.Num()]);
	}

	for (int32 i = 0; i < 8; ++i)
	{
		const float X = (W / 9.f) * (i + 1);
		AddKit(Spec.Lamp, FVector2D(X, WallThickness + 40.f), FVector2D::ZeroVector, 1.f, 0.f, FLinearColor(0.55f, 0.5f, 0.35f));
	}
}

void AWS2Arena::BuildBossLayout()
{
	// Neroberg: the Monopteros dominates the centre, only a handful of pines and
	// rails around the rim.  The boss has room to move, the player has landmarks.
	const float W = Def.W;
	const float H = Def.H;
	const float WallThickness = 70.f;
	const FKitSpec Spec = SpecFor(Def.Id);
	FWS2Rng Rng(FCrc::StrCrc32(*Def.Id));

	// Landmark off the spawn point: a solid base on the exact centre would hide
	// the player and the first enemy ring behind its top face.
	AddKit(TEXT("Monopteros"), GetCenter() + FVector2D(0.f, -430.f), FVector2D(420.f, 420.f), 1.f, 0.f,
		FLinearColor(0.7f, 0.6f, 0.55f), 480.f);

	for (int32 i = 0; i < FMath::Max(2, Def.Buildings); ++i)
	{
		const float Bw = Rng.Range(150.f, 230.f);
		const float Bh = Rng.Range(150.f, 230.f);
		const FVector2D Min(Rng.Range(WallThickness + 40.f, W - WallThickness - Bw - 40.f),
			Rng.Range(WallThickness + 40.f, H - WallThickness - Bh - 40.f));
		const FVector2D Max = Min + FVector2D(Bw, Bh);
		if (FVector2D::Distance((Min + Max) * 0.5f, GetCenter()) < 520.f)
		{
			continue;
		}
		AddBlock(Min, Max, false, 240.f, Spec.Perimeter);
	}

	for (int32 i = 0; i < Def.Cover; ++i)
	{
		const float Size = Rng.Range(80.f, 130.f);
		const FVector2D Min(Rng.Range(WallThickness + 40.f, W - WallThickness - Size - 40.f),
			Rng.Range(WallThickness + 40.f, H - WallThickness - Size - 40.f));
		const FVector2D Max = Min + FVector2D(Size, Size);
		if (FVector2D::Distance((Min + Max) * 0.5f, GetCenter()) < 360.f || OverlapsAny(Min, Max, 60.f))
		{
			continue;
		}
		AddBlock(Min, Max, true, 100.f, Spec.Cover[i % Spec.Cover.Num()]);
	}

	for (int32 i = 0; i < 10; ++i)
	{
		const float Angle = i / 10.f * 2.f * PI;
		const FVector2D Pos = GetCenter() + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 520.f;
		AddKit(Spec.Lamp, Pos, FVector2D::ZeroVector, 1.f, 0.f, FLinearColor(0.9f, 0.2f, 0.55f));
	}
}

void AWS2Arena::AddArenaMods()
{
	FWS2Rng Rng(FCrc::StrCrc32(*(Def.Id + TEXT("_mods"))));
	const float W = Def.W;
	const float H = Def.H;

	for (int32 i = 0; i < Def.Poison; ++i)
	{
		FPoisonPool Pool;
		Pool.Radius = Rng.Range(130.f, 210.f);
		Pool.Center = FVector2D(Rng.Range(Pool.Radius + 120.f, W - Pool.Radius - 120.f),
			Rng.Range(Pool.Radius + 120.f, H - Pool.Radius - 120.f));
		Pool.Dps = 3.f + i * 0.8f;
		PoisonPools.Add(Pool);
		HazardMeshes.Add(AddDisc(Pool.Center, Pool.Radius, FLinearColor(0.02f, 0.18f, 0.055f), -2.f));
	}

	for (int32 i = 0; i < Def.SpeedField; ++i)
	{
		FSpeedField Field;
		Field.Radius = Rng.Range(110.f, 170.f);
		Field.Center = FVector2D(Rng.Range(Field.Radius + 120.f, W - Field.Radius - 120.f),
			Rng.Range(Field.Radius + 120.f, H - Field.Radius - 120.f));
		Field.Multiplier = 1.25f + 0.08f * i;
		SpeedFields.Add(Field);
		HazardMeshes.Add(AddDisc(Field.Center, Field.Radius, FLinearColor(0.015f, 0.075f, 0.16f), -1.f));
	}

	for (int32 i = 0; i < Def.MovingWalls; ++i)
	{
		const float Bw = 250.f;
		const float Bh = 46.f;
		FVector2D Center = GetCenter() + FVector2D(0.f, (i - (Def.MovingWalls - 1) * 0.5f) * 150.f);
		bool bPlaced = false;
		// The moving wall is a real arena mod: search a ring around the centre for
		// a lane that does not sit on top of an existing building.
		for (int32 Try = 0; Try < 24 && !bPlaced; ++Try)
		{
			const float Angle = Try / 24.f * 2.f * PI;
			const float Radius = Try < 8 ? 0.f : 120.f + (Try / 8) * 110.f;
			const FVector2D Candidate = GetCenter() + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius
				+ FVector2D(0.f, (i - (Def.MovingWalls - 1) * 0.5f) * 90.f);
			const FVector2D CandidateMin = Candidate - FVector2D(Bw, Bh) * 0.5f;
			const FVector2D CandidateMax = Candidate + FVector2D(Bw, Bh) * 0.5f;
			if (!OverlapsAny(CandidateMin, CandidateMax, 40.f))
			{
				Center = Candidate;
				bPlaced = true;
			}
		}
		const FVector2D Min = Center - FVector2D(Bw, Bh) * 0.5f;
		const FVector2D Max = Center + FVector2D(Bw, Bh) * 0.5f;
		AddBlock(Min, Max, false, 220.f, SpecFor(Def.Id).Building);
		FMovingWall Wall;
		Wall.BlockIndex = Blocks.Num() - 1;
		Wall.BaseMin = Min;
		Wall.BaseMax = Max;
		Wall.Axis = (i % 2 == 0) ? FVector2D(1.f, 0.f) : FVector2D(0.f, 1.f);
		Wall.Amplitude = 120.f + i * 20.f;
		Wall.Speed = 0.45f + i * 0.12f;
		Wall.Phase = i * 1.7f;
		MovingWalls.Add(Wall);
		Blocks[Wall.BlockIndex].MovingWall = MovingWalls.Num() - 1;
	}
}

void AWS2Arena::AddBlock(const FVector2D& Min, const FVector2D& Max, bool bCover, float Height, const FString& Piece)
{
	FBlock Block;
	Block.Min = Min;
	Block.Max = Max;
	Block.bCover = bCover;
	Blocks.Add(Block);

	const FVector2D Size = Max - Min;
	const FVector2D Center = (Min + Max) * 0.5f;
	UStaticMeshComponent* Comp = AddKit(Piece, Center, Size, 1.f, 0.f, PieceTint(Piece), Height);
	Blocks.Last().Visual = Comp;
}

UStaticMesh* AWS2Arena::LoadKitMesh(const FString& Piece)
{
	const FString Key = Piece;
	if (UStaticMesh** Found = KitMeshes.Find(Key))
	{
		return *Found;
	}

	const FString Path = FString::Printf(TEXT("/Game/Art/Arena/%s/SM_%s_%s.SM_%s_%s"),
		*KitName, *KitName, *Piece, *KitName, *Piece);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	KitMeshes.Add(Key, Mesh);
	if (!Mesh)
	{
		UE_LOG(LogWS2Arena, Warning, TEXT("missing kit piece %s/%s (%s)"), *KitName, *Piece, *Path);
	}
	return Mesh;
}

UStaticMeshComponent* AWS2Arena::AddKit(const FString& Piece, const FVector2D& Center, const FVector2D& Size,
	float UniformScale, float Yaw, const FLinearColor& Tint, float TargetHeight)
{
	UStaticMesh* Mesh = LoadKitMesh(Piece);
	if (!Mesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	AddInstanceComponent(Comp);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(Mesh);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);

	const FVector Extent = Mesh->GetBounds().BoxExtent * 2.f;
	FVector Scale(UniformScale);
	if (Size.X > 0.f && Size.Y > 0.f)
	{
		const float ScaleX = Size.X / FMath::Max(1.f, Extent.X);
		const float ScaleY = Size.Y / FMath::Max(1.f, Extent.Y);
		// Height is authored per block: never let the raw FBX height punch through
		// the camera or turn a facade into a skyscraper.
		const float ScaleZ = TargetHeight > 0.f ? TargetHeight / FMath::Max(1.f, Extent.Z)
			: FMath::Min(ScaleX, ScaleY);
		Scale = FVector(ScaleX, ScaleY, ScaleZ);
	}
	else if (TargetHeight > 0.f)
	{
		const float Uniform = TargetHeight / FMath::Max(1.f, Extent.Z);
		Scale = FVector(Uniform);
	}

	Comp->SetRelativeScale3D(Scale);
	Comp->SetRelativeLocation(FVector(Center.X - Def.W * 0.5f, Center.Y - Def.H * 0.5f, 0.f));
	Comp->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));

	if (UMaterialInterface* BaseMat = LoadColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, Comp);
		FLinearColor Use = PieceTint(Piece);
		if (Piece.Contains(TEXT("Lamp"))) Use = HexToColor(Def.Accent, Use);
		Use.A = 1.f;
		Mid->SetVectorParameterValue(TEXT("Color"), Use);
		const int32 NumSlots = Comp->GetStaticMesh() ? Comp->GetStaticMesh()->GetStaticMaterials().Num() : 0;
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			Comp->SetMaterial(Slot, Mid);
		}
	}

	KitCounts.FindOrAdd(Piece)++;
	BlockMeshes.Add(Comp);
	return Comp;
}

FLinearColor AWS2Arena::PieceTint(const FString& Piece) const
{
	const FLinearColor FloorTint = HexToColor(Def.Ground, FLinearColor(0.1f, 0.15f, 0.12f));
	const FLinearColor Grid = HexToColor(Def.Grid, FLinearColor(0.18f, 0.2f, 0.24f));
	const FLinearColor Accent = HexToColor(Def.Accent, FLinearColor(0.8f, 0.8f, 0.4f));
	if (Piece.Contains(TEXT("Lamp"))) return Accent;
	if (Piece.Contains(TEXT("Pine")) || Piece.Contains(TEXT("Hedge")) || Piece.Contains(TEXT("Palm")) ||
		Piece.Contains(TEXT("Planter"))) return FloorTint * 0.45f + FLinearColor(0.f, 0.03f, 0.008f);
	if (Piece.Contains(TEXT("Pavilion")) || Piece.Contains(TEXT("Bandstand")) || Piece.Contains(TEXT("Monopteros")))
		return Grid * 1.65f + Accent * 0.08f;
	if (Piece.Contains(TEXT("Facade")) || Piece.Contains(TEXT("Hall")) || Piece.Contains(TEXT("Arcade")) ||
		Piece.Contains(TEXT("Kiosk")) || Piece.Contains(TEXT("LabWall")) || Piece.Contains(TEXT("Boiler")))
		return Grid * 1.4f;
	if (Piece.Contains(TEXT("Tank")) || Piece.Contains(TEXT("Pipe"))) return Grid * 1.15f;
	if (Piece.Contains(TEXT("Plaza")) || Piece.Contains(TEXT("Ponton"))) return Grid * 0.85f;
	if (Piece.Contains(TEXT("Tree")) || Piece.Contains(TEXT("Fountain")) || Piece.Contains(TEXT("Pond")) ||
		Piece.Contains(TEXT("Bench")) || Piece.Contains(TEXT("Crate")) || Piece.Contains(TEXT("Hedge")))
		return Grid * 0.78f;
	return Grid * 1.05f + FloorTint * 0.18f;
}

UStaticMeshComponent* AWS2Arena::AddDisc(const FVector2D& Center, float Radius, const FLinearColor& Color, float Z)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetupAttachment(Root);
	Comp->RegisterComponent();
	AddInstanceComponent(Comp);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCastShadow(false);
	const float ScaleXY = Radius * 2.f / 100.f;
	Comp->SetRelativeScale3D(FVector(ScaleXY, ScaleXY, 0.02f));
	Comp->SetRelativeLocation(FVector(Center.X - Def.W * 0.5f, Center.Y - Def.H * 0.5f, Z));
	if (UMaterialInterface* BaseMat = LoadColorMaterial())
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, Comp);
		Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor(Color.R, Color.G, Color.B, 0.55f));
		Comp->SetMaterial(0, Mid);
	}
	return Comp;
}

bool AWS2Arena::OverlapsAny(const FVector2D& Min, const FVector2D& Max, float Padding) const
{
	for (const FBlock& Block : Blocks)
	{
		if (Min.X < Block.Max.X + Padding && Max.X > Block.Min.X - Padding &&
			Min.Y < Block.Max.Y + Padding && Max.Y > Block.Min.Y - Padding)
		{
			return true;
		}
	}
	return false;
}

FVector2D AWS2Arena::ClampToBounds(const FVector2D& Pos, float Radius) const
{
	const float Margin = 70.f + Radius;
	return FVector2D(
		FMath::Clamp(Pos.X, Margin, Def.W - Margin),
		FMath::Clamp(Pos.Y, Margin, Def.H - Margin));
}

bool AWS2Arena::ResolveCircle(FVector2D& Pos, float Radius) const
{
	bool bMoved = false;
	const FVector2D Clamped = ClampToBounds(Pos, Radius);
	if (!Clamped.Equals(Pos, 0.01f))
	{
		Pos = Clamped;
		bMoved = true;
	}

	for (const FBlock& Block : Blocks)
	{
		if (Pos.X > Block.Min.X - Radius && Pos.X < Block.Max.X + Radius &&
			Pos.Y > Block.Min.Y - Radius && Pos.Y < Block.Max.Y + Radius)
		{
			const float Left = Pos.X - (Block.Min.X - Radius);
			const float Right = (Block.Max.X + Radius) - Pos.X;
			const float Bottom = Pos.Y - (Block.Min.Y - Radius);
			const float Top = (Block.Max.Y + Radius) - Pos.Y;
			const float MinPen = FMath::Min(FMath::Min(Left, Right), FMath::Min(Bottom, Top));

			if (MinPen == Left)
			{
				Pos.X = Block.Min.X - Radius;
			}
			else if (MinPen == Right)
			{
				Pos.X = Block.Max.X + Radius;
			}
			else if (MinPen == Bottom)
			{
				Pos.Y = Block.Min.Y - Radius;
			}
			else
			{
				Pos.Y = Block.Max.Y + Radius;
			}
			bMoved = true;
		}
	}

	return bMoved;
}

FVector2D AWS2Arena::GetRandomOpenPoint(FWS2Rng& Rng, float Radius) const
{
	for (int32 Try = 0; Try < 40; ++Try)
	{
		const FVector2D Candidate(Rng.Range(120.f, Def.W - 120.f), Rng.Range(120.f, Def.H - 120.f));
		FVector2D Resolved = Candidate;
		if (!ResolveCircle(Resolved, Radius) && FVector2D::Distance(Resolved, Candidate) < 1.f)
		{
			return Candidate;
		}
	}
	return GetCenter();
}

float AWS2Arena::GetPoisonDpsAt(const FVector2D& Pos) const
{
	float Dps = 0.f;
	for (const FPoisonPool& Pool : PoisonPools)
	{
		if (FVector2D::Distance(Pos, Pool.Center) <= Pool.Radius)
		{
			Dps = FMath::Max(Dps, Pool.Dps);
		}
	}
	return Dps;
}

float AWS2Arena::GetSpeedMultiplierAt(const FVector2D& Pos) const
{
	float Multiplier = 1.f;
	for (const FSpeedField& Field : SpeedFields)
	{
		if (FVector2D::Distance(Pos, Field.Center) <= Field.Radius)
		{
			Multiplier = FMath::Max(Multiplier, Field.Multiplier);
		}
	}
	return Multiplier;
}

FString AWS2Arena::GetModSummary() const
{
	return FString::Printf(TEXT("poison=%d speedFields=%d movingWalls=%d pools=%d fields=%d walls=%d"),
		Def.Poison, Def.SpeedField, Def.MovingWalls, PoisonPools.Num(), SpeedFields.Num(), MovingWalls.Num());
}

void AWS2Arena::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ElapsedTime += DeltaSeconds;
	if (MovingWalls.Num() > 0)
	{
		UpdateMovingWalls(DeltaSeconds);
	}
}

void AWS2Arena::UpdateMovingWalls(float DeltaSeconds)
{
	for (const FMovingWall& Wall : MovingWalls)
	{
		if (!Blocks.IsValidIndex(Wall.BlockIndex))
		{
			continue;
		}
		FBlock& Block = Blocks[Wall.BlockIndex];
		const FVector2D Offset = Wall.Axis * (FMath::Sin(ElapsedTime * Wall.Speed + Wall.Phase) * Wall.Amplitude);
		Block.Min = Wall.BaseMin + Offset;
		Block.Max = Wall.BaseMax + Offset;
		if (Block.Visual)
		{
			Block.Visual->SetRelativeLocation(FVector(
				(Block.Min.X + Block.Max.X) * 0.5f - Def.W * 0.5f,
				(Block.Min.Y + Block.Max.Y) * 0.5f - Def.H * 0.5f,
				0.f));
		}
	}
}
