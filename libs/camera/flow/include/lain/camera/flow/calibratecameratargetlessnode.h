#pragma once

#include "lain/camera/flow/calibrationnode.h"

#include <lain/camera/calibration/report.h>
#include <lain/camera/calibration/targetlesssettings.h>
#include <lain/flow/evaluation.h>

namespace lain::camera
{
	// Targetless calibration over footage (calibration::targetless::calibrate): one moving camera's
	// model estimated from the static scene its footage sees, with no board. Extracts feature tracks
	// across the sampled frames, reconstructs from them, adjusts the model, validates on held-out
	// frames and resamples, and gives a verdict. Always a `report`, successful or failed.
	//
	// ONE node for the whole method, extraction included, as calibrateCamera is for detection: where
	// the reconstruction starts, which frames validate, what an outlier is and what the verdict is held
	// to are decisions taken together. The camera is named by its footage's source.
	//
	// What footage calibrates, and what a Ready verdict does not see, is targetless.h's operator
	// guidance: the camera translates as well as turns, does not keep one point centred, and sees a
	// wide field; and a Ready model can still be a few pixels out (an HD lens seeing no board was
	// Ready 1.5 to 3.6 px out).
	//
	// `imported` takes part through `importedPolicy` (CalibrationNode). The extraction settings are
	// the ones that describe the footage:
	// - `longestSide`, the scale each frame is searched at, 0 for native;
	// - `sampledFrames`, how many frames are sampled, spread evenly across the footage;
	// - `featureCap`, the strongest features of a frame kept;
	// - `matchSearch` and `matchRatio`, how features are matched (Exact is the same on every run);
	// - `inlierPixels`, how far a verified match may be from its pair's fundamental matrix, in the
	//   pixels searched;
	// - `minimumPairInliers`, the inliers a frame pair needs to verify.
	// The pre-screen, the localisation per unit of size and the verification's seed stay at
	// feature::SequenceExtractionRequest's defaults: they describe the extractor, not the footage.
	// `loss` and `lossScale` are the bundle adjustment's robust loss and where an observation counts
	// as an outlier; `freePrincipalPoint` estimates the principal point, false holds it. `seed` drives
	// the method's draws; `deterministic` governs the extraction too.
	class CalibrateCameraTargetlessNode : public CalibrationNode
	{
	public:
		CalibrateCameraTargetlessNode();

		std::unique_ptr<flow::Node> clone() const override { return std::make_unique<CalibrateCameraTargetlessNode>(*this); }
		void compute(flow::NodeEvaluation& evaluation) const override;

	private:
		// The settings those parameters ask for. The extraction's execution policy is the request's,
		// which the method sets as it records them.
		calibration::targetless::Settings settings() const;

		flow::PortId m_longestSide;		   // "longestSide" (int, 0 = native)
		flow::PortId m_sampledFrames;	   // "sampledFrames" (int)
		flow::PortId m_featureCap;		   // "featureCap" (int)
		flow::PortId m_matchSearch;		   // "matchSearch" (feature::MatchSearch)
		flow::PortId m_matchRatio;		   // "matchRatio" (float)
		flow::PortId m_inlierPixels;	   // "inlierPixels" (float, pixels searched)
		flow::PortId m_minimumPairInliers; // "minimumPairInliers" (int)
		flow::PortId m_loss;			   // "loss" (refinement::LossFamily)
		flow::PortId m_lossScale;		   // "lossScale" (float, standard deviations)
		flow::PortId m_freePrincipalPoint; // "freePrincipalPoint" (bool)
		flow::PortId m_report;			   // "report" (calibration::Report)
	};
} // namespace lain::camera
