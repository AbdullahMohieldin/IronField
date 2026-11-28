#include "Castle/IFBuildingSnapSolver.h"

#include "Castle/IFGateDoor.h"
#include "Castle/IFStructure.h"
#include "Castle/IFStructureDataAsset.h"
#include "Castle/IFTerritory.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/SphereComponent.h"
#include "Core/IFGameState.h"
#include "Core/IFGameTypes.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Tactical/IFPlacementGhost.h"

namespace
{
	FVector2D Fwd2D(float YawDeg)
	{
		const float Rad = FMath::DegreesToRadians(YawDeg);
		return FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
	}

	FVector2D Right2D(float YawDeg)
	{
		const float Rad = FMath::DegreesToRadians(YawDeg);
		return FVector2D(-FMath::Sin(Rad), FMath::Cos(Rad));
	}

	FVector2D Rotate2D(const FVector2D& V, float YawDeg)
	{
		const float Rad = FMath::DegreesToRadians(YawDeg);
		const float C = FMath::Cos(Rad);
		const float S = FMath::Sin(Rad);
		return FVector2D(V.X * C - V.Y * S, V.X * S + V.Y * C);
	}

	FVector2D GetEffectiveHorizontalHalfExtent(const FIFWorldCollisionBox& Box)
	{
		const float PitchRad = FMath::DegreesToRadians(Box.WorldRotation.Pitch);
		const float EffX = FMath::Abs(FMath::Cos(PitchRad)) * Box.HalfExtent.X
			+ FMath::Abs(FMath::Sin(PitchRad)) * Box.HalfExtent.Z;
		return FVector2D(EffX, Box.HalfExtent.Y);
	}

	bool DoesOrientedBox2DOverlap(
		const FVector2D& CenterA,
		const FVector2D& HalfA,
		float YawA,
		const FVector2D& CenterB,
		const FVector2D& HalfB,
		float YawB)
	{
		const FVector2D AxisAX = Fwd2D(YawA);
		const FVector2D AxisAY(-AxisAX.Y, AxisAX.X);
		const FVector2D AxisBX = Fwd2D(YawB);
		const FVector2D AxisBY(-AxisBX.Y, AxisBX.X);

		const FVector2D Delta = CenterB - CenterA;
		const FVector2D Axes[4] = { AxisAX, AxisAY, AxisBX, AxisBY };

		for (const FVector2D& Axis : Axes)
		{
			const float Dist = FMath::Abs(FVector2D::DotProduct(Delta, Axis));
			const float ProjA = HalfA.X * FMath::Abs(FVector2D::DotProduct(AxisAX, Axis))
				+ HalfA.Y * FMath::Abs(FVector2D::DotProduct(AxisAY, Axis));
			const float ProjB = HalfB.X * FMath::Abs(FVector2D::DotProduct(AxisBX, Axis))
				+ HalfB.Y * FMath::Abs(FVector2D::DotProduct(AxisBY, Axis));
			if (Dist >= ProjA + ProjB)
			{
				return false;
			}
		}

		return true;
	}

	bool IsPointInsideAnyBox2D(const FVector2D& Point, const TArray<FIFWorldCollisionBox>& Boxes)
	{
		for (const FIFWorldCollisionBox& Box : Boxes)
		{
			const FVector2D Half = GetEffectiveHorizontalHalfExtent(Box);
			if (DoesOrientedBox2DOverlap(Point, FVector2D(4.f, 4.f), 0.f, FVector2D(Box.WorldCenter), Half, Box.WorldRotation.Yaw))
			{
				return true;
			}
		}
		return false;
	}

	void GatherPlayerStructures(
		const UWorld* World,
		const AIFStructure* IgnoredStructure,
		TArray<const AIFStructure*>& OutStructures)
	{
		OutStructures.Reset();
		if (!IsValid(World))
		{
			return;
		}

		for (TActorIterator<AIFStructure> It(World); It; ++It)
		{
			const AIFStructure* const Struct = *It;
			if (!IsValid(Struct)
				|| Struct == IgnoredStructure
				|| !Struct->IsStructureAlive()
				|| Struct->GetGenericTeamId().GetId() != IFTeamIds::Player)
			{
				continue;
			}
			OutStructures.Add(Struct);
		}
	}

