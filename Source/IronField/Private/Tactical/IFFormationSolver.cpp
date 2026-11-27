#include "Tactical/IFFormationSolver.h"

#include "NavigationSystem.h"

namespace
{
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

	void BuildLineSlots(const FIFFormationSolveParams& Params, const FVector& ProjectedAnchor, TArray<FIFFormationSlot>& OutSlots, FVector& OutCenter)
	{
		const int32 Count = Params.UnitCount;
		const float LateralSpacing = FMath::Max(Params.LateralSpacing, 80.f);
		const float RankSpacing = FMath::Max(Params.RankSpacing, 80.f);

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

		const int32 Rows = FMath::CeilToInt(static_cast<float>(Count) / static_cast<float>(Columns));
		const float FacingYaw = FormationForward.Rotation().Yaw;

		OutSlots.Reserve(Count);
		for (int32 RowIndex = 0; RowIndex < Rows; ++RowIndex)
		{
			const int32 RowStart = RowIndex * Columns;
			const int32 RowCount = FMath::Min(Columns, Count - RowStart);
			const float ColCenterOffset = 0.5f * static_cast<float>(RowCount - 1);
			const FVector RowOrigin = FrontRankCenter - FormationForward * (static_cast<float>(RowIndex) * RankSpacing);

			for (int32 ColIndex = 0; ColIndex < RowCount; ++ColIndex)
			{
				const float RightOffset = (static_cast<float>(ColIndex) - ColCenterOffset) * LateralSpacing;

				FIFFormationSlot Slot;
				Slot.Location = RowOrigin + FormationRight * RightOffset;
				Slot.FacingYaw = FacingYaw;
				OutSlots.Add(Slot);
			}
		}
	}

