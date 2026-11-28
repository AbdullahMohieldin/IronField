#include "Tactical/IFFormationSolver.h"

#include "AI/Navigation/NavigationTypes.h"
#include "Engine/World.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

namespace
{
	constexpr float MinAcceptedSlotClearanceCm = 88.f;

	FVector ResolveFallbackForward(const FIFFormationSolveParams& Params, const FVector& ReferenceAnchor)
	{
		FVector Forward = ReferenceAnchor - Params.SquadCentroid;
		Forward.Z = 0.f;
		if (Forward.SizeSquared2D() >= FMath::Square(50.f) && Forward.Normalize())
		{
			return Forward;
		}

		Forward = Params.CameraForward;
		Forward.Z = 0.f;
		if (Forward.Normalize())
		{
			return Forward;
		}

		return FVector::ForwardVector;
	}

	FVector ComputeRightFromForward(const FVector& Forward)
	{
		// In Unreal's left-handed Z-up frame (+X forward, +Y right): Up x Forward = (-Fy, Fx, 0).
		return FVector(-Forward.Y, Forward.X, 0.f).GetSafeNormal2D();
	}

	FVector ComputeForwardFromRight(const FVector& Right)
	{
		// Dragging left-to-right across the viewport defines +Right, so Right x Up = (Ry, -Rx, 0) faces away from camera.
		return FVector(Right.Y, -Right.X, 0.f).GetSafeNormal2D();
	}

	void BuildCenterOutwardColumnOrder(int32 ColumnCount, TArray<int32, TInlineAllocator<32>>& OutColumns)
	{
		OutColumns.Reset();
		if (ColumnCount <= 0)
		{
			return;
		}

		OutColumns.Reserve(ColumnCount);
		const int32 LeftStart = (ColumnCount - 1) / 2;
		const int32 RightStart = LeftStart + 1;

		if (ColumnCount % 2 == 1)
		{
			OutColumns.Add(LeftStart);
			for (int32 Step = 1; OutColumns.Num() < ColumnCount; ++Step)
			{
				if (LeftStart - Step >= 0)
				{
					OutColumns.Add(LeftStart - Step);
				}
				if (LeftStart + Step < ColumnCount)
				{
					OutColumns.Add(LeftStart + Step);
				}
			}
		}
		else
		{
			for (int32 Step = 0; OutColumns.Num() < ColumnCount; ++Step)
			{
				if (LeftStart - Step >= 0)
				{
					OutColumns.Add(LeftStart - Step);
				}
				if (RightStart + Step < ColumnCount)
				{
					OutColumns.Add(RightStart + Step);
				}
			}
		}
	}