	bool TrySnapEndToEndChannel(
		const TArray<const AIFStructure*>& PlayerStructures,
		const UIFStructureDataAsset* Data,
		EIFConnectorChannel Channel,
		const FVector& CursorGroundLocation,
		int32 ConnectorFlipIndex,
		float SnapRadiusCm,
		FIFSnapResult& OutResult)
	{
		TArray<FIFModularConnector> GhostChannelConnectors;
		for (const FIFModularConnector& Conn : Data->Connectors)
		{
			if (Conn.Channel == Channel)
			{
				GhostChannelConnectors.Add(Conn);
			}
		}
		if (GhostChannelConnectors.IsEmpty())
		{
			return false;
		}

		float BestDistSq = FMath::Square(FMath::Max(50.f, SnapRadiusCm));
		const AIFStructure* BestTargetStruct = nullptr;
		FIFModularConnector BestTargetConn;

		for (const AIFStructure* const TargetStruct : PlayerStructures)
		{
			TArray<FIFModularConnector> TargetConnectors;
			TargetStruct->GetWorldConnectors(TargetConnectors);

			for (const FIFModularConnector& TargetConn : TargetConnectors)
			{
				if (TargetConn.Channel != Channel)
				{
					continue;
				}

				const float DistSq = FVector2D::DistSquared(FVector2D(CursorGroundLocation), TargetConn.LocalPosition);
				if (DistSq >= BestDistSq)
				{
					continue;
				}

				// Skip connectors whose outward space is already occupied by another structure on this channel.
				const FVector2D ProbePos = TargetConn.LocalPosition + TargetConn.LocalOutwardNormal * 35.f;
				bool bOccupied = false;
				for (const AIFStructure* const OtherStruct : PlayerStructures)
				{
					if (OtherStruct == TargetStruct)
					{
						continue;
					}
					const UIFStructureDataAsset* const OtherDA = OtherStruct->GetStructureData();
					if (!IsValid(OtherDA))
					{
						continue;
					}
					const bool bSharesChannel = OtherDA->Connectors.ContainsByPredicate(
						[Channel](const FIFModularConnector& C) { return C.Channel == Channel; });
					if (!bSharesChannel)
					{
						continue;
					}

					TArray<FIFWorldCollisionBox> OtherBoxes;
					OtherStruct->GetWorldCollisionBoxes(OtherBoxes);
					if (IsPointInsideAnyBox2D(ProbePos, OtherBoxes))
					{
						bOccupied = true;
						break;
					}
				}

				if (bOccupied)
				{
					continue;
				}

				BestDistSq = DistSq;
				BestTargetStruct = TargetStruct;
				BestTargetConn = TargetConn;
			}
		}

		if (!IsValid(BestTargetStruct))
		{
			return false;
		}

		const int32 SafeIdx = ((ConnectorFlipIndex % GhostChannelConnectors.Num()) + GhostChannelConnectors.Num()) % GhostChannelConnectors.Num();
		const FIFModularConnector& GhostConn = GhostChannelConnectors[SafeIdx];

		const float TargetAngleRad = FMath::Atan2(-BestTargetConn.LocalOutwardNormal.Y, -BestTargetConn.LocalOutwardNormal.X);
		const float GhostAngleRad = FMath::Atan2(GhostConn.LocalOutwardNormal.Y, GhostConn.LocalOutwardNormal.X);
		const float RequiredYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(TargetAngleRad - GhostAngleRad));

		const FVector2D RotatedLocal = Rotate2D(GhostConn.LocalPosition, RequiredYaw);
		OutResult.bSnapped = true;
		OutResult.Location = FVector(
			BestTargetConn.LocalPosition.X - RotatedLocal.X,
			BestTargetConn.LocalPosition.Y - RotatedLocal.Y,
			CursorGroundLocation.Z);
		OutResult.YawDegrees = RequiredYaw;
		OutResult.HostStructure = BestTargetStruct;
		return true;
	}
}

bool FIFBuildingSnapSolver::DoesWorldBoxOverlap(
	const FIFWorldCollisionBox& A,
	const FIFWorldCollisionBox& B,
	float XYInsetCm,
	float ZToleranceCm)
{
	if (A.WorldMaxZ <= B.WorldMinZ + ZToleranceCm || B.WorldMaxZ <= A.WorldMinZ + ZToleranceCm)
	{
		return false;
	}

	const FVector2D EffA = GetEffectiveHorizontalHalfExtent(A);
	const FVector2D EffB = GetEffectiveHorizontalHalfExtent(B);

	const FVector2D HalfA(FMath::Max(4.f, EffA.X - XYInsetCm), FMath::Max(4.f, EffA.Y - XYInsetCm));
	const FVector2D HalfB(FMath::Max(4.f, EffB.X - XYInsetCm), FMath::Max(4.f, EffB.Y - XYInsetCm));

	return DoesOrientedBox2DOverlap(
		FVector2D(A.WorldCenter),
		HalfA,
		A.WorldRotation.Yaw,
		FVector2D(B.WorldCenter),
		HalfB,
		B.WorldRotation.Yaw);
}

