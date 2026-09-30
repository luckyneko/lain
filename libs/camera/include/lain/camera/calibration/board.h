#pragma once

#include "lain/camera/board/detection.h"
#include "lain/camera/calibration/report.h"
#include "lain/camera/calibration/request.h"

#include <lain/media/framesequence.h>

#include <cstdint>
#include <vector>

// Board calibration, the first calibration method module (ADR-0016). Backend-neutral: detection and
// estimation go through the registered backends; everything that decides what the result MEANS
// (which views, which held out, how it is validated, how stable it is, what it is fit for) is here,
// so it is the same whichever backend runs.
namespace lain::camera::calibration::board
{
	// Calibrate a camera from footage of a known board: detect it in every frame, hold some usable
	// views out, select calibration views from the rest, estimate through the registered backend,
	// validate on the held-out views with lain's own projection, resample for stability, and give a
	// verdict. Always a report, successful or failed.
	//
	// Frames are decoded one per task and released, so memory is a frame per worker however long
	// the footage is. With ExecutionPolicy::DeterministicDebug the same work runs serially.
	Report calibrate(const media::FrameSequence& footage, const camera::board::Specification& board,
					 const Request& request);

	// The same from detections already made: one report per frame, measured in `image`. What
	// calibrate(footage) does after detecting; useful on its own to calibrate stored detections again
	// with another model or profile without decoding a frame.
	Report calibrate(const std::vector<camera::board::DetectionReport>& detections, const ImageGeometry& image,
					 const camera::board::Specification& board, const Request& request);

	// Which usable detections are held out, and which calibrate: positions into `detections`.
	//
	// Deterministic. Usable views are put in canonical order (source uri, then frame ordinal); every
	// k-th of them is held out, k = 1 / heldOutFraction (none are when fewer than five views are
	// usable); calibration views are then chosen greedily from the rest, up to maximumViews, by the
	// image-grid cells they newly cover plus the board tilt they add, ties going to the earlier view.
	struct ViewSplit
	{
		std::vector<std::uint32_t> calibration;
		std::vector<std::uint32_t> heldOut;
	};
	ViewSplit splitViews(const std::vector<camera::board::DetectionReport>& detections,
						 const camera::board::Specification& board, const Request& request);

	// The fraction of an 8 x 6 grid over `image` in which at least one of the views has a corner.
	constexpr int kCoverageColumns = 8;
	constexpr int kCoverageRows = 6;
	double coverage(const std::vector<const camera::board::Observation*>& views, const ImageGeometry& image);
} // namespace lain::camera::calibration::board