	void BuildLineCandidateLattice(
		const FIFFormationSolveParams& Params,
		const FVector& ProjectedAnchor,
		int32 MaxCandidates,
		TArray<FIFFormationSlot>& OutCandidates,
		FVector& OutCenter)
	{
		const int32 Count = Params.UnitCount;
		const float LateralSpacing = FMath::Max(Params.LateralSpacing, 86.f);
		const float RankSpacing = FMath::Max(Params.RankSpacing, 88.f);

		FVector FormationForward = ResolveFallbackForward(Params, ProjectedAnchor);
		FVector FormationRight = ComputeRightFromForward(FormationForward);
		FVector FrontRankCenter = ProjectedAnchor;

		const int32 MinCols = FMath::Min(Count, FMath::Max(1, Params.MinLineColumns));
		const int32 MaxCols = FMath::Max(MinCols, Params.MaxLineColumns);
		const float Aspect = FMath::Max(Params.LineAspectRatio, 1.f);
		int32 Columns = FMath::Clamp(FMath::RoundToInt(FMath::Sqrt(static_cast<float>(Count) * Aspect)), MinCols, MaxCols);
		Columns = FMath::Clamp(Columns, 1, Count);

		if (Params.bIsDrag)
		{
			FVector DragVec = Params.DragEndPoint - Params.AnchorPoint;
			DragVec.Z = 0.f;
			const float DragWidth = DragVec.Size2D();
			if (DragWidth >= 10.f)
			{
				FormationRight = DragVec.GetSafeNormal2D();
				FormationForward = ComputeForwardFromRight(FormationRight);
				FrontRankCenter = 0.5f * (Params.AnchorPoint + Params.DragEndPoint);
				FrontRankCenter.Z = ProjectedAnchor.Z;
				Columns = FMath::Clamp(FMath::FloorToInt(DragWidth / LateralSpacing) + 1, 1, Count);
			}
		}

		OutCenter = FrontRankCenter;
		const float FacingYaw = FormationForward.Rotation().Yaw;
		const float ColCenterOffset = 0.5f * static_cast<float>(Columns - 1);

		TArray<int32, TInlineAllocator<32>> ColOrder;
		BuildCenterOutwardColumnOrder(Columns, ColOrder);

		OutCandidates.Reserve(MaxCandidates);
		for (int32 RowIndex = 0; OutCandidates.Num() < MaxCandidates; ++RowIndex)
		{
			const FVector RowOrigin = FrontRankCenter - FormationForward * (static_cast<float>(RowIndex) * RankSpacing);
			for (const int32 ColIndex : ColOrder)
			{
				if (OutCandidates.Num() >= MaxCandidates)
				{
					break;
				}

				const float RightOffset = (static_cast<float>(ColIndex) - ColCenterOffset) * LateralSpacing;
				FIFFormationSlot Slot;
				Slot.Location = RowOrigin + FormationRight * RightOffset;
				Slot.FacingYaw = FacingYaw;
				OutCandidates.Add(Slot);
			}
		}
	}