	void AppendCircleRingSlots(
		const FVector& Center,
		float Radius,
		float StartAngleRad,
		float LateralSpacing,
		int32& InOutRemaining,
		TArray<FIFFormationSlot>& OutSlots)
	{
		if (InOutRemaining <= 0 || Radius <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		int32 RingCapacity = 1;
		if (2.f * Radius > LateralSpacing)
		{
			const float SinHalfAngle = FMath::Clamp(LateralSpacing / (2.f * Radius), 0.f, 1.f);
			RingCapacity = FMath::Max(1, FMath::FloorToInt(PI / FMath::Asin(SinHalfAngle)));
		}
		const int32 UnitsOnRing = FMath::Min(InOutRemaining, RingCapacity);
		const float AngleStep = (2.f * PI) / static_cast<float>(UnitsOnRing);

		for (int32 i = 0; i < UnitsOnRing; ++i)
		{
			const float Angle = StartAngleRad + static_cast<float>(i) * AngleStep;
			const FVector RadialDir(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);

			FIFFormationSlot Slot;
			Slot.Location = Center + RadialDir * Radius;
			Slot.FacingYaw = RadialDir.Rotation().Yaw;
			OutSlots.Add(Slot);
		}

		InOutRemaining -= UnitsOnRing;
	}

	void BuildCircleSlots(const FIFFormationSolveParams& Params, const FVector& ProjectedAnchor, TArray<FIFFormationSlot>& OutSlots, FVector& OutCenter)
	{
		const int32 Count = Params.UnitCount;
		const float LateralSpacing = FMath::Max(Params.LateralSpacing, 80.f);
		const float RankSpacing = FMath::Max(Params.RankSpacing, 80.f);
		const float MinRadius = FMath::Max(Params.MinShapeRadius, LateralSpacing * 0.75f);
		const float MaxRadius = FMath::Max(MinRadius, Params.MaxShapeRadius);

		OutCenter = ProjectedAnchor;
		const FVector FallbackForward = ResolveFallbackForward(Params, ProjectedAnchor);

		if (Count == 1)
		{
			FIFFormationSlot Slot;
			Slot.Location = ProjectedAnchor;
			Slot.FacingYaw = FallbackForward.Rotation().Yaw;
			OutSlots.Add(Slot);
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

		OutSlots.Reserve(Count);
		int32 Remaining = Count;

		float CurrentRadius = BaseRadius;
		float InnermostPlacedRadius = BaseRadius;
		while (Remaining > 0 && CurrentRadius >= LateralSpacing * 0.75f)
		{
			AppendCircleRingSlots(ProjectedAnchor, CurrentRadius, StartAngleRad, LateralSpacing, Remaining, OutSlots);
			InnermostPlacedRadius = CurrentRadius;
			CurrentRadius -= RankSpacing;
		}

		if (Remaining > 0 && InnermostPlacedRadius >= LateralSpacing * 0.85f)
		{
			FIFFormationSlot CenterSlot;
			CenterSlot.Location = ProjectedAnchor;
			CenterSlot.FacingYaw = FallbackForward.Rotation().Yaw;
			OutSlots.Add(CenterSlot);
			--Remaining;
		}

		// Expand outward if the dragged radius was too tight to hold every unit inside.
		float OverflowRadius = BaseRadius + RankSpacing;
		while (Remaining > 0)
		{
			AppendCircleRingSlots(ProjectedAnchor, OverflowRadius, StartAngleRad, LateralSpacing, Remaining, OutSlots);
			OverflowRadius += RankSpacing;
		}
	}

	void BuildSquareSlots(const FIFFormationSolveParams& Params, const FVector& ProjectedAnchor, TArray<FIFFormationSlot>& OutSlots, FVector& OutCenter)
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
			OutSlots.Add(Slot);
			return;
		}

		// Tight shoulder-to-shoulder Roman infantry square (filled Cols x Rows block with outward perimeter facing).
		const float Scale = FMath::Clamp(Params.SquareSpacingScale, 0.75f, 1.25f);
		const float ColSpacing = FMath::Max(Params.LateralSpacing * Scale, 125.f);
		const float RowSpacing = FMath::Max(Params.RankSpacing * Scale, 130.f);
		const int32 Cols = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count))));
		const int32 Rows = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Count) / static_cast<float>(Cols)));
		const float RowCenterOffset = 0.5f * static_cast<float>(Rows - 1);

		OutSlots.Reserve(Count);
		for (int32 RowIndex = 0; RowIndex < Rows; ++RowIndex)
		{
			const int32 RowStart = RowIndex * Cols;
			const int32 RowCount = FMath::Min(Cols, Count - RowStart);
			const float ColCenterOffset = 0.5f * static_cast<float>(RowCount - 1);
			const float ForwardOffset = (RowCenterOffset - static_cast<float>(RowIndex)) * RowSpacing;

			for (int32 ColIndex = 0; ColIndex < RowCount; ++ColIndex)
			{
				const float RightOffset = (static_cast<float>(ColIndex) - ColCenterOffset) * ColSpacing;

				const bool bIsFrontRow = (RowIndex == 0);
				const bool bIsBackRow = (Rows >= 2 && RowIndex == Rows - 1);
				const bool bIsLeftEdge = (RowCount >= 2 && ColIndex == 0);
				const bool bIsRightEdge = (RowCount >= 2 && ColIndex == RowCount - 1);

				FVector OutwardDir = FVector::ZeroVector;
				if (bIsFrontRow)
				{
					OutwardDir += Forward;
				}
				else if (bIsBackRow)
				{
					OutwardDir -= Forward;
				}

				if (bIsLeftEdge && (RowCount >= 3 || !bIsFrontRow))
				{
					OutwardDir -= Right;
				}
				else if (bIsRightEdge && (RowCount >= 3 || !bIsFrontRow))
				{
					OutwardDir += Right;
				}

				if (OutwardDir.IsNearlyZero())
				{
					OutwardDir = Forward;
				}
				else
				{
					OutwardDir = OutwardDir.GetSafeNormal2D();
				}

				FIFFormationSlot Slot;
				Slot.Location = ProjectedAnchor + Forward * ForwardOffset + Right * RightOffset;
				Slot.FacingYaw = OutwardDir.Rotation().Yaw;
				OutSlots.Add(Slot);
			}
		}
	}
}

