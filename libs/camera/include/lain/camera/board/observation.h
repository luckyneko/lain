#pragma once

#include "lain/camera/cameramodel.h" // ImageGeometry

#include <lain/core/sha256.h>
#include <lain/math/types.h>
#include <lain/media/frameref.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace lain::camera::board
{
	// One identified board feature (a ChArUco inner corner) where it was measured in the image.
	struct FeatureObservation
	{
		std::uint32_t id = 0;	// the pattern's corner id (Pattern::cornerCount)
		math::Vec2d pixel{0.0}; // SOURCE-image pixel coordinates (ImageGeometry's convention)
		// The pixel covariance (xx, xy, yy), present only when a backend can defend it as a
		// statistical uncertainty. A detector's response or iteration count is diagnostic, never
		// this; with it absent, a solver uses an explicitly configured noise model instead.
		std::optional<std::array<double, 3>> covariance;
	};

	// That a board pattern was seen in one source frame, and where its features were (CONTEXT.md,
	// "Board observation"). It names its frame and holds compact image-space evidence; it never holds
	// the image, so a calibration can keep thousands of these without keeping thousands of frames.
	// Calibration, validation, tests and debug views all consume this same value.
	struct Observation
	{
		media::FrameRef frame;
		ImageGeometry image;					  // the geometry the pixels are measured in: the source frame's
		core::Sha256Digest pattern;				  // which pattern's ids these are (Pattern::fingerprint)
		std::vector<FeatureObservation> features; // ascending by id, each id at most once
	};
} // namespace lain::camera::board