	void BuildSquareCandidateLattice(
		const FIFFormationSolveParams& Params,
		const FVector& ProjectedAnchor,
		int32 MaxCandidates,
		TArray<FIFFormationSlot>& OutCandidates,
		FVector& OutCenter)
	{
		const int32 Count = Params.UnitCount;
		FVector Forward = ResolveFallbackForward(Params, ProjectedAnchor);

		if (Params.bIsDrag)
		{
			FVector DragVec = Params.DragEndPoint - Params.AnchorPoint;
			DragVec.Z = 0.f;
			if (DragVec.SizeSquared2D() >= FMath::Square(10.f))
			{
				Forward = DragVec.GetSafeNormal2D();
			}
		}

		const FVector Right = ComputeRightFromForward(Forward);
		OutCenter = ProjectedAnchor;

		if (Count == 1)
		{
			FIFFormationSlot Slot;
			Slot.Location = ProjectedAnchor;
			Slot.FacingYaw = Forward.Rotation().Yaw;
			OutCandidates.Add(Slot);
			return;
		}

		const float Scale = FMath::Clamp(Params.SquareSpacingScale, 0.80f, 1.25f);
		const float ColSpacing = FMath::Max(Params.LateralSpacing * Scale, 90.f);
		const float RowSpacing = FMath::Max(Params.RankSpacing * Scale, 92.f);
		const int32 Cols = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count))));
		const int32 BaseRows = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Count) / static_cast<float>(Cols)));
		const float RowCenterOffset = 0.5f * static_cast<float>(BaseRows - 1);
		const float ColCenterOffset = 0.5f * static_cast<float>(Cols - 1);

		TArray<int32, TInlineAllocator<32>> ColOrder;
		BuildCenterOutwardColumnOrder(Cols, ColOrder);

		OutCandidates.Reserve(MaxCandidates);
		for (int32 RowIndex = 0; OutCandidates.Num() < MaxCandidates; ++RowIndex)
		{
			const float ForwardOffset = (RowCenterOffset - static_cast<float>(RowIndex)) * RowSpacing;
			const bool bIsFrontRow = (RowIndex == 0);
			const bool bIsBackRow = (BaseRows >= 2 && RowIndex >= BaseRows - 1);

			for (const int32 ColIndex : ColOrder)
			{
				if (OutCandidates.Num() >= MaxCandidates)
				{
					break;
				}

				const float RightOffset = (static_cast<float>(ColIndex) - ColCenterOffset) * ColSpacing;
				const bool bIsLeftEdge = (Cols >= 2 && ColIndex == 0);
				const bool bIsRightEdge = (Cols >= 2 && ColIndex == Cols - 1);

				FVector OutwardDir = FVector::ZeroVector;
				if (bIsFrontRow)
				{
					OutwardDir += Forward;
				}
				else if (bIsBackRow)
				{
					OutwardDir -= Forward;
				}

				if (bIsLeftEdge && (Cols >= 3 || !bIsFrontRow))
				{
					OutwardDir -= Right;
				}
				else if (bIsRightEdge && (Cols >= 3 || !bIsFrontRow))
				{
					OutwardDir += Right;
				}

				OutwardDir = OutwardDir.IsNearlyZero() ? Forward : OutwardDir.GetSafeNormal2D();

				FIFFormationSlot Slot;
				Slot.Location = ProjectedAnchor + Forward * ForwardOffset + Right * RightOffset;
				Slot.FacingYaw = OutwardDir.Rotation().Yaw;
				OutCandidates.Add(Slot);
			}
		}
	}

	void AppendCircleRingCandidates(
		const FVector& Center,
		float Radius,
		float StartAngleRad,
		float LateralSpacing,
		int32 MaxToAppend,
		TArray<FIFFormationSlot>& OutCandidates)
	{
		if (MaxToAppend <= 0 || Radius <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		int32 RingCapacity = 1;
		if (2.f * Radius > LateralSpacing)
		{
			const float SinHalfAngle = FMath::Clamp(LateralSpacing / (2.f * Radius), 0.f, 1.f);
			RingCapacity = FMath::Max(1, FMath::FloorToInt(PI / FMath::Asin(SinHalfAngle)));
		}

		const int32 UnitsOnRing = FMath::Min(MaxToAppend, RingCapacity);
		const float AngleStep = (2.f * PI) / static_cast<float>(UnitsOnRing);

		for (int32 i = 0; i < UnitsOnRing; ++i)
		{
			const float Angle = StartAngleRad + static_cast<float>(i) * AngleStep;
			const FVector RadialDir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);

			FIFFormationSlot Slot;
			Slot.Location = Center + RadialDir * Radius;
			Slot.FacingYaw = RadialDir.Rotation().Yaw;
			OutCandidates.Add(Slot);
		}
	}

	void BuildCircleCandidateLattice(
		const FIFFormationSolveParams& Params,
		const FVector& ProjectedAnchor,
		int32 MaxCandidates,
		TArray<FIFFormationSlot>& OutCandidates,
		FVector& OutCenter)
	{
		const int32 Count = Params.UnitCount;
		const float LateralSpacing = FMath::Max(Params.LateralSpacing, 86.f);
		const float RankSpacing = FMath::Max(Params.RankSpacing, 88.f);
		const float MinRadius = FMath::Max(Params.MinShapeRadius, LateralSpacing * 0.85f);
		const float MaxRadius = FMath::Max(MinRadius, Params.MaxShapeRadius);

		OutCenter = ProjectedAnchor;
		const FVector FallbackForward = ResolveFallbackForward(Params, ProjectedAnchor);

		if (Count == 1)
		{
			FIFFormationSlot Slot;
			Slot.Location = ProjectedAnchor;
			Slot.FacingYaw = FallbackForward.Rotation().Yaw;
			OutCandidates.Add(Slot);
			return;
		}

		float BaseRadius = MinRadius;
		float StartAngleRad = FMath::DegreesToRadians(FallbackForward.Rotation().Yaw);

		if (Params.bIsDrag)
		{
			FVector DragVec = Params.DragEndPoint - Params.AnchorPoint;
			DragVec.Z = 0.f;
			BaseRadius = FMath::Clamp(DragVec.Size2D(), MinRadius, MaxRadius);
			if (DragVec.SizeSquared2D() >= FMath::Square(10.f))
			{
				StartAngleRad = FMath::Atan2(DragVec.Y, DragVec.X);
			}
		}
		else
		{
			const float OuterShare = (Count <= 16) ? static_cast<float>(Count) : (static_cast<float>(Count) * 0.62f);
			const float DesiredRadius = (OuterShare * LateralSpacing) / (2.f * PI);
			BaseRadius = FMath::Clamp(DesiredRadius, MinRadius, MaxRadius);
		}

		OutCandidates.Reserve(MaxCandidates);
		int32 PrimaryBudget = Count;
		float CurrentRadius = BaseRadius;
		float InnermostPlacedRadius = BaseRadius;

		while (PrimaryBudget > 0 && CurrentRadius >= LateralSpacing * 0.85f)
		{
			const int32 PrevNum = OutCandidates.Num();
			AppendCircleRingCandidates(ProjectedAnchor, CurrentRadius, StartAngleRad, LateralSpacing, PrimaryBudget, OutCandidates);
			PrimaryBudget -= (OutCandidates.Num() - PrevNum);
			InnermostPlacedRadius = CurrentRadius;
			CurrentRadius -= RankSpacing;
		}

		if (PrimaryBudget > 0 && InnermostPlacedRadius >= LateralSpacing * 0.90f)
		{
			FIFFormationSlot CenterSlot;
			CenterSlot.Location = ProjectedAnchor;
			CenterSlot.FacingYaw = FallbackForward.Rotation().Yaw;
			OutCandidates.Add(CenterSlot);
		}

		// Concentric outer overflow rings absorb units when inner slots hit a flag pole or DragRadius is small.
		float OverflowRadius = BaseRadius + RankSpacing;
		int32 RingStep = 0;
		while (OutCandidates.Num() < MaxCandidates)
		{
			const float StaggerAngle = StartAngleRad + ((RingStep % 2 == 1) ? (PI / 12.f) : 0.f);
			int32 FullRingCap = 1;
			if (2.f * OverflowRadius > LateralSpacing)
			{
				const float SinHalf = FMath::Clamp(LateralSpacing / (2.f * OverflowRadius), 0.f, 1.f);
				FullRingCap = FMath::Max(1, FMath::FloorToInt(PI / FMath::Asin(SinHalf)));
			}
			AppendCircleRingCandidates(
				ProjectedAnchor,
				OverflowRadius,
				StaggerAngle,
				LateralSpacing,
				FMath::Min(FullRingCap, MaxCandidates - OutCandidates.Num()),
				OutCandidates);
			OverflowRadius += RankSpacing;
			++RingStep;
		}
	}

	bool HasSufficientSlotClearance(const FVector& CandidateLoc, const TArray<FIFFormationSlot>& AcceptedSlots, float MinClearanceSq)
	{
		for (const FIFFormationSlot& Existing : AcceptedSlots)
		{
			if (FVector::DistSquared2D(CandidateLoc, Existing.Location) < MinClearanceSq)
			{
				return false;
			}
		}
		return true;
	}

	bool IsSlotConnectedToAnchor(
		UWorld* World,
		UNavigationSystemV1& NavSys,
		const ANavigationData* NavData,
		const FVector& NavAnchor,
		const FVector& NavSlot)
	{
		FVector HitLocation = FVector::ZeroVector;
		if (!UNavigationSystemV1::NavigationRaycast(World, NavAnchor, NavSlot, HitLocation))
		{
			return true;
		}

		// Bounded path-length fallback allows Circles to wrap around narrow interior obstacles (e.g. flag poles)
		// while rejecting slots that project across castle walls, closed gates, or disconnected upper walkways.
		if (!IsValid(NavData))
		{
			return false;
		}

		const FPathFindingQuery Query(
			World,
			*NavData,
			NavAnchor,
			NavSlot,
			UNavigationQueryFilter::GetQueryFilter(*NavData, World, nullptr));
		const FPathFindingResult Result = NavSys.FindPathSync(Query, EPathFindingMode::Regular);
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
		{
			return false;
		}

		const float DirectDist2D = FVector::Dist2D(NavAnchor, NavSlot);
		const float MaxAllowedPathLength = FMath::Max(DirectDist2D * 2.0f + 160.f, 450.f);
		return Result.Path->GetLength() <= MaxAllowedPathLength;
	}
}