bool FIFFormationSolver::SolveFormation(
	const FIFFormationSolveParams& Params,
	const UNavigationSystemV1* NavSys,
	TArray<FIFFormationSlot>& OutSlots,
	FVector& OutFormationCenter)
{
	OutSlots.Reset();
	if (Params.UnitCount <= 0)
	{
		OutFormationCenter = Params.AnchorPoint;
		return false;
	}

	FVector ProjectedAnchor = Params.AnchorPoint;
	if (IsValid(NavSys))
	{
		FNavLocation NavAnchor;
		if (NavSys->ProjectPointToNavigation(Params.AnchorPoint, NavAnchor, FVector(250.f, 250.f, 400.f)))
		{
			ProjectedAnchor = NavAnchor.Location;
		}
	}

	switch (Params.FormationType)
	{
	case EIFTacticalFormationType::Square:
		BuildSquareSlots(Params, ProjectedAnchor, OutSlots, OutFormationCenter);
		break;
	case EIFTacticalFormationType::Circle:
		BuildCircleSlots(Params, ProjectedAnchor, OutSlots, OutFormationCenter);
		break;
	case EIFTacticalFormationType::Line:
	default:
		BuildLineSlots(Params, ProjectedAnchor, OutSlots, OutFormationCenter);
		break;
	}

	if (IsValid(NavSys))
	{
		FNavLocation ProjectedCenter;
		if (NavSys->ProjectPointToNavigation(OutFormationCenter, ProjectedCenter, FVector(250.f, 250.f, 400.f)))
		{
			OutFormationCenter = ProjectedCenter.Location;
		}

		const float MinSlotClearance = FMath::Max(Params.LateralSpacing, 80.f) * 0.85f;
		const float MinSlotClearanceSq = FMath::Square(MinSlotClearance);

		auto GetMinDistSqToEarlier = [&OutSlots](const FVector& Candidate, int32 UpToIndex) -> float
		{
			float MinDistSq = TNumericLimits<float>::Max();
			for (int32 PrevIdx = 0; PrevIdx < UpToIndex; ++PrevIdx)
			{
				MinDistSq = FMath::Min(MinDistSq, FVector::DistSquared2D(Candidate, OutSlots[PrevIdx].Location));
			}
			return MinDistSq;
		};

		for (int32 SlotIdx = 0; SlotIdx < OutSlots.Num(); ++SlotIdx)
		{
			FIFFormationSlot& Slot = OutSlots[SlotIdx];
			FNavLocation NavSlot;
			if (NavSys->ProjectPointToNavigation(Slot.Location, NavSlot, Params.NavQueryExtent))
			{
				Slot.Location = NavSlot.Location;
			}
			else
			{
				// Step inward toward OutFormationCenter along the ray before falling back so blocked slots don't pile up on one point.
				bool bResolved = false;
				constexpr float StepAlphas[] = { 0.75f, 0.50f, 0.25f };
				for (const float Alpha : StepAlphas)
				{
					const FVector Sample = FMath::Lerp(OutFormationCenter, Slot.Location, Alpha);
					if (NavSys->ProjectPointToNavigation(Sample, NavSlot, Params.NavQueryExtent))
					{
						Slot.Location = NavSlot.Location;
						bResolved = true;
						break;
					}
				}

				if (!bResolved)
				{
					const float Angle = static_cast<float>(SlotIdx) * (2.f * PI / 8.f);
					const float RingRadius = 45.f * static_cast<float>(1 + (SlotIdx / 8));
					const FVector RingSample = OutFormationCenter + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * RingRadius;
					Slot.Location = NavSys->ProjectPointToNavigation(RingSample, NavSlot, Params.NavQueryExtent)
						? NavSlot.Location
						: OutFormationCenter;
				}
			}

			if (SlotIdx > 0 && GetMinDistSqToEarlier(Slot.Location, SlotIdx) < MinSlotClearanceSq)
			{
				FVector BestCandidate = Slot.Location;
				float BestClearanceSq = GetMinDistSqToEarlier(BestCandidate, SlotIdx);
				bool bFoundClearSlot = false;

				constexpr int32 MaxRings = 4;
				constexpr int32 AngularSamples = 12;
				for (int32 Ring = 1; Ring <= MaxRings && !bFoundClearSlot; ++Ring)
				{
					const float SearchRadius = MinSlotClearance * 0.6f * static_cast<float>(Ring);
					for (int32 SampleIdx = 0; SampleIdx < AngularSamples; ++SampleIdx)
					{
						const float Angle = (static_cast<float>(SampleIdx) / static_cast<float>(AngularSamples)) * 2.f * PI;
						const FVector CandidateSample = Slot.Location + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * SearchRadius;
						if (NavSys->ProjectPointToNavigation(CandidateSample, NavSlot, Params.NavQueryExtent))
						{
							const float ClearanceSq = GetMinDistSqToEarlier(NavSlot.Location, SlotIdx);
							if (ClearanceSq >= MinSlotClearanceSq)
							{
								BestCandidate = NavSlot.Location;
								bFoundClearSlot = true;
								break;
							}
							if (ClearanceSq > BestClearanceSq)
							{
								BestClearanceSq = ClearanceSq;
								BestCandidate = NavSlot.Location;
							}
						}
					}
				}

				Slot.Location = BestCandidate;
			}
		}
	}

	return !OutSlots.IsEmpty();
}