bool FIFBuildingSnapSolver::HasStoneWallBacking(
	const UWorld* World,
	const FVector& WalkwayLocation,
	float WalkwayYaw,
	const AIFStructure* IgnoredStructure,
	bool& OutIsCornerInnerWalkway,
	const AIFStructure** OutBackingHost)
{
	OutIsCornerInnerWalkway = false;
	if (OutBackingHost)
	{
		*OutBackingHost = nullptr;
	}
	TArray<const AIFStructure*> PlayerStructures;
	GatherPlayerStructures(World, IgnoredStructure, PlayerStructures);

	const FVector2D WalkwayPos2D(WalkwayLocation);
	const FVector2D WalkwayFwd = Fwd2D(WalkwayYaw);
	const FVector2D WalkwayRight = Right2D(WalkwayYaw);

	for (const AIFStructure* const Host : PlayerStructures)
	{
		const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
		if (!IsValid(HostDA))
		{
			continue;
		}

		const FVector2D HostLoc2D(Host->GetActorLocation());
		const float HostYaw = Host->GetActorRotation().Yaw;

		for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
		{
			if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
			{
				const FVector2D Anchor = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
				const float SurfYaw = FRotator::NormalizeAxis(HostYaw + Surf.LocalTangentYaw);
				const FVector2D SurfFwd = Fwd2D(SurfYaw);
				const FVector2D SurfRight = Right2D(SurfYaw);

				// Walkway must run parallel to this wall segment.
				if (FMath::Abs(FVector2D::DotProduct(WalkwayFwd, SurfFwd)) < 0.96f)
				{
					continue;
				}

				const FVector2D Delta = WalkwayPos2D - Anchor;
				const float AlongX = FMath::Abs(FVector2D::DotProduct(Delta, SurfFwd));
				if (AlongX > Surf.HalfLength + 45.f)
				{
					continue;
				}

				const float SignedPerp = FVector2D::DotProduct(Delta, SurfRight);
				const float ExpectedDist = 62.5f + Surf.HalfThickness;
				if (FMath::Abs(FMath::Abs(SignedPerp) - ExpectedDist) > 16.f)
				{
					continue;
				}

				// Corbel (-Y = -WalkwayRight) must face toward the wall anchor.
				const FVector2D ToWall = (Anchor - WalkwayPos2D).GetSafeNormal();
				if (FVector2D::DotProduct(-WalkwayRight, ToWall) > 0.85f)
				{
					if (OutBackingHost)
					{
						*OutBackingHost = Host;
					}
					return true;
				}
			}
			else if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
			{
				const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
				const FVector2D MidRadial = Rotate2D(FVector2D(0.70710678f, -0.70710678f), HostYaw);

				const FVector2D InnerCenter = WorldArcCenter + MidRadial * (Surf.InnerWalkwayBackRadius - 62.5f);
				const float InnerYaw = FRotator::NormalizeAxis(HostYaw + 45.f);
				if (FVector2D::DistSquared(WalkwayPos2D, InnerCenter) <= FMath::Square(25.f)
					&& FMath::Abs(FRotator::NormalizeAxis(WalkwayYaw - InnerYaw)) < 10.f)
				{
					OutIsCornerInnerWalkway = true;
					if (OutBackingHost)
					{
						*OutBackingHost = Host;
					}
					return true;
				}

				const FVector2D OuterCenter = WorldArcCenter + MidRadial * (Surf.OuterWalkwayBackRadius + 62.5f);
				const float OuterYaw = FRotator::NormalizeAxis(HostYaw - 135.f);
				if (FVector2D::DistSquared(WalkwayPos2D, OuterCenter) <= FMath::Square(25.f)
					&& FMath::Abs(FRotator::NormalizeAxis(WalkwayYaw - OuterYaw)) < 10.f)
				{
					if (OutBackingHost)
					{
						*OutBackingHost = Host;
					}
					return true;
				}
			}
		}
	}

	return false;
}

