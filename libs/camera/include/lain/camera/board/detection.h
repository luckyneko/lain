#pragma once

#include "lain/camera/board/observation.h"
#include "lain/camera/board/specification.h"
#include "lain/camera/provenance.h"
#include "lain/camera/scalepolicy.h"

#include <lain/core/factory.h>
#include <lain/core/time.h>
#include <lain/image/image.h>
#include <lain/media/frameref.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lain::camera::board
{
	// --- the request ------------------------------------------------------------

	// The scale to look for the board's features at. The policy is the camera module's
	// (scalepolicy.h), since scene features are found at a scale too; it is named here as well, where
	// a board detection's callers look for it.
	using camera::LongestSide;
	using camera::NativeScale;
	using camera::resolveScale;
	using camera::ScaleFactor;
	using camera::ScalePolicy;

	enum class DetailLevel
	{
		Summary,  // the production default
		Detailed, // also keeps rejected candidates, for debug overlays; never pixels
	};

	struct DetectionRequest
	{
		ScalePolicy scale = NativeScale{};
		// Refine accepted corners against native-resolution pixels after detecting at a reduced
		// scale. Independent of the scale, so native detection, coarse detection with native
		// refinement and fully reduced processing can be compared without moving any coordinate.
		bool refineAtNativeResolution = true;
		DetailLevel detail = DetailLevel::Summary;
		// Fewer corners than this is Failed, not Partial: four is the least that fixes a board pose.
		std::uint32_t minimumCorners = 4;
	};

	// --- the report ---------------------------------------------------------------

	enum class DetectionStatus
	{
		Detected, // every corner of the pattern was found
		Partial,  // some were, at least the request's minimum: a usable observation
		Failed,	  // no usable observation
	};

	enum class Rejection
	{
		NoBackend,	   // this build has no detector
		UnusableImage, // empty, or a format the backend cannot read
		NoMarkers,	   // no marker of the pattern's dictionary was found
		TooFewCorners, // markers were found, but fewer corners than the request's minimum
	};

	struct RejectionReason
	{
		Rejection reason;
		std::string detail;
	};

	struct DetectionStats
	{
		std::uint32_t markersFound = 0;
		std::uint32_t markersRejected = 0; // candidates the backend looked at and turned down
		std::uint32_t cornersFound = 0;
		std::uint32_t cornersExpected = 0;
	};

	// Where the image the backend searched sat relative to the source: detection coordinates are
	// source coordinates times these factors. Per axis, because rounding a resized image to whole
	// pixels leaves the two slightly different.
	struct DetectionTransform
	{
		double scaleX = 1.0;
		double scaleY = 1.0;
	};

	enum class RefinementResolution
	{
		NotRefined,
		DetectionScale, // refined in the reduced image
		Native,			// refined against source pixels
	};

	// Rejected candidates, kept only for DetailLevel::Detailed.
	struct DetectionEvidence
	{
		std::vector<std::array<math::Vec2d, 4>> rejectedMarkers; // quad corners, source pixels
	};

	// The outcome of looking for a board in one frame: always a report, never a bare optional
	// (CONTEXT.md, "Board-detection report"). A failed report is as much a result as a successful
	// one; view selection and tests read its reasons.
	struct DetectionReport
	{
		DetectionStatus status = DetectionStatus::Failed;
		std::vector<RejectionReason> rejections;
		std::optional<Observation> observation; // present exactly when not Failed
		DetectionStats stats;
		DetectionRequest request;	  // what was asked
		DetectionTransform transform; // what was done
		RefinementResolution refinement = RefinementResolution::NotRefined;
		Provenance provenance;
		core::Time elapsed;
		std::optional<DetectionEvidence> evidence; // Detailed only

		// One line for a person: the status, the corners found, and the first reason when it failed.
		std::string toString() const;
	};

	// --- the backend seam -----------------------------------------------------------

	// A board detector backend (ADR-0004's service shape, the io::video precedent): the only place a
	// detection library is named. It fills the report's status, rejections, observation, stats,
	// transform, refinement, provenance and evidence; the detect() facade fills the request and the
	// elapsed time, so every backend reports those the same way.
	class Detector
	{
	public:
		virtual ~Detector() = default;

		// Look for `board` in `image` at `scale` (already resolved from the request, in (0, 1]).
		virtual DetectionReport detect(const image::Image& image, const media::FrameRef& frame,
									   const Specification& board, const DetectionRequest& request, double scale) const = 0;
	};

	// The process-wide detector registry, keyed by backend name ("opencv").
	core::Factory<Detector>& detectorRegistry();

	// Whether this build can detect boards at all: whether any backend registered.
	bool canDetect();

	// Look for `board` in `image` through the registered backend. With none registered the report
	// is Failed with Rejection::NoBackend, so a caller learns about a missing capability from the
	// same value it reads every other outcome from.
	DetectionReport detect(const image::Image& image, const media::FrameRef& frame, const Specification& board,
						   const DetectionRequest& request = {});
} // namespace lain::camera::board
