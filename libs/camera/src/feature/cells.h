#pragma once

#include "lain/camera/feature/extraction.h" // PairExtraction
#include "lain/camera/feature/extractionsettings.h"
#include "lain/camera/feature/features.h"
#include "lain/camera/feature/status.h"
#include "lain/camera/provenance.h"

#include <lain/image/image.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The work every feature-track extraction shares, the fixed rig's and a moving camera's: one image's
// features as cells, a pair of images matched before verification, and verified pairs joined into
// tracks. What a view is, how a pair is verified and how a track is spelled stay each extraction's own.
// Private to lain::camera: `src/` is never on a consumer's include path.
namespace lain::camera::detail
{
	// The 1/64-pixel cell a source-pixel coordinate falls in: the grid a track's identity is spelled
	// on, and the grid on which two features of one image are one feature.
	long long cell(double v);

	// The order features are kept in, strongest first: by response, then where they are, then size and
	// angle. Two cells of one image are at two pixels, so it orders them totally.
	bool stronger(const feature::Keypoint& a, const feature::Keypoint& b);

	// The features of one image as CELLS (ADR-0016): every feature in one 1/64-pixel cell is one
	// feature, found at one or more orientations, each its own descriptor row. SIFT reports a
	// keypoint with two strong orientation peaks twice, at one pixel and one size, and which of the
	// two a view would keep depends on its roll, so every row is matched and a match between rows is
	// a match between their cells. A cell's rows are contiguous, strongest first, and the cells are in
	// `stronger` order of their first row.
	struct Cells
	{
		feature::Features rows;
		std::vector<std::uint32_t> start;  // per cell, its first row
		std::vector<std::uint32_t> cellOf; // per row, its cell

		std::size_t size() const { return start.size(); }
		std::uint32_t end(std::size_t c) const { return c + 1 < start.size() ? start[c + 1] : std::uint32_t(rows.size()); }
		// A cell's first row: its strongest, and the pixel and size the cell is at.
		const feature::Keypoint& keypoint(std::size_t c) const { return rows.keypoints[start[c]]; }
	};

	// No cells, over `f`'s descriptor kind, image and scale.
	Cells cellsLike(const feature::Features& f);

	// Appends cell `c` of `from` to `to`, every row at `at`'s pixel and size when given.
	void appendCell(Cells& to, const Cells& from, std::size_t c, const feature::Keypoint* at = nullptr);

	// The first `count` cells of `c`, its strongest, with all their rows.
	Cells firstCells(const Cells& c, std::size_t count);

	// One decoded image's features, through the extract() facade: grouped into cells, strongest
	// first, and capped at the settings' feature cap. When the facade refuses, `status` and `detail`
	// say what it said and there are no cells.
	struct ImageCells
	{
		feature::Status status = feature::Status::Ok;
		std::string detail;
		Provenance provenance;
		double scale = 1.0;
		Cells cells;
		std::uint32_t found = 0;		// keypoints, every orientation
		std::uint32_t orientations = 0; // of those, further orientations of a feature, kept with it
		std::uint32_t capped = 0;		// features beyond the cap

		bool ok() const { return status == feature::Status::Ok; }
	};

	ImageCells imageCells(const image::Image& image, const feature::ExtractionSettings& settings);

	using CellPair = std::pair<std::uint32_t, std::uint32_t>;

	// A pair of images matched, before it is verified.
	struct MatchedCells
	{
		bool verify = false;		   // whether the pair reached verification
		std::vector<CellPair> matches; // to verify: (A's cell, B's), ascending, each cell once
		feature::Status failure = feature::Status::Ok;
		std::string detail; // the matcher's, when it refused
		std::optional<Provenance> matcher;
	};

	// One pair: a pre-screen on each side's strongest features, with all their orientations; the full
	// match; and a cell matched to two of the other side's dropped. Fills `entry`'s match counts, and
	// its outcome and detail when the pair stops before verification: TooFew, when a side has fewer
	// than two `features` (the noun its report uses), or Skipped by the pre-screen.
	MatchedCells matchCells(const Cells& a, const Cells& b, feature::PairExtraction& entry,
							const feature::ExtractionSettings& settings, const char* features);

	// One pair's verified matches, between views `a` < `b`.
	struct VerifiedMatches
	{
		std::uint32_t a = 0;
		std::uint32_t b = 0;
		std::vector<CellPair> matches; // (a's cell, b's cell)
	};

	// Tracks joined from verified matches: each a list of (view, cell), in view order, of two or more
	// views.
	struct JoinedTracks
	{
		std::vector<std::vector<CellPair>> tracks; // in the order of their smallest (view, cell)
		std::uint32_t conflicts = 0;			   // components holding two cells of one view, rejected whole
		std::uint32_t conflictObservations = 0;	   // the cells those held
	};

	// The verified matches of every pair joined into tracks, serially in the order given (canonical
	// pair order). `cellsPerView` numbers each view's cells. A component holding two cells of one view
	// is a conflict and is rejected whole; one of a single cell is no track.
	JoinedTracks joinTracks(const std::vector<std::uint32_t>& cellsPerView, const std::vector<VerifiedMatches>& verified);

	// The first problem with the settings every extraction shares, or nothing. `fewestInliers` is the
	// fewest its verification needs, which `verification` names ("a relative pose").
	std::optional<std::string> settingsProblem(const feature::ExtractionSettings& settings, std::uint32_t fewestInliers,
											   const char* verification);

	// Whether the settings ask for a search no matcher can run reproducibly or at all, before anything
	// is decoded; the reason, or nothing.
	std::optional<std::string> searchProblem(const feature::ExtractionSettings& settings);
} // namespace lain::camera::detail