FIFSnapResult FIFBuildingSnapSolver::SolvePlacementTransform(
	const UWorld* World,
	const UIFStructureDataAsset* Data,
	const FVector& CursorGroundLocation,
	int32 FreeYawIndex,
	int32 ConnectorFlipIndex,
	int32 StairMountMode,
	float SnapRadiusCm,
	float MountSnapRadiusCm,
	const AIFStructure* IgnoredStructure)
{
	FIFSnapResult Result;
	const float FreeYaw = FRotator::NormalizeAxis(static_cast<float>((FreeYawIndex % 4 + 4) % 4) * 90.f);
	Result.Location = CursorGroundLocation;
	Result.YawDegrees = FreeYaw;

	if (!IsValid(World) || !IsValid(Data))
	{
		return Result;
	}

	TArray<const AIFStructure*> PlayerStructures;
	GatherPlayerStructures(World, IgnoredStructure, PlayerStructures);

	// 1. FreeOrEndSnap: Wall, Wall Corner, Gate, Guard Tower, Keep.
	if (Data->PlacementRule == EIFPlacementRule::FreeOrEndSnap)
	{
		if (!Data->Connectors.IsEmpty())
		{
			FIFSnapResult SnapRes;
			if (TrySnapEndToEndChannel(
				PlayerStructures,
				Data,
				Data->Connectors[0].Channel,
				CursorGroundLocation,
				ConnectorFlipIndex,
				SnapRadiusCm,
				SnapRes))
			{
				const UIFStructureDataAsset* const HostDA = SnapRes.HostStructure.IsValid() ? SnapRes.HostStructure->GetStructureData() : nullptr;
				const bool bHostIsCorner = IsValid(HostDA) && HostDA->MountSurfaces.ContainsByPredicate(
					[](const FIFLateralMountSurface& S) { return S.Kind == EIFMountSurfaceKind::CornerArc; });
				SnapRes.SnapState = bHostIsCorner ? EIFPlacementSnapState::SnappedToCorner : EIFPlacementSnapState::SnappedToWall;
				return SnapRes;
			}
		}
		return Result;
	}

	// 1b. CornerWalkwayOnly: Curved Corner Walkway can ONLY attach onto an existing Wall Corner.
	if (Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly)
	{
		const FVector2D Cursor2D(CursorGroundLocation);
		float BestDistSq = FMath::Square(FMath::Max(100.f, MountSnapRadiusCm));
		FIFSnapResult BestCornerSnap;

		for (const AIFStructure* const Host : PlayerStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA))
			{
				continue;
			}

			const FVector2D HostLoc2D(Host->GetActorLocation());
			const float HostYaw = Host->GetActorRotation().Yaw;

			for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
			{
				if (Surf.Kind != EIFMountSurfaceKind::CornerArc)
				{
					continue;
				}

				const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
				const FVector2D MidRadial = Rotate2D(FVector2D(0.70710678f, -0.70710678f), HostYaw);
				const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;

				const float DistSq = FMath::Min(
					FVector2D::DistSquared(Cursor2D, ArcMidPoint),
					FVector2D::DistSquared(Cursor2D, WorldArcCenter + MidRadial * (Surf.ArcMidRadius * 0.55f)));
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestCornerSnap.bSnapped = true;
					BestCornerSnap.Location = FVector(WorldArcCenter.X, WorldArcCenter.Y, CursorGroundLocation.Z);
					BestCornerSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw + 135.f);
					BestCornerSnap.HostStructure = Host;
					BestCornerSnap.bIsCornerInnerWalkway = true;
					BestCornerSnap.bBackedByStoneWall = true;
					BestCornerSnap.SnapState = EIFPlacementSnapState::SnappedToCorner;
				}
			}
		}

		if (BestCornerSnap.bSnapped)
		{
			return BestCornerSnap;
		}
		return Result;
	}

	// 1c. GateOpeningOnly: Double-leaf Gate Door snaps centered inside an existing Gate archway (bSupportsGateDoor).
	if (Data->PlacementRule == EIFPlacementRule::GateOpeningOnly)
	{
		const FVector2D Cursor2D(CursorGroundLocation);
		float BestDistSq = FMath::Square(FMath::Max(120.f, MountSnapRadiusCm));
		FIFSnapResult BestGateSnap;

		for (const AIFStructure* const Host : PlayerStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA) || !HostDA->bSupportsGateDoor)
			{
				continue;
			}

			const FVector HostLoc = Host->GetActorLocation();
			const float DistSq = FVector2D::DistSquared(Cursor2D, FVector2D(HostLoc));
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestGateSnap.bSnapped = true;
				BestGateSnap.Location = FVector(HostLoc.X, HostLoc.Y, CursorGroundLocation.Z);
				BestGateSnap.YawDegrees = Host->GetActorRotation().Yaw;
				BestGateSnap.HostStructure = Host;
				BestGateSnap.bBackedByStoneWall = true;
				BestGateSnap.SnapState = EIFPlacementSnapState::SnappedToGate;
			}
		}

		if (BestGateSnap.bSnapped)
		{
			return BestGateSnap;
		}
		return Result;
	}

	// 2. WallWalkwayOnly: Wooden Walkway must attach to a Wall/Gate/WallCorner (or chain end-to-end while still backed by a stone wall).
	if (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly)
	{
		const FVector2D Cursor2D(CursorGroundLocation);
		float BestDistSq = FMath::Square(FMath::Max(100.f, MountSnapRadiusCm));
		FIFSnapResult BestWallSnap;

		for (const AIFStructure* const Host : PlayerStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA))
			{
				continue;
			}

			const FVector2D HostLoc2D(Host->GetActorLocation());
			const float HostYaw = Host->GetActorRotation().Yaw;

			for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
			{
				if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
				{
					const FVector2D Anchor = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
					const float SurfYaw = FRotator::NormalizeAxis(HostYaw + Surf.LocalTangentYaw);
					const FVector2D SurfFwd = Fwd2D(SurfYaw);
					const FVector2D SurfRight = Right2D(SurfYaw);

					const FVector2D Delta = Cursor2D - Anchor;
					if (FMath::Abs(FVector2D::DotProduct(Delta, SurfFwd)) > Surf.HalfLength + 90.f)
					{
						continue;
					}

					const float SideSign = (FVector2D::DotProduct(Delta, SurfRight) >= 0.f) ? 1.f : -1.f;
					if ((SideSign > 0.f && !Surf.bAllowPositiveSide) || (SideSign < 0.f && !Surf.bAllowNegativeSide))
					{
						continue;
					}

					const FVector2D CandPos2D = Anchor + SurfRight * (SideSign * (Surf.HalfThickness + Data->AttachmentDepthHalfExtent));
					const float DistSq = FVector2D::DistSquared(Cursor2D, CandPos2D);
					if (DistSq < BestDistSq)
					{
						BestDistSq = DistSq;
						BestWallSnap.bSnapped = true;
						BestWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, CursorGroundLocation.Z);
						// -Y corbels must face the wall on both sides.
						BestWallSnap.YawDegrees = (SideSign > 0.f) ? SurfYaw : FRotator::NormalizeAxis(SurfYaw + 180.f);
						BestWallSnap.HostStructure = Host;
						BestWallSnap.bIsCornerInnerWalkway = false;
						BestWallSnap.bBackedByStoneWall = true;
						BestWallSnap.SnapState = EIFPlacementSnapState::SnappedToWall;
					}
				}
				else if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
				{
					const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
					const FVector2D MidRadial = Rotate2D(FVector2D(0.70710678f, -0.70710678f), HostYaw);
					const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;

					const float DistToCornerSq = FVector2D::DistSquared(Cursor2D, ArcMidPoint);
					if (DistToCornerSq >= BestDistSq)
					{
						continue;
					}

					const float CursorRadius = FVector2D::Distance(Cursor2D, WorldArcCenter);
					const bool bInnerSide = (CursorRadius < Surf.ArcMidRadius);
					if ((bInnerSide && !Surf.bAllowPositiveSide) || (!bInnerSide && !Surf.bAllowNegativeSide))
					{
						continue;
					}

					BestDistSq = DistToCornerSq;
					BestWallSnap.bSnapped = true;
					if (bInnerSide)
					{
						const FVector2D CandPos2D = WorldArcCenter + MidRadial * (Surf.InnerWalkwayBackRadius - Data->AttachmentDepthHalfExtent);
						BestWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, CursorGroundLocation.Z);
						BestWallSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw + 45.f);
						BestWallSnap.bIsCornerInnerWalkway = true;
					}
					else
					{
						const FVector2D CandPos2D = WorldArcCenter + MidRadial * (Surf.OuterWalkwayBackRadius + Data->AttachmentDepthHalfExtent);
						BestWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, CursorGroundLocation.Z);
						BestWallSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw - 135.f);
						BestWallSnap.bIsCornerInnerWalkway = false;
					}
					BestWallSnap.HostStructure = Host;
					BestWallSnap.bBackedByStoneWall = true;
					BestWallSnap.SnapState = EIFPlacementSnapState::SnappedToCorner;
				}
			}
		}

		// Also try end-to-end WoodWalkway chaining, but ONLY accept if a stone wall backs the new position.
		FIFSnapResult ChainSnap;
		if (TrySnapEndToEndChannel(
			PlayerStructures,
			Data,
			EIFConnectorChannel::WoodWalkway,
			CursorGroundLocation,
			0,
			SnapRadiusCm,
			ChainSnap))
		{
			bool bChainCornerInner = false;
			const AIFStructure* BackingHost = nullptr;
			if (HasStoneWallBacking(World, ChainSnap.Location, ChainSnap.YawDegrees, IgnoredStructure, bChainCornerInner, &BackingHost))
			{
				ChainSnap.bIsCornerInnerWalkway = bChainCornerInner;
				ChainSnap.bBackedByStoneWall = true;
				ChainSnap.HostStructure = IsValid(BackingHost) ? BackingHost : ChainSnap.HostStructure.Get();
				ChainSnap.SnapState = bChainCornerInner ? EIFPlacementSnapState::SnappedToCorner : EIFPlacementSnapState::SnappedToWalkway;
				const float ChainDistSq = FVector2D::DistSquared(Cursor2D, FVector2D(ChainSnap.Location));
				if (!BestWallSnap.bSnapped || ChainDistSq < BestDistSq)
				{
					return ChainSnap;
				}
			}
		}

		if (BestWallSnap.bSnapped)
		{
			return BestWallSnap;
		}
		return Result;
	}

	// 3. StairMountOrFree: Wall Stairs snap to WalkwayOuterEdge / StraightWall / CornerArc on cursor's side, or free-place in 90 deg steps.
	if (Data->PlacementRule == EIFPlacementRule::StairMountOrFree)
	{
		const FVector2D Cursor2D(CursorGroundLocation);
		float BestScoreDistSq = FMath::Square(FMath::Max(100.f, MountSnapRadiusCm));
		FIFSnapResult BestStairSnap;
		const int32 Mode = ((StairMountMode % 3) + 3) % 3;

		auto HasWalkwayOnSide = [&PlayerStructures](const FVector2D& WallAnchor, const FVector2D& SideNormal) -> bool
		{
			const FVector2D Probe = WallAnchor + SideNormal * 75.5f;
			for (const AIFStructure* const S : PlayerStructures)
			{
				const UIFStructureDataAsset* const SDA = S->GetStructureData();
				if (IsValid(SDA) && SDA->PlacementRule == EIFPlacementRule::WallWalkwayOnly)
				{
					if (FVector2D::DistSquared(FVector2D(S->GetActorLocation()), Probe) < FMath::Square(55.f))
					{
						return true;
					}
				}
			}
			return false;
		};

		for (const AIFStructure* const Host : PlayerStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA))
			{
				continue;
			}

			const FVector2D HostLoc2D(Host->GetActorLocation());
			const float HostYaw = Host->GetActorRotation().Yaw;

			for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
			{
				FVector2D MountAnchor = FVector2D::ZeroVector;
				FVector2D SideNormal = FVector2D(0.f, 1.f);
				float CursorDistSq = MAX_flt;
				EIFPlacementSnapState CandidateSnapState = EIFPlacementSnapState::SnappedToWall;

				if (Surf.Kind == EIFMountSurfaceKind::WalkwayOuterEdge)
				{
					const FVector2D Center = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
					const float SurfYaw = FRotator::NormalizeAxis(HostYaw + Surf.LocalTangentYaw);
					const FVector2D SurfFwd = Fwd2D(SurfYaw);
					const FVector2D SurfRight = Right2D(SurfYaw);

					const FVector2D Delta = Cursor2D - Center;
					if (FMath::Abs(FVector2D::DotProduct(Delta, SurfFwd)) > Surf.HalfLength + 90.f)
					{
						continue;
					}
					// Stairs can mount on the +Y outer courtyard edge of a walkway.
					if (FVector2D::DotProduct(Delta, SurfRight) < -15.f)
					{
						continue;
					}

					SideNormal = SurfRight;
					MountAnchor = Center + SideNormal * Surf.HalfThickness;
					CursorDistSq = FVector2D::DistSquared(Cursor2D, MountAnchor);
					// Slight priority over raw wall faces when both are nearby.
					CursorDistSq *= 0.85f;
					CandidateSnapState = EIFPlacementSnapState::SnappedToWalkway;
				}
				else if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
				{
					const FVector2D Center = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
					const float SurfYaw = FRotator::NormalizeAxis(HostYaw + Surf.LocalTangentYaw);
					const FVector2D SurfFwd = Fwd2D(SurfYaw);
					const FVector2D SurfRight = Right2D(SurfYaw);

					const FVector2D Delta = Cursor2D - Center;
					if (FMath::Abs(FVector2D::DotProduct(Delta, SurfFwd)) > Surf.HalfLength + 90.f)
					{
						continue;
					}

					const float SideSign = (FVector2D::DotProduct(Delta, SurfRight) >= 0.f) ? 1.f : -1.f;
					SideNormal = SurfRight * SideSign;
					if (HasWalkwayOnSide(Center, SideNormal))
					{
						continue;
					}

					MountAnchor = Center + SideNormal * Surf.HalfThickness;
					CursorDistSq = FVector2D::DistSquared(Cursor2D, MountAnchor);
					CandidateSnapState = EIFPlacementSnapState::SnappedToWall;
				}
				else if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
				{
					const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
					const FVector2D MidRadial = Rotate2D(FVector2D(0.70710678f, -0.70710678f), HostYaw);
					const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;
					CursorDistSq = FVector2D::DistSquared(Cursor2D, ArcMidPoint);
					const bool bInnerSide = (FVector2D::Distance(Cursor2D, WorldArcCenter) < Surf.ArcMidRadius);
					SideNormal = bInnerSide ? -MidRadial : MidRadial;
					MountAnchor = WorldArcCenter + MidRadial * (bInnerSide ? 227.5f : 253.5f);
					CandidateSnapState = EIFPlacementSnapState::SnappedToCorner;
				}
				else
				{
					continue;
				}

				if (CursorDistSq >= BestScoreDistSq)
				{
					continue;
				}

				BestScoreDistSq = CursorDistSq;
				BestStairSnap.bSnapped = true;
				BestStairSnap.HostStructure = Host;
				BestStairSnap.SnapState = CandidateSnapState;

				const FVector2D Tangent(-SideNormal.Y, SideNormal.X);
				FVector2D FinalPos2D = MountAnchor;
				float FinalYaw = FreeYaw;

				if (Mode == 0)
				{
					// Parallel Left-to-Right along Tangent.
					FinalPos2D = MountAnchor + SideNormal * Data->AttachmentDepthHalfExtent - Tangent * Data->StairParallelLandingShift;
					FinalYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X)));
				}
				else if (Mode == 1)
				{
					// Parallel Right-to-Left along -Tangent.
					FinalPos2D = MountAnchor + SideNormal * Data->AttachmentDepthHalfExtent + Tangent * Data->StairParallelLandingShift;
					FinalYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(-Tangent.Y, -Tangent.X)));
				}
				else
				{
					// Perpendicular outward: +X top landing flush against host, -X ramp extending into courtyard.
					FinalPos2D = MountAnchor + SideNormal * Data->StairHalfLength;
					FinalYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(-SideNormal.Y, -SideNormal.X)));
				}

				BestStairSnap.Location = FVector(FinalPos2D.X, FinalPos2D.Y, CursorGroundLocation.Z);
				BestStairSnap.YawDegrees = FinalYaw;
			}
		}

		if (BestStairSnap.bSnapped)
		{
			return BestStairSnap;
		}
	}

	return Result;
}