bool FIFFormationSolver::SolveFormation(
	UWorld* World,
	const FIFFormationSolveParams& Params,
	TArray<FIFFormationSlot>& OutSlots,
	FVector& OutFormationCenter)
{
	OutSlots.Reset();
	if (Params.UnitCount <= 0)
	{
		OutFormationCenter = Params.AnchorPoint;
		return false;
	}

	UNavigationSystemV1* const NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FVector ProjectedAnchor = Params.AnchorPoint;
	if (IsValid(NavSys))
	{
		FNavLocation NavAnchor;
		if (NavSys->ProjectPointToNavigation(Params.AnchorPoint, NavAnchor, FVector(250.f, 250.f, 400.f)))
		{
			ProjectedAnchor = NavAnchor.Location;
		}
	}

	const int32 MaxCandidates = (Params.UnitCount == 1) ? 1 : (Params.UnitCount * 2 + 16);
	TArray<FIFFormationSlot> Candidates;

	switch (Params.FormationType)
	{
	case EIFTacticalFormationType::Square:
		BuildSquareCandidateLattice(Params, ProjectedAnchor, MaxCandidates, Candidates, OutFormationCenter);
		break;
	case EIFTacticalFormationType::Circle:
		BuildCircleCandidateLattice(Params, ProjectedAnchor, MaxCandidates, Candidates, OutFormationCenter);
		break;
	case EIFTacticalFormationType::Line:
	default:
		BuildLineCandidateLattice(Params, ProjectedAnchor, MaxCandidates, Candidates, OutFormationCenter);
		break;
	}

	if (!IsValid(World) || !IsValid(NavSys))
	{
		const int32 CopyCount = FMath::Min(Params.UnitCount, Candidates.Num());
		OutSlots.Append(Candidates.GetData(), CopyCount);
		return !OutSlots.IsEmpty();
	}

	FNavLocation ProjectedCenter;
	if (NavSys->ProjectPointToNavigation(OutFormationCenter, ProjectedCenter, FVector(250.f, 250.f, 400.f)))
	{
		OutFormationCenter = ProjectedCenter.Location;
	}
	else
	{
		OutFormationCenter = ProjectedAnchor;
	}

	const ANavigationData* const NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	const float MinClearanceSq = FMath::Square(MinAcceptedSlotClearanceCm);
	const float FallbackFacingYaw = Candidates.IsEmpty() ? 0.f : Candidates[0].FacingYaw;

	OutSlots.Reserve(Params.UnitCount);
	for (const FIFFormationSlot& Candidate : Candidates)
	{
		if (OutSlots.Num() >= Params.UnitCount)
		{
			break;
		}

		FNavLocation NavSlot;
		if (!NavSys->ProjectPointToNavigation(Candidate.Location, NavSlot, Params.NavQueryExtent))
		{
			continue;
		}

		if (!HasSufficientSlotClearance(NavSlot.Location, OutSlots, MinClearanceSq))
		{
			continue;
		}

		if (!IsSlotConnectedToAnchor(World, *NavSys, NavData, OutFormationCenter, NavSlot.Location))
		{
			continue;
		}

		FIFFormationSlot AcceptedSlot;
		AcceptedSlot.Location = NavSlot.Location;
		AcceptedSlot.FacingYaw = Candidate.FacingYaw;
		OutSlots.Add(AcceptedSlot);
	}

	// Compact spiral fallback only if the entire squad was ordered inside a cramped enclosure.
	if (OutSlots.Num() < Params.UnitCount)
	{
		constexpr float GoldenAngleRad = 2.39996323f;
		const int32 MaxSpiralSamples = Params.UnitCount * 6 + 16;
		for (int32 SampleIdx = 0; SampleIdx < MaxSpiralSamples && OutSlots.Num() < Params.UnitCount; ++SampleIdx)
		{
			const float Radius = (SampleIdx == 0) ? 0.f : (MinAcceptedSlotClearanceCm * 0.65f * FMath::Sqrt(static_cast<float>(SampleIdx)));
			const float Angle = static_cast<float>(SampleIdx) * GoldenAngleRad;
			const FVector SamplePos = OutFormationCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;

			FNavLocation NavSlot;
			if (!NavSys->ProjectPointToNavigation(SamplePos, NavSlot, Params.NavQueryExtent))
			{
				continue;
			}
			if (OutSlots.Num() > 0 && !HasSufficientSlotClearance(NavSlot.Location, OutSlots, FMath::Square(72.f)))
			{
				continue;
			}
			if (!IsSlotConnectedToAnchor(World, *NavSys, NavData, OutFormationCenter, NavSlot.Location))
			{
				continue;
			}

			FIFFormationSlot FallbackSlot;
			FallbackSlot.Location = NavSlot.Location;
			FallbackSlot.FacingYaw = FallbackFacingYaw;
			OutSlots.Add(FallbackSlot);
		}

		while (OutSlots.Num() < Params.UnitCount)
		{
			FIFFormationSlot AnchorFallback;
			AnchorFallback.Location = OutFormationCenter;
			AnchorFallback.FacingYaw = FallbackFacingYaw;
			OutSlots.Add(AnchorFallback);
		}
	}

	return !OutSlots.IsEmpty();
}

