#pragma once

#include "lain/camera/capture/capturegroup.h"
#include "lain/camera/feature/geometry.h"
#include "lain/camera/feature/matching.h"
#include "lain/camera/feature/status.h"
#include "lain/camera/feature/tracks.h"
#include "lain/camera/method.h" // ExecutionPolicy
#include "lain/camera/provenance.h"
#include "lain/camera/rig.h"
#include "lain/camera/scalepolicy.h"

#include <lain/core/time.h>

#include <cstdint>
#include <string>
#include <vector>

// Feature-track extraction for fixed cameras (CONTEXT.md, "Feature-track extraction"): footage and
// capture groups in, accepted static tracks and a report out. Backend-neutral: the features, the
// neighbours and the candidate poses come from the registered backends, and everything that decides
// what a track IS (which features are static, which matches survive, which pairs verify, which
// tracks are accepted) is here, so it is the same whichever backends run (ADR-0016, "Producers
// propose; lain decides").
namespace lain::camera::feature
{
	struct ExtractionRequest
	{
		// The scale each frame is searched at.
		ScalePolicy scale = LongestSide{1920};
		// How many capture groups every camera samples, spread evenly across the capture.
		std::uint32_t samples = 5;
		// The strongest features of each frame kept, after duplicates collapse.
		std::uint32_t featureCap = 8192;
		// How features are matched: between a camera's frames, and between cameras.
		MatchRequest matching;
		// How a camera pair's matches are verified.
		GeometryRequest geometry;
		// A pair verifies with at least this many inliers (COLMAP's two-view minimum); at least 5.
		std::uint32_t minimumPairInliers = 15;
		// A pair is first matched on each side's strongest few features, and skipped when fewer than
		// `prescreenMatches` survive.
		std::uint32_t prescreenFeatures = 512;
		std::uint32_t prescreenMatches = 5;
		// A feature found in two of a camera's frames is one feature only within this distance, in
		// the pixels the backend searched, and this ratio of sizes.
		double staticDistance = 1.5;
		double staticSizeRatio = 1.5;
		// How precisely a feature is located: a standard deviation per unit of its size
		// (Keypoint::size), so in source pixels whatever the scale searched. A detector finds a
		// feature at a size it chose, and a larger one is located less precisely. 0.034 is SIFT's,
		// measured against the truth on rendered scenes (M9 slice 3, sub-slice 7): 3.4% natively,
		// within the same bound at half scale.
		double localisationPerSize = 0.034;
		ExecutionPolicy execution = ExecutionPolicy::Normal;
	};

	// What became of one camera's features: how many it found, how many left before matching and why,
	// and how many static features the rest became.
	struct CameraExtraction
	{
		double scale = 1.0;				  // the factor its frames were searched at
		std::uint32_t frames = 0;		  // sampled frames it has a member in
		std::uint32_t framesFailed = 0;	  // of those, frames that did not decode
		std::uint32_t features = 0;		  // found, summed over its frames
		std::uint32_t duplicates = 0;	  // collapsed onto a stronger feature at the same pixel
		std::uint32_t capped = 0;		  // beyond the feature cap
		std::uint32_t inconsistent = 0;	  // in a cluster holding two features of one frame
		std::uint32_t transient = 0;	  // in a cluster found in too few of its frames
		std::uint32_t staticFeatures = 0; // what it brings to matching
	};

	enum class PairOutcome
	{
		TooFew,		// a side has fewer than two static features
		Skipped,	// the pre-screen found too few matches to be worth the full match
		Unverified, // no relative pose explains enough of its matches
		Verified,	// its inliers join tracks
	};

	struct PairExtraction
	{
		std::uint32_t a = 0; // into TrackSet::views, a < b
		std::uint32_t b = 0;
		PairOutcome outcome = PairOutcome::TooFew;
		std::uint32_t prescreenMatches = 0;
		std::uint32_t matches = 0; // from the full match
		std::uint32_t inliers = 0; // of those, explained by the pair's relative pose
		std::string detail;		   // why it did not verify
	};

	struct ExtractionReport
	{
		ExtractionRequest request;
		std::vector<CameraExtraction> cameras; // as TrackSet::views
		std::vector<PairExtraction> pairs;	   // every pair, ascending by (a, b)
		std::uint32_t tracks = 0;
		std::uint32_t conflicts = 0;			// components holding two features of one view, rejected whole
		std::uint32_t conflictObservations = 0; // the features those held
		// The backends that answered, from the first call in canonical order.
		Provenance extractor;
		Provenance matcher;
		Provenance geometry;
		core::Time elapsed;
	};

	struct ExtractionResult
	{
		Status status = Status::NoSolution;
		std::string detail; // why there is no track set, when the status is not Ok
		TrackSet trackSet;
		ExtractionReport report;

		bool ok() const { return status == Status::Ok; }
	};

	// The static feature tracks of a fixed rig. In order:
	// 1. every check that needs no frame, so a request that cannot succeed decodes nothing;
	// 2. the capture groups to sample: ordered by their members' median timestamp, then median
	//    ordinal, then identity, and taken evenly across that order;
	// 3. per camera, one task: each sampled frame decoded, its features extracted, the image released,
	//    duplicates collapsed and the cap applied; then the frames matched with each other, and a
	//    feature found at one pixel in at least half of them kept as static, at its median pixel;
	// 4. per camera pair, one task: a pre-screen on the strongest features, the full match, and the
	//    matches verified by a relative pose;
	// 5. the verified matches joined into tracks, serially, a track holding two features of one view
	//    rejected whole.
	//
	// Ok with an empty track set is an extraction that found nothing to join: whether that is enough
	// is the consumer's question. Independent of input order and of the execution policy.
	ExtractionResult extractTracks(const std::vector<RigFootage>& cameras, const std::vector<capture::CaptureGroup>& groups,
								   const ExtractionRequest& request = {});
} // namespace lain::camera::feature