bool FIFBuildingSnapSolver::ValidatePlacement(
	const UWorld* World,
	const UIFStructureDataAsset* Data,
	const FIFSnapResult& Candidate,
	const AActor* PlayerPawn,
	bool bRequireHeroInTerritory,
	const AIFStructure* RelocatingStructure,
	const AIFPlacementGhost* PlacementGhost,
	AIFTerritory*& OutTerritory,
	EIFPlacementInvalidReason& OutReason)
{
	OutTerritory = nullptr;
	OutReason = EIFPlacementInvalidReason::None;

	const AIFGameState* const GS = IsValid(World) ? World->GetGameState<AIFGameState>() : nullptr;
	if (!IsValid(Data) || !IsValid(World) || !IsValid(GS))
	{
		OutReason = EIFPlacementInvalidReason::InvalidLocation;
		return false;
	}

	// 1. Attachment rule checks (no floating Wooden Walkways or Corner Walkways).
	if (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly && (!Candidate.bSnapped || !Candidate.bBackedByStoneWall))
	{
		OutReason = EIFPlacementInvalidReason::RequiresWallAttachment;
		return false;
	}
	if (Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly && !Candidate.bSnapped)
	{
		OutReason = EIFPlacementInvalidReason::RequiresCornerAttachment;
		return false;
	}
	if (Data->PlacementRule == EIFPlacementRule::GateOpeningOnly)
	{
		if (!Candidate.bSnapped || !Candidate.HostStructure.IsValid())
		{
			OutReason = EIFPlacementInvalidReason::RequiresGateArchway;
			return false;
		}

		const AIFStructure* const HostGate = Candidate.HostStructure.Get();
		for (TActorIterator<AIFGateDoor> DoorIt(World); DoorIt; ++DoorIt)
		{
			const AIFGateDoor* const ExistingDoor = *DoorIt;
			if (!IsValid(ExistingDoor) || ExistingDoor == RelocatingStructure || !ExistingDoor->IsStructureAlive())
			{
				continue;
			}

			if (ExistingDoor->GetHostGate() == HostGate
				|| FVector::DistSquared2D(ExistingDoor->GetActorLocation(), HostGate->GetActorLocation()) < FMath::Square(60.f))
			{
				OutReason = EIFPlacementInvalidReason::GateAlreadyHasDoor;
				return false;
			}
		}
	}

	// 2. Compute candidate 3D collision boxes.
	TArray<FIFWorldCollisionBox> CandBoxes;
	AIFStructure::ComputeWorldCollisionBoxes(Data, Candidate.Location, Candidate.YawDegrees, CandBoxes);
	if (CandBoxes.IsEmpty())
	{
		OutReason = EIFPlacementInvalidReason::InvalidLocation;
		return false;
	}

	// 3. Territory bounds: every candidate box must fit inside the same player-owned Territory.
	AIFTerritory* Territory = nullptr;
	for (const FIFWorldCollisionBox& Box : CandBoxes)
	{
		const FVector2D HorizHalf = GetEffectiveHorizontalHalfExtent(Box);
		AIFTerritory* const BoxTerr = GS->FindTerritoryContainingBox(Box.WorldCenter, HorizHalf, Box.WorldRotation.Yaw);
		if (!IsValid(BoxTerr) || BoxTerr->GetGenericTeamId().GetId() != IFTeamIds::Player)
		{
			OutReason = EIFPlacementInvalidReason::OutsideOwnedTerritory;
			return false;
		}
		if (!IsValid(Territory))
		{
			Territory = BoxTerr;
		}
		else if (Territory != BoxTerr)
		{
			OutReason = EIFPlacementInvalidReason::OutsideOwnedTerritory;
			return false;
		}
	}

	if (bRequireHeroInTerritory)
	{
		if (!IsValid(PlayerPawn) || !Territory->ContainsPoint2D(PlayerPawn->GetActorLocation()))
		{
			OutReason = EIFPlacementInvalidReason::HeroNotInTerritory;
			return false;
		}
	}

	if (Data->StructureType == EIFStructureType::Keep && Territory->HasActiveKeep())
	{
		if (!IsValid(RelocatingStructure) || !RelocatingStructure->IsKeep() || Territory->GetActiveKeep() != RelocatingStructure)
		{
			OutReason = EIFPlacementInvalidReason::KeepAlreadyExists;
			return false;
		}
	}

	// 4. Flag capture zone clearance (only ground-level boxes, Z < 220 cm).
	if (const USphereComponent* const FlagZone = Territory->GetFlagCaptureZone())
	{
		const float FlagZ = FlagZone->GetComponentLocation().Z;
		for (const FIFWorldCollisionBox& Box : CandBoxes)
		{
			if ((Box.WorldMinZ - Candidate.Location.Z) >= 220.f)
			{
				continue;
			}
			const FVector TestCenter(Box.WorldCenter.X, Box.WorldCenter.Y, FlagZ);
			const FQuat TestRot = FRotator(0.f, Box.WorldRotation.Yaw, 0.f).Quaternion();
			const FVector2D HorizHalf = GetEffectiveHorizontalHalfExtent(Box);
			if (FlagZone->OverlapComponent(TestCenter, TestRot, FCollisionShape::MakeBox(FVector(HorizHalf.X, HorizHalf.Y, 120.f))))
			{
				OutReason = EIFPlacementInvalidReason::TooCloseToFlag;
				return false;
			}
		}
	}

	// 5. 3D Structure-vs-Structure OBB SAT overlap check with 3.0 cm seam inset.
	const bool bCandIsWalkway = (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly);
	const bool bCandIsCornerWalkway = (Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly);
	const bool bCandIsGateDoor = (Data->PlacementRule == EIFPlacementRule::GateOpeningOnly);

	for (TActorIterator<AIFStructure> It(World); It; ++It)
	{
		const AIFStructure* const Existing = *It;
		if (!IsValid(Existing) || Existing == RelocatingStructure || !Existing->IsStructureAlive())
		{
			continue;
		}

		if ((bCandIsCornerWalkway || bCandIsGateDoor) && Existing == Candidate.HostStructure.Get())
		{
			continue;
		}

		const UIFStructureDataAsset* const ExistDA = Existing->GetStructureData();
		const bool bExistIsWalkway = IsValid(ExistDA) && (ExistDA->PlacementRule == EIFPlacementRule::WallWalkwayOnly);
		const bool bExistIsCornerWalkway = IsValid(ExistDA) && (ExistDA->PlacementRule == EIFPlacementRule::CornerWalkwayOnly);

		// Allow clean miter overlap between Corner Walkway and adjoining straight Wooden Walkways.
		if ((bCandIsCornerWalkway && bExistIsWalkway) || (bCandIsWalkway && bExistIsCornerWalkway))
		{
			continue;
		}

		// Allow clean miter overlap between a 45-deg WallCorner inner walkway and its two adjacent straight-wall walkways.
		if (bCandIsWalkway && bExistIsWalkway)
		{
			const float YawDiff = FMath::Abs(FRotator::NormalizeAxis(Candidate.YawDegrees - Existing->GetActorRotation().Yaw));
			const bool bIsMiterAngle = FMath::IsNearlyEqual(YawDiff, 45.f, 6.f) || FMath::IsNearlyEqual(YawDiff, 135.f, 6.f);
			if (bIsMiterAngle && FVector::DistSquared2D(Candidate.Location, Existing->GetActorLocation()) > FMath::Square(150.f))
			{
				continue;
			}
		}

		TArray<FIFWorldCollisionBox> ExistBoxes;
		Existing->GetWorldCollisionBoxes(ExistBoxes);

		for (const FIFWorldCollisionBox& CB : CandBoxes)
		{
			for (const FIFWorldCollisionBox& EB : ExistBoxes)
			{
				if (DoesWorldBoxOverlap(CB, EB, /*XYInsetCm=*/3.0f, /*ZToleranceCm=*/2.0f))
				{
					OutReason = EIFPlacementInvalidReason::BlockedByObstacle;
					return false;
				}
			}
		}
	}

	// 6. Pawn clearance (only ground-level boxes with local bottom Z < 180 cm).
	const FCollisionObjectQueryParams PawnObjects = IFCollision::MakePawnObjectQueryParams();
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFBuildPawnOverlap), false);
	if (IsValid(PlacementGhost))
	{
		QueryParams.AddIgnoredActor(PlacementGhost);
	}
	if (IsValid(RelocatingStructure))
	{
		QueryParams.AddIgnoredActor(RelocatingStructure);
	}

	for (const FIFWorldCollisionBox& Box : CandBoxes)
	{
		if ((Box.WorldMinZ - Candidate.Location.Z) >= 180.f)
		{
			continue;
		}

		const FVector TestExtent(Box.HalfExtent.X + 20.f, Box.HalfExtent.Y + 20.f, FMath::Max(Box.HalfExtent.Z, 80.f));
		TArray<FOverlapResult> PawnOverlaps;
		if (World->OverlapMultiByObjectType(
			PawnOverlaps,
			Box.WorldCenter,
			Box.WorldRotation.Quaternion(),
			PawnObjects,
			FCollisionShape::MakeBox(TestExtent),
			QueryParams))
		{
			for (const FOverlapResult& Overlap : PawnOverlaps)
			{
				const AActor* const HitActor = Overlap.GetActor();
				if (!IsValid(HitActor))
				{
					continue;
				}
				if (const AIFBaseCharacter* const BaseChar = Cast<AIFBaseCharacter>(HitActor); IsValid(BaseChar) && BaseChar->IsDead())
				{
					continue;
				}

				OutReason = EIFPlacementInvalidReason::BlockedByUnit;
				return false;
			}
		}
	}

	OutTerritory = Territory;
	OutReason = EIFPlacementInvalidReason::None;
	return true;
}
