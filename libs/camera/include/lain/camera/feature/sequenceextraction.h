#pragma once

#include "lain/camera/capture/capturegroup.h" // CameraFootage
#include "lain/camera/feature/extraction.h"	  // PairExtraction
#include "lain/camera/feature/extractionsettings.h"
#include "lain/camera/feature/status.h"
#include "lain/camera/feature/tracks.h"
#include "lain/camera/provenance.h"

#include <lain/core/time.h>

#include <cstdint>
#include <string>
#include <vector>

// Feature-track extraction for one moving camera (CONTEXT.md, "Feature-track extraction"): its footage
// in, every sampled frame its own view, and accepted tracks of scene points across frames out. It is
// targetless calibration's evidence, gathered before the camera has a model, so a frame pair is
// verified by a fundamental matrix on pixels where the fixed rig's verifies by a relative pose on rays.
// Backend-neutral, as the fixed rig's is: the backends propose features, neighbours and candidate
// matrices, and what a track IS is decided here (ADR-0016, "Producers propose; lain decides").
namespace lain::camera::feature
{
	// The settings every extraction shares (extractionsettings.h), and what only a moving camera's
	// reads. `minimumPairInliers` is at least 8 here, the fewest a fundamental matrix needs.
	struct SequenceExtractionRequest : ExtractionSettings
	{
		// How many frames are sampled, spread evenly across the footage. Provisional: M9 slice 4's
		// sub-slice 8 times matching every pair of 40, and sub-slice 10 measures the default.
		std::uint32_t samples = 40;
		// A match is an inlier within this Sampson distance of its pair's fundamental matrix, in the
		// pixels the backend searched, so it means the same under any scale policy (as the fixed rig's
		// staticDistance does). COLMAP's 4 px, provisional until sub-slice 8 measures it.
		double threshold = 4.0;
		// The seed every pair's verification samples with.
		std::uint64_t seed = 0;
	};

	// What became of one sampled frame's features.
	struct FrameExtraction
	{
		double scale = 1.0;				// the factor it was searched at
		bool failed = false;			// it did not decode, so it is a view with no observations
		std::uint32_t found = 0;		// keypoints found, every orientation
		std::uint32_t orientations = 0; // of those, further orientations of a feature, kept with it
		std::uint32_t capped = 0;		// features beyond the feature cap
		std::uint32_t features = 0;		// what it brings to matching
	};

	struct SequenceExtractionReport
	{
		SequenceExtractionRequest request;
		std::vector<FrameExtraction> frames; // as TrackSet::views
		std::vector<PairExtraction> pairs;	 // every frame pair, ascending by (a, b)
		std::uint32_t tracks = 0;
		std::uint32_t conflicts = 0;			// components holding two features of one frame, rejected whole
		std::uint32_t conflictObservations = 0; // the features those held
		// The backends that answered, from the first call in canonical order.
		Provenance extractor;
		Provenance matcher;
		Provenance geometry;
		core::Time elapsed;
	};

	struct SequenceExtractionResult
	{
		Status status = Status::NoSolution;
		std::string detail; // why there is no track set, when the status is not Ok
		TrackSet trackSet;
		SequenceExtractionReport report;

		bool ok() const { return status == Status::Ok; }
	};

	// The feature tracks of one moving camera's footage. In order:
	// 1. every check that needs no frame, so a request that cannot succeed decodes nothing;
	// 2. the frames in capture order (timestamp, then ordinal, then source), and `samples` of them at
	//    the centres of equal bins, so the order the footage lists them in does not matter;
	// 3. per sampled frame, one task: decoded, its features extracted, the image released, and the
	//    keypoints at one pixel kept together as one feature at several orientations, capped;
	// 4. per frame pair, one task: a pre-screen on the strongest features, the full match, a feature
	//    matched to two of the other frame's dropped, and the matches verified by a fundamental matrix;
	// 5. the verified matches joined into tracks, serially, a track holding two features of one frame
	//    rejected whole.
	//
	// Every sampled frame is a view, its camera the footage's identity, decoded or not. The tracks'
	// identities digest their frames as well as their pixels. Ok with an empty track set is an
	// extraction that found nothing to join. Independent of the order of the footage's frames and of
	// the execution policy.
	SequenceExtractionResult extractSequenceTracks(const capture::CameraFootage& camera,
												   const SequenceExtractionRequest& request = {});
} // namespace lain::camera::feature
