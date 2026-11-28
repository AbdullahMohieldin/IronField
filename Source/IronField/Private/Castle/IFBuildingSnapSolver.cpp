#include "Castle/IFBuildingSnapSolver.h"

#include "Castle/IFGateDoor.h"
#include "Castle/IFPlacementGhost.h"
#include "Castle/IFStructure.h"
#include "Castle/IFStructureDataAsset.h"
#include "Castle/IFTerritory.h"
#include "Character/IFBaseCharacter.h"
#include "Combat/IFTeamTypes.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Core/IFGameState.h"
#include "Core/IFGameTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	constexpr float SnapHysteresisCm = 12.f;
	constexpr float MaxSnappedGroundDeltaCm = 80.f;
	constexpr float ObstacleGroundLiftCm = 25.f;

	struct FStraightWallBasis
	{
		FVector2D Anchor = FVector2D::ZeroVector;
		float SurfYaw = 0.f;
		FVector2D Forward = FVector2D(1.f, 0.f);
		FVector2D Right = FVector2D(0.f, 1.f);
	};

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

	FStraightWallBasis ComputeStraightWallBasis(
		const FVector2D& HostLoc2D,
		float HostYaw,
		const FIFLateralMountSurface& Surf)
	{
		FStraightWallBasis Basis;
		Basis.Anchor = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
		Basis.SurfYaw = FRotator::NormalizeAxis(HostYaw + Surf.LocalTangentYaw);
		Basis.Forward = Fwd2D(Basis.SurfYaw);
		Basis.Right = Right2D(Basis.SurfYaw);
		return Basis;
	}

	FVector2D ComputeCornerArcMidRadial(float HostYaw)
	{
		// SM_Wall_Corner90 curves 90 deg from -X to +Y around its local arc center, so (+sqrt(2)/2, -sqrt(2)/2) is the outward bisector.
		return Rotate2D(FVector2D(0.70710678f, -0.70710678f), HostYaw);
	}

	float EstimateFootprintRadius2D(const UIFStructureDataAsset* Data)
	{
		if (!IsValid(Data))
		{
			return 220.f;
		}
		return FVector2D(Data->FootprintExtent.X, Data->FootprintExtent.Y).Size();
	}

	float ApplyHostHysteresisDistSq(float RawDistSq, const AIFStructure* CandidateHost, const AIFStructure* PreviousHost)
	{
		if (!IsValid(PreviousHost) || CandidateHost != PreviousHost)
		{
			return RawDistSq;
		}

		const float AdjustedDist = FMath::Max(0.f, FMath::Sqrt(RawDistSq) - SnapHysteresisCm);
		return AdjustedDist * AdjustedDist;
	}

	int8 ResolveStickySideSign(float SignedValue, const AIFStructure* CandidateHost, const AIFStructure* PreviousHost, int8 PreviousSideSign)
	{
		if (IsValid(PreviousHost) && CandidateHost == PreviousHost && PreviousSideSign != 0)
		{
			if (PreviousSideSign > 0 && SignedValue > -SnapHysteresisCm)
			{
				return 1;
			}
			if (PreviousSideSign < 0 && SignedValue < SnapHysteresisCm)
			{
				return -1;
			}
		}
		return (SignedValue >= 0.f) ? 1 : -1;
	}

	int8 ResolveHoveredStraightWallSideSign(
		const FStraightWallBasis& Basis,
		const FVector& HoveredImpactNormal,
		const FVector& ViewRayOrigin,
		const AIFStructure* Host,
		const AIFStructure* PreviousHost,
		int8 PreviousSideSign)
	{
		// Prefer the aimed side face normal so cursor hits on a wall face never flip to the opposite side.
		const float NormalDot = FVector2D::DotProduct(FVector2D(HoveredImpactNormal), Basis.Right);
		if (FMath::Abs(NormalDot) > 0.25f)
		{
			return (NormalDot > 0.f) ? 1 : -1;
		}

		// When aiming at the horizontal top edge, fall back to which side of the wall plane the camera sits on.
		const float CameraSignedPerp = FVector2D::DotProduct(FVector2D(ViewRayOrigin) - Basis.Anchor, Basis.Right);
		return ResolveStickySideSign(CameraSignedPerp, Host, PreviousHost, PreviousSideSign);
	}

	int8 ResolveHoveredCornerArcSideSign(
		const FVector2D& WorldArcCenter,
		const FVector2D& MidRadial,
		const FVector2D& ArcMidPoint,
		const FVector& HoveredImpactPoint,
		const FVector& HoveredImpactNormal,
		const FVector& ViewRayOrigin,
		const AIFStructure* Host,
		const AIFStructure* PreviousHost,
		int8 PreviousSideSign)
	{
		const FVector2D LocalRadial = (FVector2D(HoveredImpactPoint) - WorldArcCenter).GetSafeNormal();
		if (!LocalRadial.IsNearlyZero())
		{
			const float RadialDot = FVector2D::DotProduct(FVector2D(HoveredImpactNormal), LocalRadial);
			if (RadialDot < -0.25f)
			{
				return 1;
			}
			if (RadialDot > 0.25f)
			{
				return -1;
			}
		}

		// Fall back to camera side of the corner arc's tangent plane when aiming at the top parapet.
		const float CameraInwardDist = -FVector2D::DotProduct(FVector2D(ViewRayOrigin) - ArcMidPoint, MidRadial);
		return ResolveStickySideSign(CameraInwardDist, Host, PreviousHost, PreviousSideSign);
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

	bool DoesWorldBoxOverlap(
		const FIFWorldCollisionBox& A,
		const FIFWorldCollisionBox& B,
		float XYInsetCm = 3.0f,
		float ZToleranceCm = 2.0f)
	{
		if (A.WorldMaxZ <= B.WorldMinZ + ZToleranceCm || B.WorldMaxZ <= A.WorldMinZ + ZToleranceCm)
		{
			return false;
		}

		const FVector2D EffA = GetEffectiveHorizontalHalfExtent(A);
		const FVector2D EffB = GetEffectiveHorizontalHalfExtent(B);

		// Inset both boxes (6 cm combined) so flush-snapped walls and slightly thicker gate pillars do not self-reject on seams.
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

	bool HasStoneWallBacking(
		TArrayView<const AIFStructure* const> FriendlyStructures,
		const FVector& WalkwayLocation,
		float WalkwayYaw,
		float WalkwayDepthHalfExtent,
		bool& OutIsCornerInnerWalkway,
		const AIFStructure** OutBackingHost = nullptr)
	{
		OutIsCornerInnerWalkway = false;
		if (OutBackingHost)
		{
			*OutBackingHost = nullptr;
		}

		const FVector2D WalkwayPos2D(WalkwayLocation);
		const FVector2D WalkwayFwd = Fwd2D(WalkwayYaw);
		const FVector2D WalkwayRight = Right2D(WalkwayYaw);

		for (const AIFStructure* const Host : FriendlyStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA) || HostDA->MountSurfaces.IsEmpty())
			{
				continue;
			}

			const float BroadphaseRadius = EstimateFootprintRadius2D(HostDA) + WalkwayDepthHalfExtent + 180.f;
			if (FVector::DistSquared2D(WalkwayLocation, Host->GetActorLocation()) > FMath::Square(BroadphaseRadius))
			{
				continue;
			}

			const FVector2D HostLoc2D(Host->GetActorLocation());
			const float HostYaw = Host->GetActorRotation().Yaw;

			for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
			{
				if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
				{
					const FStraightWallBasis Basis = ComputeStraightWallBasis(HostLoc2D, HostYaw, Surf);
					if (FMath::Abs(FVector2D::DotProduct(WalkwayFwd, Basis.Forward)) < 0.96f)
					{
						continue;
					}

					const FVector2D Delta = WalkwayPos2D - Basis.Anchor;
					if (FMath::Abs(FVector2D::DotProduct(Delta, Basis.Forward)) > Surf.HalfLength + 45.f)
					{
						continue;
					}

					const float SignedPerp = FVector2D::DotProduct(Delta, Basis.Right);
					const float ExpectedDist = WalkwayDepthHalfExtent + Surf.HalfThickness;
					if (FMath::Abs(FMath::Abs(SignedPerp) - ExpectedDist) > 16.f)
					{
						continue;
					}

					// Corbel (-Y = -WalkwayRight) must face toward the backing stone wall.
					const FVector2D ToWall = (Basis.Anchor - WalkwayPos2D).GetSafeNormal();
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
					const FVector2D MidRadial = ComputeCornerArcMidRadial(HostYaw);

					const FVector2D InnerCenter = WorldArcCenter + MidRadial * (Surf.InnerWalkwayBackRadius - WalkwayDepthHalfExtent);
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

					const FVector2D OuterCenter = WorldArcCenter + MidRadial * (Surf.OuterWalkwayBackRadius + WalkwayDepthHalfExtent);
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

	bool TrySnapEndToEndChannel(
		TArrayView<const AIFStructure* const> FriendlyStructures,
		const UIFStructureDataAsset* Data,
		EIFConnectorChannel Channel,
		const FVector& CursorGroundLocation,
		int32 ConnectorFlipIndex,
		float SnapRadiusCm,
		bool bPreserveHostOrientation,
		const AIFStructure* PreviousSnapHost,
		FIFSnapSolverScratch& Scratch,
		FIFSnapResult& OutResult)
	{
		TArray<FIFModularConnector, TInlineAllocator<8>> GhostChannelConnectors;
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

		const float ClampedSnapRadius = FMath::Max(50.f, SnapRadiusCm);
		float BestScoreDistSq = FMath::Square(ClampedSnapRadius);
		const AIFStructure* BestTargetStruct = nullptr;
		FIFModularConnector BestTargetConn;

		for (const AIFStructure* const TargetStruct : FriendlyStructures)
		{
			const UIFStructureDataAsset* const TargetDA = TargetStruct->GetStructureData();
			if (!IsValid(TargetDA) || TargetDA->Connectors.IsEmpty())
			{
				continue;
			}

			const float BroadphaseRadius = EstimateFootprintRadius2D(TargetDA) + ClampedSnapRadius + 60.f;
			if (FVector::DistSquared2D(CursorGroundLocation, TargetStruct->GetActorLocation()) > FMath::Square(BroadphaseRadius))
			{
				continue;
			}

			Scratch.TargetConnectors.Reset();
			TargetStruct->GetWorldConnectors(Scratch.TargetConnectors);

			for (const FIFModularConnector& TargetConn : Scratch.TargetConnectors)
			{
				if (TargetConn.Channel != Channel)
				{
					continue;
				}

				const float RawDistSq = FVector2D::DistSquared(FVector2D(CursorGroundLocation), TargetConn.LocalPosition);
				if (RawDistSq >= FMath::Square(ClampedSnapRadius))
				{
					continue;
				}

				const float ScoreDistSq = ApplyHostHysteresisDistSq(RawDistSq, TargetStruct, PreviousSnapHost);
				if (ScoreDistSq >= BestScoreDistSq)
				{
					continue;
				}

				// Skip connectors whose outward space is already occupied by another structure on this channel.
				const FVector2D ProbePos = TargetConn.LocalPosition + TargetConn.LocalOutwardNormal * 35.f;
				bool bOccupied = false;
				for (const AIFStructure* const OtherStruct : FriendlyStructures)
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

					const float OtherRadius = EstimateFootprintRadius2D(OtherDA) + 60.f;
					if (FVector2D::DistSquared(ProbePos, FVector2D(OtherStruct->GetActorLocation())) > FMath::Square(OtherRadius))
					{
						continue;
					}

					const bool bSharesChannel = OtherDA->Connectors.ContainsByPredicate(
						[Channel](const FIFModularConnector& C) { return C.Channel == Channel; });
					if (!bSharesChannel)
					{
						continue;
					}

					Scratch.OtherBoxes.Reset();
					OtherStruct->GetWorldCollisionBoxes(Scratch.OtherBoxes);
					if (IsPointInsideAnyBox2D(ProbePos, Scratch.OtherBoxes))
					{
						bOccupied = true;
						break;
					}
				}

				if (bOccupied)
				{
					continue;
				}

				BestScoreDistSq = ScoreDistSq;
				BestTargetStruct = TargetStruct;
				BestTargetConn = TargetConn;
			}
		}

		if (!IsValid(BestTargetStruct))
		{
			return false;
		}

		// Negate the target outward normal so matched connectors face head-to-head and the new piece extends outward.
		const float TargetAngleRad = FMath::Atan2(-BestTargetConn.LocalOutwardNormal.Y, -BestTargetConn.LocalOutwardNormal.X);

		int32 ChosenIdx = ((ConnectorFlipIndex % GhostChannelConnectors.Num()) + GhostChannelConnectors.Num()) % GhostChannelConnectors.Num();
		if (bPreserveHostOrientation)
		{
			const float HostYaw = BestTargetStruct->GetActorRotation().Yaw;
			float BestYawDiff = MAX_flt;
			for (int32 Idx = 0; Idx < GhostChannelConnectors.Num(); ++Idx)
			{
				const FIFModularConnector& CandConn = GhostChannelConnectors[Idx];
				const float CandGhostAngleRad = FMath::Atan2(CandConn.LocalOutwardNormal.Y, CandConn.LocalOutwardNormal.X);
				const float CandYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(TargetAngleRad - CandGhostAngleRad));
				const float YawDiff = FMath::Abs(FRotator::NormalizeAxis(CandYaw - HostYaw));
				if (YawDiff < BestYawDiff)
				{
					BestYawDiff = YawDiff;
					ChosenIdx = Idx;
				}
			}
		}

		const FIFModularConnector& GhostConn = GhostChannelConnectors[ChosenIdx];
		const float GhostAngleRad = FMath::Atan2(GhostConn.LocalOutwardNormal.Y, GhostConn.LocalOutwardNormal.X);
		const float RequiredYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(TargetAngleRad - GhostAngleRad));

		const FVector2D RotatedLocal = Rotate2D(GhostConn.LocalPosition, RequiredYaw);
		OutResult.bSnapped = true;
		OutResult.Location = FVector(
			BestTargetConn.LocalPosition.X - RotatedLocal.X,
			BestTargetConn.LocalPosition.Y - RotatedLocal.Y,
			BestTargetStruct->GetActorLocation().Z);
		OutResult.YawDegrees = RequiredYaw;
		OutResult.HostStructure = BestTargetStruct;
		return true;
	}

	bool SolveEndSnap(
		const FIFSnapQuery& Query,
		TArrayView<const AIFStructure* const> FriendlyStructures,
		FIFSnapSolverScratch& Scratch,
		FIFSnapResult& OutResult)
	{
		if (Query.Data->Connectors.IsEmpty())
		{
			return false;
		}

		FIFSnapResult SnapRes;
		if (!TrySnapEndToEndChannel(
			FriendlyStructures,
			Query.Data,
			Query.Data->Connectors[0].Channel,
			Query.CursorGroundLocation,
			Query.ConnectorFlipIndex,
			Query.SnapRadiusCm,
			/*bPreserveHostOrientation=*/false,
			Query.PreviousSnapHost,
			Scratch,
			SnapRes))
		{
			return false;
		}

		const UIFStructureDataAsset* const HostDA = SnapRes.HostStructure.IsValid() ? SnapRes.HostStructure->GetStructureData() : nullptr;
		const bool bHostIsCorner = IsValid(HostDA) && HostDA->MountSurfaces.ContainsByPredicate(
			[](const FIFLateralMountSurface& S) { return S.Kind == EIFMountSurfaceKind::CornerArc; });
		SnapRes.SnapState = bHostIsCorner ? EIFPlacementSnapState::SnappedToCorner : EIFPlacementSnapState::SnappedToWall;
		OutResult = SnapRes;
		return true;
	}

	bool SolveCornerWalkwaySnap(
		const FIFSnapQuery& Query,
		TArrayView<const AIFStructure* const> FriendlyStructures,
		FIFSnapResult& OutResult)
	{
		if (IsValid(Query.HoveredStructure) && FriendlyStructures.Contains(Query.HoveredStructure))
		{
			const UIFStructureDataAsset* const HoveredDA = Query.HoveredStructure->GetStructureData();
			if (IsValid(HoveredDA))
			{
				const FVector HostLoc = Query.HoveredStructure->GetActorLocation();
				const FVector2D HostLoc2D(HostLoc);
				const float HostYaw = Query.HoveredStructure->GetActorRotation().Yaw;

				for (const FIFLateralMountSurface& Surf : HoveredDA->MountSurfaces)
				{
					if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
					{
						const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
						OutResult = FIFSnapResult();
						OutResult.bSnapped = true;
						OutResult.Location = FVector(WorldArcCenter.X, WorldArcCenter.Y, HostLoc.Z);
						OutResult.YawDegrees = FRotator::NormalizeAxis(HostYaw + 135.f);
						OutResult.HostStructure = Query.HoveredStructure;
						OutResult.bIsCornerInnerWalkway = true;
						OutResult.bBackedByStoneWall = true;
						OutResult.SideSign = 1;
						OutResult.SnapState = EIFPlacementSnapState::SnappedToCorner;
						return true;
					}
				}
			}
		}

		const FVector2D Cursor2D(Query.CursorGroundLocation);
		const float ClampedRadius = FMath::Max(100.f, Query.MountSnapRadiusCm);
		float BestScoreDistSq = FMath::Square(ClampedRadius);
		FIFSnapResult BestCornerSnap;

		for (const AIFStructure* const Host : FriendlyStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA) || HostDA->MountSurfaces.IsEmpty())
			{
				continue;
			}

			const FVector HostLoc = Host->GetActorLocation();
			const float BroadphaseRadius = EstimateFootprintRadius2D(HostDA) + ClampedRadius + 120.f;
			if (FVector::DistSquared2D(Query.CursorGroundLocation, HostLoc) > FMath::Square(BroadphaseRadius))
			{
				continue;
			}

			const FVector2D HostLoc2D(HostLoc);
			const float HostYaw = Host->GetActorRotation().Yaw;

			for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
			{
				if (Surf.Kind != EIFMountSurfaceKind::CornerArc)
				{
					continue;
				}

				const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
				const FVector2D MidRadial = ComputeCornerArcMidRadial(HostYaw);
				const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;

				const float RawDistSq = FMath::Min(
					FVector2D::DistSquared(Cursor2D, ArcMidPoint),
					FVector2D::DistSquared(Cursor2D, WorldArcCenter + MidRadial * (Surf.ArcMidRadius * 0.55f)));
				if (RawDistSq >= FMath::Square(ClampedRadius))
				{
					continue;
				}

				const float ScoreDistSq = ApplyHostHysteresisDistSq(RawDistSq, Host, Query.PreviousSnapHost);
				if (ScoreDistSq < BestScoreDistSq)
				{
					BestScoreDistSq = ScoreDistSq;
					BestCornerSnap.bSnapped = true;
					BestCornerSnap.Location = FVector(WorldArcCenter.X, WorldArcCenter.Y, HostLoc.Z);
					BestCornerSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw + 135.f);
					BestCornerSnap.HostStructure = Host;
					BestCornerSnap.bIsCornerInnerWalkway = true;
					BestCornerSnap.bBackedByStoneWall = true;
					BestCornerSnap.SideSign = 1;
					BestCornerSnap.SnapState = EIFPlacementSnapState::SnappedToCorner;
				}
			}
		}

		if (BestCornerSnap.bSnapped)
		{
			OutResult = BestCornerSnap;
			return true;
		}
		return false;
	}

	bool SolveGateDoorSnap(
		const FIFSnapQuery& Query,
		TArrayView<const AIFStructure* const> FriendlyStructures,
		FIFSnapResult& OutResult)
	{
		if (IsValid(Query.HoveredStructure) && FriendlyStructures.Contains(Query.HoveredStructure))
		{
			const AIFStructure* TargetGate = Query.HoveredStructure;
			if (const AIFGateDoor* const HoveredDoor = Cast<AIFGateDoor>(TargetGate))
			{
				TargetGate = HoveredDoor->GetHostGate();
			}

			if (IsValid(TargetGate) && FriendlyStructures.Contains(TargetGate))
			{
				const UIFStructureDataAsset* const HostDA = TargetGate->GetStructureData();
				if (IsValid(HostDA) && HostDA->bSupportsGateDoor)
				{
					const FVector HostLoc = TargetGate->GetActorLocation();
					OutResult = FIFSnapResult();
					OutResult.bSnapped = true;
					OutResult.Location = FVector(HostLoc.X, HostLoc.Y, HostLoc.Z);
					OutResult.YawDegrees = TargetGate->GetActorRotation().Yaw;
					OutResult.HostStructure = TargetGate;
					OutResult.bBackedByStoneWall = true;
					OutResult.SideSign = 0;
					OutResult.SnapState = EIFPlacementSnapState::SnappedToGate;
					return true;
				}
			}
		}

		const FVector2D Cursor2D(Query.CursorGroundLocation);
		const float ClampedRadius = FMath::Max(120.f, Query.MountSnapRadiusCm);
		float BestScoreDistSq = FMath::Square(ClampedRadius);
		FIFSnapResult BestGateSnap;

		for (const AIFStructure* const Host : FriendlyStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA) || !HostDA->bSupportsGateDoor)
			{
				continue;
			}

			const FVector HostLoc = Host->GetActorLocation();
			const float RawDistSq = FVector2D::DistSquared(Cursor2D, FVector2D(HostLoc));
			if (RawDistSq >= FMath::Square(ClampedRadius))
			{
				continue;
			}

			const float ScoreDistSq = ApplyHostHysteresisDistSq(RawDistSq, Host, Query.PreviousSnapHost);
			if (ScoreDistSq < BestScoreDistSq)
			{
				BestScoreDistSq = ScoreDistSq;
				BestGateSnap.bSnapped = true;
				BestGateSnap.Location = FVector(HostLoc.X, HostLoc.Y, HostLoc.Z);
				BestGateSnap.YawDegrees = Host->GetActorRotation().Yaw;
				BestGateSnap.HostStructure = Host;
				BestGateSnap.bBackedByStoneWall = true;
				BestGateSnap.SideSign = 0;
				BestGateSnap.SnapState = EIFPlacementSnapState::SnappedToGate;
			}
		}

		if (BestGateSnap.bSnapped)
		{
			OutResult = BestGateSnap;
			return true;
		}
		return false;
	}

	bool SolveWallWalkwaySnap(
		const FIFSnapQuery& Query,
		TArrayView<const AIFStructure* const> FriendlyStructures,
		FIFSnapSolverScratch& Scratch,
		FIFSnapResult& OutResult)
	{
		if (IsValid(Query.HoveredStructure) && FriendlyStructures.Contains(Query.HoveredStructure))
		{
			const AIFStructure* const Hovered = Query.HoveredStructure;
			const UIFStructureDataAsset* const HoveredDA = Hovered->GetStructureData();
			if (IsValid(HoveredDA))
			{
				const FVector HostLoc = Hovered->GetActorLocation();
				const FVector2D HostLoc2D(HostLoc);
				const float HostYaw = Hovered->GetActorRotation().Yaw;
				const FVector2D Impact2D(Query.HoveredImpactPoint);

				FIFSnapResult HoveredWallSnap;
				float BestHoveredSurfDistSq = MAX_flt;

				for (const FIFLateralMountSurface& Surf : HoveredDA->MountSurfaces)
				{
					if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
					{
						const FStraightWallBasis Basis = ComputeStraightWallBasis(HostLoc2D, HostYaw, Surf);
						const int8 SideSign = ResolveHoveredStraightWallSideSign(
							Basis,
							Query.HoveredImpactNormal,
							Query.ViewRayOrigin,
							Hovered,
							Query.PreviousSnapHost,
							Query.PreviousSideSign);
						if ((SideSign > 0 && !Surf.bAllowPositiveSide) || (SideSign < 0 && !Surf.bAllowNegativeSide))
						{
							continue;
						}

						const float SurfDistSq = FVector2D::DistSquared(Impact2D, Basis.Anchor);
						if (SurfDistSq < BestHoveredSurfDistSq)
						{
							BestHoveredSurfDistSq = SurfDistSq;
							const FVector2D CandPos2D = Basis.Anchor + Basis.Right * (static_cast<float>(SideSign) * (Surf.HalfThickness + Query.Data->AttachmentDepthHalfExtent));
							HoveredWallSnap = FIFSnapResult();
							HoveredWallSnap.bSnapped = true;
							HoveredWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, HostLoc.Z);
							HoveredWallSnap.YawDegrees = (SideSign > 0) ? Basis.SurfYaw : FRotator::NormalizeAxis(Basis.SurfYaw + 180.f);
							HoveredWallSnap.HostStructure = Hovered;
							HoveredWallSnap.bIsCornerInnerWalkway = false;
							HoveredWallSnap.bBackedByStoneWall = true;
							HoveredWallSnap.SideSign = SideSign;
							HoveredWallSnap.SnapState = EIFPlacementSnapState::SnappedToWall;
						}
					}
					else if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
					{
						const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
						const FVector2D MidRadial = ComputeCornerArcMidRadial(HostYaw);
						const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;
						const int8 SideSign = ResolveHoveredCornerArcSideSign(
							WorldArcCenter,
							MidRadial,
							ArcMidPoint,
							Query.HoveredImpactPoint,
							Query.HoveredImpactNormal,
							Query.ViewRayOrigin,
							Hovered,
							Query.PreviousSnapHost,
							Query.PreviousSideSign);
						const bool bInnerSide = (SideSign > 0);
						if ((bInnerSide && !Surf.bAllowPositiveSide) || (!bInnerSide && !Surf.bAllowNegativeSide))
						{
							continue;
						}

						const float SurfDistSq = FVector2D::DistSquared(Impact2D, ArcMidPoint);
						if (SurfDistSq < BestHoveredSurfDistSq)
						{
							BestHoveredSurfDistSq = SurfDistSq;
							HoveredWallSnap = FIFSnapResult();
							HoveredWallSnap.bSnapped = true;
							if (bInnerSide)
							{
								const FVector2D CandPos2D = WorldArcCenter + MidRadial * (Surf.InnerWalkwayBackRadius - Query.Data->AttachmentDepthHalfExtent);
								HoveredWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, HostLoc.Z);
								HoveredWallSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw + 45.f);
								HoveredWallSnap.bIsCornerInnerWalkway = true;
							}
							else
							{
								const FVector2D CandPos2D = WorldArcCenter + MidRadial * (Surf.OuterWalkwayBackRadius + Query.Data->AttachmentDepthHalfExtent);
								HoveredWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, HostLoc.Z);
								HoveredWallSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw - 135.f);
								HoveredWallSnap.bIsCornerInnerWalkway = false;
							}
							HoveredWallSnap.HostStructure = Hovered;
							HoveredWallSnap.bBackedByStoneWall = true;
							HoveredWallSnap.SideSign = SideSign;
							HoveredWallSnap.SnapState = EIFPlacementSnapState::SnappedToCorner;
						}
					}
				}

				if (HoveredWallSnap.bSnapped)
				{
					OutResult = HoveredWallSnap;
					return true;
				}

				if (HoveredDA->PlacementRule == EIFPlacementRule::WallWalkwayOnly)
				{
					FIFSnapResult ChainSnap;
					const FVector HoveredChainAnchor(Query.HoveredImpactPoint.X, Query.HoveredImpactPoint.Y, HostLoc.Z);
					if (TrySnapEndToEndChannel(
						FriendlyStructures,
						Query.Data,
						EIFConnectorChannel::WoodWalkway,
						HoveredChainAnchor,
						0,
						FMath::Max(Query.SnapRadiusCm, Query.MountSnapRadiusCm),
						/*bPreserveHostOrientation=*/true,
						Query.PreviousSnapHost,
						Scratch,
						ChainSnap))
					{
						bool bChainCornerInner = false;
						const AIFStructure* BackingHost = nullptr;
						if (HasStoneWallBacking(
							FriendlyStructures,
							ChainSnap.Location,
							ChainSnap.YawDegrees,
							Query.Data->AttachmentDepthHalfExtent,
							bChainCornerInner,
							&BackingHost))
						{
							ChainSnap.bIsCornerInnerWalkway = bChainCornerInner;
							ChainSnap.bBackedByStoneWall = true;
							ChainSnap.HostStructure = IsValid(BackingHost) ? BackingHost : ChainSnap.HostStructure.Get();
							if (ChainSnap.HostStructure.IsValid())
							{
								ChainSnap.Location.Z = ChainSnap.HostStructure->GetActorLocation().Z;
							}
							ChainSnap.SnapState = bChainCornerInner ? EIFPlacementSnapState::SnappedToCorner : EIFPlacementSnapState::SnappedToWalkway;
							OutResult = ChainSnap;
							return true;
						}
					}
				}
			}
		}

		const FVector2D Cursor2D(Query.CursorGroundLocation);
		const float ClampedRadius = FMath::Max(100.f, Query.MountSnapRadiusCm);
		float BestScoreDistSq = FMath::Square(ClampedRadius);
		FIFSnapResult BestWallSnap;

		for (const AIFStructure* const Host : FriendlyStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA) || HostDA->MountSurfaces.IsEmpty())
			{
				continue;
			}

			const FVector HostLoc = Host->GetActorLocation();
			const float BroadphaseRadius = EstimateFootprintRadius2D(HostDA) + ClampedRadius + 140.f;
			if (FVector::DistSquared2D(Query.CursorGroundLocation, HostLoc) > FMath::Square(BroadphaseRadius))
			{
				continue;
			}

			const FVector2D HostLoc2D(HostLoc);
			const float HostYaw = Host->GetActorRotation().Yaw;

			for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
			{
				if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
				{
					const FStraightWallBasis Basis = ComputeStraightWallBasis(HostLoc2D, HostYaw, Surf);
					const FVector2D Delta = Cursor2D - Basis.Anchor;
					if (FMath::Abs(FVector2D::DotProduct(Delta, Basis.Forward)) > Surf.HalfLength + 90.f)
					{
						continue;
					}

					const float SignedPerp = FVector2D::DotProduct(Delta, Basis.Right);
					const int8 SideSign = ResolveStickySideSign(SignedPerp, Host, Query.PreviousSnapHost, Query.PreviousSideSign);
					if ((SideSign > 0 && !Surf.bAllowPositiveSide) || (SideSign < 0 && !Surf.bAllowNegativeSide))
					{
						continue;
					}

					const FVector2D CandPos2D = Basis.Anchor + Basis.Right * (static_cast<float>(SideSign) * (Surf.HalfThickness + Query.Data->AttachmentDepthHalfExtent));
					const float RawDistSq = FVector2D::DistSquared(Cursor2D, CandPos2D);
					if (RawDistSq >= FMath::Square(ClampedRadius))
					{
						continue;
					}

					const float ScoreDistSq = ApplyHostHysteresisDistSq(RawDistSq, Host, Query.PreviousSnapHost);
					if (ScoreDistSq < BestScoreDistSq)
					{
						BestScoreDistSq = ScoreDistSq;
						BestWallSnap.bSnapped = true;
						BestWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, HostLoc.Z);
						// -Y corbels must face the backing stone wall on both sides.
						BestWallSnap.YawDegrees = (SideSign > 0) ? Basis.SurfYaw : FRotator::NormalizeAxis(Basis.SurfYaw + 180.f);
						BestWallSnap.HostStructure = Host;
						BestWallSnap.bIsCornerInnerWalkway = false;
						BestWallSnap.bBackedByStoneWall = true;
						BestWallSnap.SideSign = SideSign;
						BestWallSnap.SnapState = EIFPlacementSnapState::SnappedToWall;
					}
				}
				else if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
				{
					const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
					const FVector2D MidRadial = ComputeCornerArcMidRadial(HostYaw);
					const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;

					const float RawDistToCornerSq = FVector2D::DistSquared(Cursor2D, ArcMidPoint);
					if (RawDistToCornerSq >= FMath::Square(ClampedRadius))
					{
						continue;
					}

					const float ScoreDistSq = ApplyHostHysteresisDistSq(RawDistToCornerSq, Host, Query.PreviousSnapHost);
					if (ScoreDistSq >= BestScoreDistSq)
					{
						continue;
					}

					const float CursorRadius = FVector2D::Distance(Cursor2D, WorldArcCenter);
					const int8 SideSign = ResolveStickySideSign(Surf.ArcMidRadius - CursorRadius, Host, Query.PreviousSnapHost, Query.PreviousSideSign);
					const bool bInnerSide = (SideSign > 0);
					if ((bInnerSide && !Surf.bAllowPositiveSide) || (!bInnerSide && !Surf.bAllowNegativeSide))
					{
						continue;
					}

					BestScoreDistSq = ScoreDistSq;
					BestWallSnap.bSnapped = true;
					if (bInnerSide)
					{
						const FVector2D CandPos2D = WorldArcCenter + MidRadial * (Surf.InnerWalkwayBackRadius - Query.Data->AttachmentDepthHalfExtent);
						BestWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, HostLoc.Z);
						BestWallSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw + 45.f);
						BestWallSnap.bIsCornerInnerWalkway = true;
					}
					else
					{
						const FVector2D CandPos2D = WorldArcCenter + MidRadial * (Surf.OuterWalkwayBackRadius + Query.Data->AttachmentDepthHalfExtent);
						BestWallSnap.Location = FVector(CandPos2D.X, CandPos2D.Y, HostLoc.Z);
						BestWallSnap.YawDegrees = FRotator::NormalizeAxis(HostYaw - 135.f);
						BestWallSnap.bIsCornerInnerWalkway = false;
					}
					BestWallSnap.HostStructure = Host;
					BestWallSnap.bBackedByStoneWall = true;
					BestWallSnap.SideSign = SideSign;
					BestWallSnap.SnapState = EIFPlacementSnapState::SnappedToCorner;
				}
			}
		}

		// Also try end-to-end WoodWalkway chaining, preserving corbel orientation on both +X and -X ends.
		FIFSnapResult ChainSnap;
		if (TrySnapEndToEndChannel(
			FriendlyStructures,
			Query.Data,
			EIFConnectorChannel::WoodWalkway,
			Query.CursorGroundLocation,
			0,
			Query.SnapRadiusCm,
			/*bPreserveHostOrientation=*/true,
			Query.PreviousSnapHost,
			Scratch,
			ChainSnap))
		{
			bool bChainCornerInner = false;
			const AIFStructure* BackingHost = nullptr;
			if (HasStoneWallBacking(
				FriendlyStructures,
				ChainSnap.Location,
				ChainSnap.YawDegrees,
				Query.Data->AttachmentDepthHalfExtent,
				bChainCornerInner,
				&BackingHost))
			{
				ChainSnap.bIsCornerInnerWalkway = bChainCornerInner;
				ChainSnap.bBackedByStoneWall = true;
				ChainSnap.HostStructure = IsValid(BackingHost) ? BackingHost : ChainSnap.HostStructure.Get();
				if (ChainSnap.HostStructure.IsValid())
				{
					ChainSnap.Location.Z = ChainSnap.HostStructure->GetActorLocation().Z;
				}
				ChainSnap.SnapState = bChainCornerInner ? EIFPlacementSnapState::SnappedToCorner : EIFPlacementSnapState::SnappedToWalkway;
				const float ChainDistSq = FVector2D::DistSquared(Cursor2D, FVector2D(ChainSnap.Location));
				if (!BestWallSnap.bSnapped || ChainDistSq < BestScoreDistSq)
				{
					OutResult = ChainSnap;
					return true;
				}
			}
		}

		if (BestWallSnap.bSnapped)
		{
			OutResult = BestWallSnap;
			return true;
		}
		return false;
	}

	bool SolveStairSnap(
		const FIFSnapQuery& Query,
		TArrayView<const AIFStructure* const> FriendlyStructures,
		float FreeYaw,
		FIFSnapResult& OutResult)
	{
		const FVector2D Cursor2D(Query.CursorGroundLocation);
		const float ClampedRadius = FMath::Max(100.f, Query.MountSnapRadiusCm);
		float BestScoreDistSq = FMath::Square(ClampedRadius);
		FIFSnapResult BestStairSnap;
		const int32 Mode = ((Query.StairMountMode % 3) + 3) % 3;

		auto FindWalkwayOnSide = [&FriendlyStructures](const FVector2D& WallAnchor, const FVector2D& SideNormal, float WallHalfThickness) -> const AIFStructure*
		{
			for (const AIFStructure* const S : FriendlyStructures)
			{
				const UIFStructureDataAsset* const SDA = S->GetStructureData();
				if (IsValid(SDA) && SDA->PlacementRule == EIFPlacementRule::WallWalkwayOnly)
				{
					const FVector2D Probe = WallAnchor + SideNormal * (WallHalfThickness + SDA->AttachmentDepthHalfExtent);
					if (FVector2D::DistSquared(FVector2D(S->GetActorLocation()), Probe) < FMath::Square(55.f))
					{
						return S;
					}
				}
			}
			return nullptr;
		};

		auto BuildStairSnapResult = [&Query, Mode, FreeYaw](
			const AIFStructure* Host,
			const FVector2D& MountAnchor,
			const FVector2D& SideNormal,
			int8 ChosenSideSign,
			EIFPlacementSnapState CandidateSnapState) -> FIFSnapResult
		{
			FIFSnapResult Snap;
			Snap.bSnapped = true;
			Snap.HostStructure = Host;
			Snap.SideSign = ChosenSideSign;
			Snap.SnapState = CandidateSnapState;

			const FVector2D Tangent(-SideNormal.Y, SideNormal.X);
			FVector2D FinalPos2D = MountAnchor;
			float FinalYaw = FreeYaw;

			if (Mode == 0)
			{
				FinalPos2D = MountAnchor + SideNormal * Query.Data->AttachmentDepthHalfExtent - Tangent * Query.Data->StairParallelLandingShift;
				FinalYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X)));
			}
			else if (Mode == 1)
			{
				FinalPos2D = MountAnchor + SideNormal * Query.Data->AttachmentDepthHalfExtent + Tangent * Query.Data->StairParallelLandingShift;
				FinalYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(-Tangent.Y, -Tangent.X)));
			}
			else
			{
				FinalPos2D = MountAnchor + SideNormal * Query.Data->StairHalfLength;
				FinalYaw = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(-SideNormal.Y, -SideNormal.X)));
			}

			Snap.Location = FVector(FinalPos2D.X, FinalPos2D.Y, Host->GetActorLocation().Z);
			Snap.YawDegrees = FinalYaw;
			return Snap;
		};

		if (IsValid(Query.HoveredStructure) && FriendlyStructures.Contains(Query.HoveredStructure))
		{
			const AIFStructure* const Hovered = Query.HoveredStructure;
			const UIFStructureDataAsset* const HoveredDA = Hovered->GetStructureData();
			if (IsValid(HoveredDA) && !HoveredDA->MountSurfaces.IsEmpty())
			{
				const FVector HostLoc = Hovered->GetActorLocation();
				const FVector2D HostLoc2D(HostLoc);
				const float HostYaw = Hovered->GetActorRotation().Yaw;
				const FVector2D Impact2D(Query.HoveredImpactPoint);

				FIFSnapResult HoveredStairSnap;
				float BestHoveredSurfDistSq = MAX_flt;

				for (const FIFLateralMountSurface& Surf : HoveredDA->MountSurfaces)
				{
					const AIFStructure* SnapHost = Hovered;
					FVector2D MountAnchor = FVector2D::ZeroVector;
					FVector2D SideNormal = FVector2D(0.f, 1.f);
					int8 ChosenSideSign = 0;
					EIFPlacementSnapState CandidateSnapState = EIFPlacementSnapState::SnappedToWall;
					float SurfDistSq = MAX_flt;

					if (Surf.Kind == EIFMountSurfaceKind::WalkwayOuterEdge)
					{
						const FStraightWallBasis Basis = ComputeStraightWallBasis(HostLoc2D, HostYaw, Surf);
						SideNormal = Basis.Right;
						MountAnchor = Basis.Anchor + SideNormal * Surf.HalfThickness;
						ChosenSideSign = 1;
						CandidateSnapState = EIFPlacementSnapState::SnappedToWalkway;
						SurfDistSq = FVector2D::DistSquared(Impact2D, MountAnchor);
					}
					else if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
					{
						const FStraightWallBasis Basis = ComputeStraightWallBasis(HostLoc2D, HostYaw, Surf);
						ChosenSideSign = ResolveHoveredStraightWallSideSign(
							Basis,
							Query.HoveredImpactNormal,
							Query.ViewRayOrigin,
							Hovered,
							Query.PreviousSnapHost,
							Query.PreviousSideSign);
						if ((ChosenSideSign > 0 && !Surf.bAllowPositiveSide) || (ChosenSideSign < 0 && !Surf.bAllowNegativeSide))
						{
							continue;
						}

						SideNormal = Basis.Right * static_cast<float>(ChosenSideSign);
						if (const AIFStructure* const SideWalkway = FindWalkwayOnSide(Basis.Anchor, SideNormal, Surf.HalfThickness))
						{
							const UIFStructureDataAsset* const WalkwayDA = SideWalkway->GetStructureData();
							bool bFoundWalkwayEdge = false;
							if (IsValid(WalkwayDA))
							{
								const FVector2D WalkwayLoc2D(SideWalkway->GetActorLocation());
								const float WalkwayYaw = SideWalkway->GetActorRotation().Yaw;
								for (const FIFLateralMountSurface& WalkwaySurf : WalkwayDA->MountSurfaces)
								{
									if (WalkwaySurf.Kind == EIFMountSurfaceKind::WalkwayOuterEdge)
									{
										const FStraightWallBasis WalkwayBasis = ComputeStraightWallBasis(WalkwayLoc2D, WalkwayYaw, WalkwaySurf);
										SnapHost = SideWalkway;
										SideNormal = WalkwayBasis.Right;
										MountAnchor = WalkwayBasis.Anchor + SideNormal * WalkwaySurf.HalfThickness;
										ChosenSideSign = 1;
										CandidateSnapState = EIFPlacementSnapState::SnappedToWalkway;
										SurfDistSq = FVector2D::DistSquared(Impact2D, Basis.Anchor);
										bFoundWalkwayEdge = true;
										break;
									}
								}
							}
							if (!bFoundWalkwayEdge)
							{
								continue;
							}
						}
						else
						{
							MountAnchor = Basis.Anchor + SideNormal * Surf.HalfThickness;
							CandidateSnapState = EIFPlacementSnapState::SnappedToWall;
							SurfDistSq = FVector2D::DistSquared(Impact2D, Basis.Anchor);
						}
					}
					else if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
					{
						const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
						const FVector2D MidRadial = ComputeCornerArcMidRadial(HostYaw);
						const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;
						ChosenSideSign = ResolveHoveredCornerArcSideSign(
							WorldArcCenter,
							MidRadial,
							ArcMidPoint,
							Query.HoveredImpactPoint,
							Query.HoveredImpactNormal,
							Query.ViewRayOrigin,
							Hovered,
							Query.PreviousSnapHost,
							Query.PreviousSideSign);
						const bool bInnerSide = (ChosenSideSign > 0);
						if ((bInnerSide && !Surf.bAllowPositiveSide) || (!bInnerSide && !Surf.bAllowNegativeSide))
						{
							continue;
						}

						SideNormal = bInnerSide ? -MidRadial : MidRadial;
						const float MountRadius = bInnerSide ? Surf.InnerWalkwayBackRadius : Surf.OuterWalkwayBackRadius;
						MountAnchor = WorldArcCenter + MidRadial * MountRadius;
						CandidateSnapState = EIFPlacementSnapState::SnappedToCorner;
						SurfDistSq = FVector2D::DistSquared(Impact2D, ArcMidPoint);
					}
					else
					{
						continue;
					}

					if (SurfDistSq < BestHoveredSurfDistSq)
					{
						BestHoveredSurfDistSq = SurfDistSq;
						HoveredStairSnap = BuildStairSnapResult(SnapHost, MountAnchor, SideNormal, ChosenSideSign, CandidateSnapState);
					}
				}

				if (HoveredStairSnap.bSnapped)
				{
					OutResult = HoveredStairSnap;
					return true;
				}
			}
		}

		for (const AIFStructure* const Host : FriendlyStructures)
		{
			const UIFStructureDataAsset* const HostDA = Host->GetStructureData();
			if (!IsValid(HostDA) || HostDA->MountSurfaces.IsEmpty())
			{
				continue;
			}

			const FVector HostLoc = Host->GetActorLocation();
			const float BroadphaseRadius = EstimateFootprintRadius2D(HostDA) + ClampedRadius + 160.f;
			if (FVector::DistSquared2D(Query.CursorGroundLocation, HostLoc) > FMath::Square(BroadphaseRadius))
			{
				continue;
			}

			const FVector2D HostLoc2D(HostLoc);
			const float HostYaw = Host->GetActorRotation().Yaw;

			for (const FIFLateralMountSurface& Surf : HostDA->MountSurfaces)
			{
				FVector2D MountAnchor = FVector2D::ZeroVector;
				FVector2D SideNormal = FVector2D(0.f, 1.f);
				float RawDistSq = MAX_flt;
				float PriorityScale = 1.f;
				int8 ChosenSideSign = 0;
				EIFPlacementSnapState CandidateSnapState = EIFPlacementSnapState::SnappedToWall;

				if (Surf.Kind == EIFMountSurfaceKind::WalkwayOuterEdge)
				{
					const FStraightWallBasis Basis = ComputeStraightWallBasis(HostLoc2D, HostYaw, Surf);
					const FVector2D Delta = Cursor2D - Basis.Anchor;
					if (FMath::Abs(FVector2D::DotProduct(Delta, Basis.Forward)) > Surf.HalfLength + 90.f)
					{
						continue;
					}
					if (FVector2D::DotProduct(Delta, Basis.Right) < -15.f)
					{
						continue;
					}

					SideNormal = Basis.Right;
					MountAnchor = Basis.Anchor + SideNormal * Surf.HalfThickness;
					RawDistSq = FVector2D::DistSquared(Cursor2D, MountAnchor);
					PriorityScale = 0.85f;
					ChosenSideSign = 1;
					CandidateSnapState = EIFPlacementSnapState::SnappedToWalkway;
				}
				else if (Surf.Kind == EIFMountSurfaceKind::StraightWall)
				{
					const FStraightWallBasis Basis = ComputeStraightWallBasis(HostLoc2D, HostYaw, Surf);
					const FVector2D Delta = Cursor2D - Basis.Anchor;
					if (FMath::Abs(FVector2D::DotProduct(Delta, Basis.Forward)) > Surf.HalfLength + 90.f)
					{
						continue;
					}

					const float SignedPerp = FVector2D::DotProduct(Delta, Basis.Right);
					ChosenSideSign = ResolveStickySideSign(SignedPerp, Host, Query.PreviousSnapHost, Query.PreviousSideSign);
					if ((ChosenSideSign > 0 && !Surf.bAllowPositiveSide) || (ChosenSideSign < 0 && !Surf.bAllowNegativeSide))
					{
						continue;
					}

					SideNormal = Basis.Right * static_cast<float>(ChosenSideSign);
					if (FindWalkwayOnSide(Basis.Anchor, SideNormal, Surf.HalfThickness) != nullptr)
					{
						continue;
					}

					MountAnchor = Basis.Anchor + SideNormal * Surf.HalfThickness;
					RawDistSq = FVector2D::DistSquared(Cursor2D, MountAnchor);
					CandidateSnapState = EIFPlacementSnapState::SnappedToWall;
				}
				else if (Surf.Kind == EIFMountSurfaceKind::CornerArc)
				{
					const FVector2D WorldArcCenter = HostLoc2D + Rotate2D(Surf.LocalCenter, HostYaw);
					const FVector2D MidRadial = ComputeCornerArcMidRadial(HostYaw);
					const FVector2D ArcMidPoint = WorldArcCenter + MidRadial * Surf.ArcMidRadius;
					RawDistSq = FVector2D::DistSquared(Cursor2D, ArcMidPoint);

					const float CursorRadius = FVector2D::Distance(Cursor2D, WorldArcCenter);
					ChosenSideSign = ResolveStickySideSign(Surf.ArcMidRadius - CursorRadius, Host, Query.PreviousSnapHost, Query.PreviousSideSign);
					const bool bInnerSide = (ChosenSideSign > 0);
					if ((bInnerSide && !Surf.bAllowPositiveSide) || (!bInnerSide && !Surf.bAllowNegativeSide))
					{
						continue;
					}

					SideNormal = bInnerSide ? -MidRadial : MidRadial;
					const float MountRadius = bInnerSide ? Surf.InnerWalkwayBackRadius : Surf.OuterWalkwayBackRadius;
					MountAnchor = WorldArcCenter + MidRadial * MountRadius;
					CandidateSnapState = EIFPlacementSnapState::SnappedToCorner;
				}
				else
				{
					continue;
				}

				if (RawDistSq >= FMath::Square(ClampedRadius))
				{
					continue;
				}

				const float ScoreDistSq = ApplyHostHysteresisDistSq(RawDistSq, Host, Query.PreviousSnapHost) * PriorityScale;
				if (ScoreDistSq >= BestScoreDistSq)
				{
					continue;
				}

				BestScoreDistSq = ScoreDistSq;
				BestStairSnap = BuildStairSnapResult(Host, MountAnchor, SideNormal, ChosenSideSign, CandidateSnapState);
			}
		}

		if (BestStairSnap.bSnapped)
		{
			OutResult = BestStairSnap;
			return true;
		}
		return false;
	}

	bool ValidateAttachmentRule(
		const FIFPlacementValidationContext& Context,
		TArrayView<const AIFStructure* const> AllAliveStructures,
		EIFPlacementInvalidReason& OutReason)
	{
		const UIFStructureDataAsset* const Data = Context.Data;
		const FIFSnapResult& Candidate = *Context.Candidate;

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
			for (const AIFStructure* const Struct : AllAliveStructures)
			{
				const AIFGateDoor* const ExistingDoor = Cast<AIFGateDoor>(Struct);
				if (!IsValid(ExistingDoor) || ExistingDoor == Context.RelocatingStructure)
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

		return true;
	}

	bool ValidateGroundAndSlope(
		const FIFPlacementValidationContext& Context,
		EIFPlacementInvalidReason& OutReason)
	{
		const UIFStructureDataAsset* const Data = Context.Data;
		const FIFSnapResult& Candidate = *Context.Candidate;

		const bool bIsGroundBearing = (Data->PlacementRule == EIFPlacementRule::FreeOrEndSnap)
			|| (Data->PlacementRule == EIFPlacementRule::StairMountOrFree);
		if (!bIsGroundBearing)
		{
			return true;
		}

		const float MinGroundNormalZ = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Context.MaxPlacementSlopeDegrees, 0.f, 89.f)));

		if (!Candidate.bSnapped)
		{
			if (!IsValid(Context.GroundActor))
			{
				OutReason = EIFPlacementInvalidReason::InvalidLocation;
				return false;
			}
			if (Context.GroundNormal.Z < MinGroundNormalZ)
			{
				OutReason = EIFPlacementInvalidReason::SlopeTooSteep;
				return false;
			}
			return true;
		}

		// Verify ground exists directly beneath host-Z-locked ground pieces so wall runs cannot cantilever off cliffs.
		FCollisionObjectQueryParams GroundParams;
		GroundParams.AddObjectTypesToQuery(ECC_WorldStatic);

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IFBuildSnapGroundCheck), false);
		if (IsValid(Context.PlacementGhost))
		{
			QueryParams.AddIgnoredActor(Context.PlacementGhost);
		}
		if (IsValid(Context.RelocatingStructure))
		{
			QueryParams.AddIgnoredActor(Context.RelocatingStructure);
		}

		const FVector TraceStart(Candidate.Location.X, Candidate.Location.Y, Candidate.Location.Z + 120.f);
		const FVector TraceEnd(Candidate.Location.X, Candidate.Location.Y, Candidate.Location.Z - 200.f);
		FHitResult GroundHit;
		if (!Context.World->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceEnd, GroundParams, QueryParams))
		{
			OutReason = EIFPlacementInvalidReason::InvalidLocation;
			return false;
		}

		if (FMath::Abs(GroundHit.ImpactPoint.Z - Candidate.Location.Z) > MaxSnappedGroundDeltaCm)
		{
			OutReason = EIFPlacementInvalidReason::InvalidLocation;
			return false;
		}
		if (GroundHit.ImpactNormal.Z < MinGroundNormalZ)
		{
			OutReason = EIFPlacementInvalidReason::SlopeTooSteep;
			return false;
		}

		return true;
	}

	bool ValidateTerritoryAndFlag(
		const FIFPlacementValidationContext& Context,
		const AIFGameState* GS,
		const TArray<FIFWorldCollisionBox>& CandBoxes,
		AIFTerritory*& OutTerritory,
		EIFPlacementInvalidReason& OutReason)
	{
		const UIFStructureDataAsset* const Data = Context.Data;
		const FIFSnapResult& Candidate = *Context.Candidate;
		const uint8 BuilderTeamRaw = Context.BuilderTeamId.GetId();

		AIFTerritory* Territory = nullptr;
		for (const FIFWorldCollisionBox& Box : CandBoxes)
		{
			const FVector2D HorizHalf = GetEffectiveHorizontalHalfExtent(Box);
			AIFTerritory* const BoxTerr = GS->FindTerritoryContainingBox(Box.WorldCenter, HorizHalf, Box.WorldRotation.Yaw);
			if (!IsValid(BoxTerr) || BoxTerr->GetGenericTeamId().GetId() != BuilderTeamRaw)
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

		if (Context.bRequireHeroInTerritory)
		{
			if (!IsValid(Context.PlayerPawn) || !Territory->ContainsPoint2D(Context.PlayerPawn->GetActorLocation()))
			{
				OutReason = EIFPlacementInvalidReason::HeroNotInTerritory;
				return false;
			}
		}

		if (Data->StructureType == EIFStructureType::Keep && Territory->HasActiveKeep())
		{
			if (!IsValid(Context.RelocatingStructure)
				|| !Context.RelocatingStructure->IsKeep()
				|| Territory->GetActiveKeep() != Context.RelocatingStructure)
			{
				OutReason = EIFPlacementInvalidReason::KeepAlreadyExists;
				return false;
			}
		}

		if (const USphereComponent* const FlagZone = Territory->GetFlagCaptureZone())
		{
			const float FlagZ = FlagZone->GetComponentLocation().Z;
			for (const FIFWorldCollisionBox& Box : CandBoxes)
			{
				// Elevated walkways (Z ~237 cm) clear standing soldiers (~180 cm) and the capture flag underneath.
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

		OutTerritory = Territory;
		return true;
	}

	bool ValidateStructureOverlap(
		const FIFPlacementValidationContext& Context,
		TArrayView<const AIFStructure* const> AllAliveStructures,
		const TArray<FIFWorldCollisionBox>& CandBoxes,
		FIFSnapSolverScratch& Scratch,
		EIFPlacementInvalidReason& OutReason)
	{
		const UIFStructureDataAsset* const Data = Context.Data;
		const FIFSnapResult& Candidate = *Context.Candidate;

		const bool bCandIsWalkway = (Data->PlacementRule == EIFPlacementRule::WallWalkwayOnly);
		const bool bCandIsCornerWalkway = (Data->PlacementRule == EIFPlacementRule::CornerWalkwayOnly);
		const bool bCandIsGateDoor = (Data->PlacementRule == EIFPlacementRule::GateOpeningOnly);
		const float CandRadius = EstimateFootprintRadius2D(Data);

		for (const AIFStructure* const Existing : AllAliveStructures)
		{
			if (!IsValid(Existing) || Existing == Context.RelocatingStructure)
			{
				continue;
			}

			if ((bCandIsCornerWalkway || bCandIsGateDoor) && Existing == Candidate.HostStructure.Get())
			{
				continue;
			}

			const UIFStructureDataAsset* const ExistDA = Existing->GetStructureData();
			const float BroadphaseRadius = CandRadius + EstimateFootprintRadius2D(ExistDA) + 80.f;
			if (FVector::DistSquared2D(Candidate.Location, Existing->GetActorLocation()) > FMath::Square(BroadphaseRadius))
			{
				continue;
			}

			const bool bExistIsWalkway = IsValid(ExistDA) && (ExistDA->PlacementRule == EIFPlacementRule::WallWalkwayOnly);
			const bool bExistIsCornerWalkway = IsValid(ExistDA) && (ExistDA->PlacementRule == EIFPlacementRule::CornerWalkwayOnly);

			// CornerWalkway and WoodWalkway intentionally overlap at wall corners to bridge adjacent walkway runs with current modular meshes.
			if ((bCandIsCornerWalkway && bExistIsWalkway) || (bCandIsWalkway && bExistIsCornerWalkway))
			{
				continue;
			}

			if (bCandIsWalkway && bExistIsWalkway)
			{
				const float YawDiff = FMath::Abs(FRotator::NormalizeAxis(Candidate.YawDegrees - Existing->GetActorRotation().Yaw));
				const bool bIsMiterAngle = FMath::IsNearlyEqual(YawDiff, 45.f, 6.f) || FMath::IsNearlyEqual(YawDiff, 135.f, 6.f);
				if (bIsMiterAngle && FVector::DistSquared2D(Candidate.Location, Existing->GetActorLocation()) > FMath::Square(150.f))
				{
					continue;
				}
			}

			Scratch.ExistingBoxes.Reset();
			Existing->GetWorldCollisionBoxes(Scratch.ExistingBoxes);

			for (const FIFWorldCollisionBox& CB : CandBoxes)
			{
				for (const FIFWorldCollisionBox& EB : Scratch.ExistingBoxes)
				{
					if (DoesWorldBoxOverlap(CB, EB, /*XYInsetCm=*/3.0f, /*ZToleranceCm=*/2.0f))
					{
						OutReason = EIFPlacementInvalidReason::BlockedByObstacle;
						return false;
					}
				}
			}
		}

		return true;
	}

	bool ValidateWorldAndPawnClearance(
		const FIFPlacementValidationContext& Context,
		const TArray<FIFWorldCollisionBox>& CandBoxes,
		FIFSnapSolverScratch& Scratch,
		EIFPlacementInvalidReason& OutReason)
	{
		const FIFSnapResult& Candidate = *Context.Candidate;

		FCollisionQueryParams ObstacleQueryParams(SCENE_QUERY_STAT(IFBuildWorldObstacleOverlap), false);
		if (IsValid(Context.PlacementGhost))
		{
			ObstacleQueryParams.AddIgnoredActor(Context.PlacementGhost);
		}
		if (IsValid(Context.RelocatingStructure))
		{
			ObstacleQueryParams.AddIgnoredActor(Context.RelocatingStructure);
		}
		if (IsValid(Context.GroundActor))
		{
			ObstacleQueryParams.AddIgnoredActor(Context.GroundActor);
		}

		FCollisionObjectQueryParams WorldObstacleParams;
		WorldObstacleParams.AddObjectTypesToQuery(ECC_WorldStatic);
		WorldObstacleParams.AddObjectTypesToQuery(ECC_WorldDynamic);

		for (const FIFWorldCollisionBox& Box : CandBoxes)
		{
			FVector TestCenter = Box.WorldCenter;
			FVector TestHalf(
				FMath::Max(4.f, Box.HalfExtent.X - 3.f),
				FMath::Max(4.f, Box.HalfExtent.Y - 3.f),
				FMath::Max(4.f, Box.HalfExtent.Z));

			const float LocalMinZ = Box.WorldMinZ - Candidate.Location.Z;
			if (LocalMinZ < ObstacleGroundLiftCm)
			{
				const float LiftNeeded = ObstacleGroundLiftCm - FMath::Max(0.f, LocalMinZ);
				if (FMath::IsNearlyZero(Box.WorldRotation.Pitch, 0.5f) && FMath::IsNearlyZero(Box.WorldRotation.Roll, 0.5f))
				{
					const float NewMinZ = Box.WorldMinZ + LiftNeeded;
					if (Box.WorldMaxZ > NewMinZ + 4.f)
					{
						TestCenter.Z = 0.5f * (NewMinZ + Box.WorldMaxZ);
						TestHalf.Z = 0.5f * (Box.WorldMaxZ - NewMinZ);
					}
				}
				else
				{
					TestCenter.Z += LiftNeeded;
				}
			}

			Scratch.OverlapResults.Reset();
			if (Context.World->OverlapMultiByObjectType(
				Scratch.OverlapResults,
				TestCenter,
				Box.WorldRotation.Quaternion(),
				WorldObstacleParams,
				FCollisionShape::MakeBox(TestHalf),
				ObstacleQueryParams))
			{
				for (const FOverlapResult& Overlap : Scratch.OverlapResults)
				{
					const AActor* const HitActor = Overlap.GetActor();
					const UPrimitiveComponent* const HitComp = Overlap.GetComponent();
					if (!IsValid(HitActor) || !IsValid(HitComp))
					{
						continue;
					}
					if (HitActor->IsA<AIFStructure>() || HitActor->IsA<AIFTerritory>() || HitActor->IsA<AIFPlacementGhost>())
					{
						continue;
					}
					if (!HitComp->IsCollisionEnabled() || HitComp->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
					{
						continue;
					}

					OutReason = EIFPlacementInvalidReason::BlockedByObstacle;
					return false;
				}
			}
		}

		const FCollisionObjectQueryParams PawnObjects = IFCollision::MakePawnObjectQueryParams();
		FCollisionQueryParams PawnQueryParams(SCENE_QUERY_STAT(IFBuildPawnOverlap), false);
		if (IsValid(Context.PlacementGhost))
		{
			PawnQueryParams.AddIgnoredActor(Context.PlacementGhost);
		}
		if (IsValid(Context.RelocatingStructure))
		{
			PawnQueryParams.AddIgnoredActor(Context.RelocatingStructure);
		}

		for (const FIFWorldCollisionBox& Box : CandBoxes)
		{
			const bool bIsElevatedBox = (Box.WorldMinZ - Candidate.Location.Z) >= 180.f;
			FVector TestCenter = Box.WorldCenter;
			FVector TestExtent = Box.HalfExtent;

			if (!bIsElevatedBox)
			{
				TestExtent = FVector(Box.HalfExtent.X + 20.f, Box.HalfExtent.Y + 20.f, FMath::Max(Box.HalfExtent.Z, 80.f));
			}
			else
			{
				// Test elevated decks at their actual Z and upward so ground units underneath do not block while units on adjacent decks do.
				const float DeckMaxZ = FMath::Max(Box.WorldMaxZ, Box.WorldMinZ + 90.f);
				TestCenter.Z = 0.5f * (Box.WorldMinZ + DeckMaxZ);
				TestExtent = FVector(Box.HalfExtent.X, Box.HalfExtent.Y, 0.5f * (DeckMaxZ - Box.WorldMinZ));
			}

			Scratch.OverlapResults.Reset();
			if (Context.World->OverlapMultiByObjectType(
				Scratch.OverlapResults,
				TestCenter,
				Box.WorldRotation.Quaternion(),
				PawnObjects,
				FCollisionShape::MakeBox(TestExtent),
				PawnQueryParams))
			{
				for (const FOverlapResult& Overlap : Scratch.OverlapResults)
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

		return true;
	}
}

void FIFBuildingSnapSolver::GatherAliveStructures(
	const UWorld* World,
	FGenericTeamId BuilderTeamId,
	const AIFStructure* IgnoredStructure,
	TArray<const AIFStructure*>& OutAllAlive,
	TArray<const AIFStructure*>& OutFriendly)
{
	OutAllAlive.Reset();
	OutFriendly.Reset();

	if (!IsValid(World))
	{
		return;
	}

	const uint8 BuilderTeamRaw = BuilderTeamId.GetId();
	for (TActorIterator<AIFStructure> It(World); It; ++It)
	{
		const AIFStructure* const Struct = *It;
		if (!IsValid(Struct) || Struct == IgnoredStructure || !Struct->IsStructureAlive())
		{
			continue;
		}

		OutAllAlive.Add(Struct);
		if (Struct->GetGenericTeamId().GetId() == BuilderTeamRaw)
		{
			OutFriendly.Add(Struct);
		}
	}
}

FIFSnapResult FIFBuildingSnapSolver::SolvePlacementTransform(
	const FIFSnapQuery& Query,
	TArrayView<const AIFStructure* const> FriendlyStructures,
	FIFSnapSolverScratch& Scratch)
{
	FIFSnapResult Result;
	const float FreeYaw = FRotator::NormalizeAxis(static_cast<float>((Query.FreeYawIndex % 4 + 4) % 4) * 90.f);
	Result.Location = Query.CursorGroundLocation;
	Result.YawDegrees = FreeYaw;

	if (!IsValid(Query.World) || !IsValid(Query.Data))
	{
		return Result;
	}

	FIFSnapResult SnappedResult;
	switch (Query.Data->PlacementRule)
	{
	case EIFPlacementRule::FreeOrEndSnap:
		if (SolveEndSnap(Query, FriendlyStructures, Scratch, SnappedResult))
		{
			return SnappedResult;
		}
		return Result;

	case EIFPlacementRule::CornerWalkwayOnly:
		if (SolveCornerWalkwaySnap(Query, FriendlyStructures, SnappedResult))
		{
			return SnappedResult;
		}
		return Result;

	case EIFPlacementRule::GateOpeningOnly:
		if (SolveGateDoorSnap(Query, FriendlyStructures, SnappedResult))
		{
			return SnappedResult;
		}
		return Result;

	case EIFPlacementRule::WallWalkwayOnly:
		if (SolveWallWalkwaySnap(Query, FriendlyStructures, Scratch, SnappedResult))
		{
			return SnappedResult;
		}
		return Result;

	case EIFPlacementRule::StairMountOrFree:
		if (SolveStairSnap(Query, FriendlyStructures, FreeYaw, SnappedResult))
		{
			return SnappedResult;
		}
		return Result;

	default:
		return Result;
	}
}

bool FIFBuildingSnapSolver::ValidatePlacement(
	const FIFPlacementValidationContext& Context,
	TArrayView<const AIFStructure* const> AllAliveStructures,
	FIFSnapSolverScratch& Scratch,
	AIFTerritory*& OutTerritory,
	EIFPlacementInvalidReason& OutReason)
{
	OutTerritory = nullptr;
	OutReason = EIFPlacementInvalidReason::None;

	const AIFGameState* const GS = IsValid(Context.World) ? Context.World->GetGameState<AIFGameState>() : nullptr;
	if (!IsValid(Context.Data) || !IsValid(Context.World) || !IsValid(GS) || Context.Candidate == nullptr)
	{
		OutReason = EIFPlacementInvalidReason::InvalidLocation;
		return false;
	}

	if (!ValidateAttachmentRule(Context, AllAliveStructures, OutReason))
	{
		return false;
	}

	if (!ValidateGroundAndSlope(Context, OutReason))
	{
		return false;
	}

	Scratch.CandidateBoxes.Reset();
	AIFStructure::ComputeWorldCollisionBoxes(
		Context.Data,
		Context.Candidate->Location,
		Context.Candidate->YawDegrees,
		Scratch.CandidateBoxes);
	if (Scratch.CandidateBoxes.IsEmpty())
	{
		OutReason = EIFPlacementInvalidReason::InvalidLocation;
		return false;
	}

	AIFTerritory* Territory = nullptr;
	if (!ValidateTerritoryAndFlag(Context, GS, Scratch.CandidateBoxes, Territory, OutReason))
	{
		return false;
	}

	if (!ValidateStructureOverlap(Context, AllAliveStructures, Scratch.CandidateBoxes, Scratch, OutReason))
	{
		return false;
	}

	if (!ValidateWorldAndPawnClearance(Context, Scratch.CandidateBoxes, Scratch, OutReason))
	{
		return false;
	}

	OutTerritory = Territory;
	OutReason = EIFPlacementInvalidReason::None;
	return true;
}
