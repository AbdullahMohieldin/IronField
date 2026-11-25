#pragma once

#include "CoreMinimal.h"
#include "IFBuildingTypes.generated.h"

class AIFStructure;

UENUM(BlueprintType)
enum class EIFPlacementRule : uint8
{
	FreeOrEndSnap,
	WallWalkwayOnly,
	CornerWalkwayOnly,
	StairMountOrFree,
	GateOpeningOnly
};

UENUM(BlueprintType)
enum class EIFPlacementSnapState : uint8
{
	Free,
	SnappedToWall,
	SnappedToCorner,
	SnappedToWalkway,
	SnappedToGate
};

UENUM(BlueprintType)
enum class EIFConnectorChannel : uint8
{
	None,
	StoneWall,
	WoodWalkway
};

UENUM(BlueprintType)
enum class EIFCollisionRole : uint8
{
	SolidUnwalkable,
	WalkableDeck,
	WalkableRamp
};

UENUM(BlueprintType)
enum class EIFMountSurfaceKind : uint8
{
	StraightWall,
	CornerArc,
	WalkwayOuterEdge
};

USTRUCT(BlueprintType)
struct IRONFIELD_API FIFCollisionBox
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Footprint")
	FVector LocalCenter = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Footprint")
	FVector HalfExtent = FVector(100.f, 100.f, 100.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Footprint")
	FRotator LocalRotation = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Footprint")
	EIFCollisionRole Role = EIFCollisionRole::SolidUnwalkable;
};

USTRUCT(BlueprintType)
struct IRONFIELD_API FIFModularConnector
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	EIFConnectorChannel Channel = EIFConnectorChannel::StoneWall;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	FVector2D LocalPosition = FVector2D::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	FVector2D LocalOutwardNormal = FVector2D(1.f, 0.f);
};

USTRUCT(BlueprintType)
struct IRONFIELD_API FIFLateralMountSurface
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	EIFMountSurfaceKind Kind = EIFMountSurfaceKind::StraightWall;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	FVector2D LocalCenter = FVector2D::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	float LocalTangentYaw = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "10.0"))
	float HalfLength = 120.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "1.0"))
	float HalfThickness = 13.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "0.0"))
	float ArcMidRadius = 240.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "0.0"))
	float InnerWalkwayBackRadius = 218.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement", meta = (ClampMin = "0.0"))
	float OuterWalkwayBackRadius = 253.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	bool bAllowPositiveSide = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "IronField|Structure|Placement")
	bool bAllowNegativeSide = true;
};

struct IRONFIELD_API FIFWorldCollisionBox
{
	FVector WorldCenter = FVector::ZeroVector;
	FVector HalfExtent = FVector::ZeroVector;
	FRotator WorldRotation = FRotator::ZeroRotator;
	float WorldMinZ = 0.f;
	float WorldMaxZ = 0.f;
	EIFCollisionRole Role = EIFCollisionRole::SolidUnwalkable;
};

struct IRONFIELD_API FIFSnapResult
{
	bool bSnapped = false;
	FVector Location = FVector::ZeroVector;
	float YawDegrees = 0.f;
	TWeakObjectPtr<const AIFStructure> HostStructure;
	bool bIsCornerInnerWalkway = false;
	bool bBackedByStoneWall = false;
	EIFPlacementSnapState SnapState = EIFPlacementSnapState::Free;
};
