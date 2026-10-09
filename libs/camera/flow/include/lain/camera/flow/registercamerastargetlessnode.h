#pragma once

#include "lain/camera/flow/rigregistrationnode.h"

#include <lain/camera/feature/extraction.h>
#include <lain/flow/evaluation.h>

namespace lain::camera
{
	// Fixed-camera targetless registration (registration::targetless::registerCameras) over a rig's
	// footage and models (RigRegistrationNode: paired by position, named by source, grouped by
	// position): the cameras placed from the static scene they share, with no board. Without metric
	// evidence the result is scale-ambiguous, normalised so the median depth of the landmarks the
	// reference sees is 1. Always a `report`, successful or failed.
	//
	// ONE node for the whole method, extraction included, as registerCameras is for detection: which
	// features are static, which tracks fit and which validate, and what the verdict is held to are
	// decisions taken together. A node of its own for extraction waits for a second consumer
	// (targetless calibration, M9 slice 4).
	//
	// The extraction settings are the ones that describe the rig and its footage:
	// - `longestSide`, the scale each frame is searched at, 0 for native;
	// - `sampledGroups`, how many capture groups every camera samples, spread across the capture;
	// - `featureCap`, the strongest features of a frame kept;
	// - `matchSearch` and `matchRatio`, how features are matched (Exact is the same on every run);
	// - `inlierAngleMrad`, how far a verified match's rays may be from where its pair's pose puts
	//   them, in milliradians;
	// - `minimumPairInliers`, the inliers a camera pair needs to verify.
	// The pre-screen, the static-feature tests and the localisation per unit of size stay at
	// feature::ExtractionRequest's defaults: they describe the extractor, not the rig.
	//
	// There is no `pixelSigma`: every observation extracted from footage carries its own covariance,
	// so a default noise model would apply to none. `seed` drives the bootstrap's draws; the geometry
	// solver's seed stays at its default. `deterministic` governs the extraction too, and refuses an
	// Approximate search, which is reproducible only within a tolerance.
	class RegisterCamerasTargetlessNode : public RigRegistrationNode
	{
	public:
		RegisterCamerasTargetlessNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<RegisterCamerasTargetlessNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		// The extraction those settings ask for, its execution policy the registration's.
		feature::ExtractionRequest extraction() const;

		flow::PortId m_longestSide;		   // "longestSide" (int, 0 = native)
		flow::PortId m_sampledGroups;	   // "sampledGroups" (int)
		flow::PortId m_featureCap;		   // "featureCap" (int)
		flow::PortId m_matchSearch;		   // "matchSearch" (feature::MatchSearch)
		flow::PortId m_matchRatio;		   // "matchRatio" (float)
		flow::PortId m_inlierAngleMrad;	   // "inlierAngleMrad" (float, milliradians)
		flow::PortId m_minimumPairInliers; // "minimumPairInliers" (int)
		flow::PortId m_report;			   // "report" (registration::Report)
	};
} // namespace lain::camera