void FIFFormationSolver::AssignSoldiersToSlots(
	const TArray<FVector>& SoldierLocations,
	const TArray<FIFFormationSlot>& Slots,
	TArray<int32>& OutSoldierToSlotIndices)
{
	const int32 N = FMath::Min(SoldierLocations.Num(), Slots.Num());
	OutSoldierToSlotIndices.Reset(N);
	if (N <= 0)
	{
		return;
	}

	OutSoldierToSlotIndices.SetNumZeroed(N);
	if (N == 1)
	{
		OutSoldierToSlotIndices[0] = 0;
		return;
	}

	// 1-indexed Kuhn-Munkres (Hungarian) potential solver in double precision.
	// Minimizing squared 2D Euclidean distance is strictly convex, eliminating both crossing paths and collinear rank inversions.
	TArray<double> U;
	TArray<double> V;
	TArray<int32> P;
	TArray<int32> Way;
	TArray<double> MinV;
	TArray<bool> Used;

	U.Init(0.0, N + 1);
	V.Init(0.0, N + 1);
	P.Init(0, N + 1);
	Way.Init(0, N + 1);
	MinV.SetNumUninitialized(N + 1);
	Used.SetNumUninitialized(N + 1);

	for (int32 Row = 1; Row <= N; ++Row)
	{
		P[0] = Row;
		int32 Col0 = 0;
		for (int32 j = 0; j <= N; ++j)
		{
			MinV[j] = TNumericLimits<double>::Max();
			Used[j] = false;
		}

		do
		{
			Used[Col0] = true;
			const int32 CurRow = P[Col0];
			const FVector& SoldierPos = SoldierLocations[CurRow - 1];
			double Delta = TNumericLimits<double>::Max();
			int32 Col1 = 0;

			for (int32 j = 1; j <= N; ++j)
			{
				if (Used[j])
				{
					continue;
				}

				const FVector& SlotPos = Slots[j - 1].Location;
				const double DX = static_cast<double>(SoldierPos.X) - static_cast<double>(SlotPos.X);
				const double DY = static_cast<double>(SoldierPos.Y) - static_cast<double>(SlotPos.Y);
				const double Cost = (DX * DX + DY * DY) - U[CurRow] - V[j];

				if (Cost < MinV[j])
				{
					MinV[j] = Cost;
					Way[j] = Col0;
				}
				if (MinV[j] < Delta)
				{
					Delta = MinV[j];
					Col1 = j;
				}
			}

			for (int32 j = 0; j <= N; ++j)
			{
				if (Used[j])
				{
					U[P[j]] += Delta;
					V[j] -= Delta;
				}
				else
				{
					MinV[j] -= Delta;
				}
			}

			Col0 = Col1;
		}
		while (P[Col0] != 0);

		do
		{
			const int32 Col1 = Way[Col0];
			P[Col0] = P[Col1];
			Col0 = Col1;
		}
		while (Col0 != 0);
	}

	for (int32 Col = 1; Col <= N; ++Col)
	{
		if (P[Col] >= 1 && P[Col] <= N)
		{
			OutSoldierToSlotIndices[P[Col] - 1] = Col - 1;
		}
	}
}