void FIFFormationSolver::AssignSoldiersToSlots(
	const TArray<FVector>& SoldierLocations,
	const TArray<FIFFormationSlot>& Slots,
	TArray<int32>& OutSoldierToSlotIndices)
{
	const int32 Count = FMath::Min(SoldierLocations.Num(), Slots.Num());
	OutSoldierToSlotIndices.Reset(Count);
	if (Count <= 0)
	{
		return;
	}

	OutSoldierToSlotIndices.SetNum(Count);

	TArray<bool> SlotTaken;
	SlotTaken.Init(false, Count);

	for (int32 SoldierIdx = 0; SoldierIdx < Count; ++SoldierIdx)
	{
		int32 BestSlot = 0;
		float BestDistSq = TNumericLimits<float>::Max();

		for (int32 SlotIdx = 0; SlotIdx < Count; ++SlotIdx)
		{
			if (SlotTaken[SlotIdx])
			{
				continue;
			}

			const float DistSq = FVector::DistSquared2D(SoldierLocations[SoldierIdx], Slots[SlotIdx].Location);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestSlot = SlotIdx;
			}
		}

		SlotTaken[BestSlot] = true;
		OutSoldierToSlotIndices[SoldierIdx] = BestSlot;
	}

	// Minimize unsquared 2D distance so every crossing pair of paths is strictly uncrossed by the triangle inequality.
	constexpr int32 MaxSwapPasses = 16;
	for (int32 Pass = 0; Pass < MaxSwapPasses; ++Pass)
	{
		bool bSwapped = false;
		for (int32 i = 0; i < Count - 1; ++i)
		{
			for (int32 j = i + 1; j < Count; ++j)
			{
				const int32 SlotI = OutSoldierToSlotIndices[i];
				const int32 SlotJ = OutSoldierToSlotIndices[j];

				const float CurrentCost =
					FVector::Dist2D(SoldierLocations[i], Slots[SlotI].Location) +
					FVector::Dist2D(SoldierLocations[j], Slots[SlotJ].Location);

				const float SwappedCost =
					FVector::Dist2D(SoldierLocations[i], Slots[SlotJ].Location) +
					FVector::Dist2D(SoldierLocations[j], Slots[SlotI].Location);

				if (SwappedCost + 0.1f < CurrentCost)
				{
					OutSoldierToSlotIndices[i] = SlotJ;
					OutSoldierToSlotIndices[j] = SlotI;
					bSwapped = true;
				}
			}
		}

		if (!bSwapped)
		{
			break;
		}
	}
}
