#include "cells.h"

#include "lain/camera/feature/matching.h"
#include "unionfind.h"

#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace lain::camera::detail
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static constexpr double kTau = 6.283185307179586;

	// An angle in [0, 2π).
	static double wrapped(double angle)
	{
		const double a = std::fmod(angle, kTau);
		return a < 0 ? a + kTau : a;
	}

	// The features of `f` at `keep`, in that order.
	static feature::Features subset(const feature::Features& f, const std::vector<std::uint32_t>& keep)
	{
		feature::Features out;
		out.kind = f.kind;
		out.descriptorBytes = f.descriptorBytes;
		out.image = f.image;
		out.scale = f.scale;
		out.keypoints.reserve(keep.size());
		out.descriptors.reserve(keep.size() * f.descriptorBytes);
		for (const std::uint32_t i : keep)
		{
			out.keypoints.push_back(f.keypoints[i]);
			const std::uint8_t* d = f.descriptor(i);
			out.descriptors.insert(out.descriptors.end(), d, d + f.descriptorBytes);
		}
		return out;
	}

	// `f`'s features grouped into cells. A cell's rows run by response, then angle in [0, 2π), then
	// size, then their order in `f`, so the grouping does not depend on how the backend listed them.
	// Adds the rows beyond each cell's first to `orientations`.
	static Cells cellsOf(const feature::Features& f, std::uint32_t& orientations)
	{
		std::map<std::pair<long long, long long>, std::vector<std::uint32_t>> byCell; // (y, x) cell -> rows
		for (std::uint32_t i = 0; i < f.size(); ++i)
			byCell[{cell(f.keypoints[i].pixel.y), cell(f.keypoints[i].pixel.x)}].push_back(i);
		const auto rowOrder = [&f](std::uint32_t a, std::uint32_t b)
		{
			const feature::Keypoint& x = f.keypoints[a];
			const feature::Keypoint& y = f.keypoints[b];
			return std::make_tuple(-x.response, wrapped(x.angle), x.size, a) < std::make_tuple(-y.response, wrapped(y.angle), y.size, b);
		};
		std::vector<std::vector<std::uint32_t>> groups;
		groups.reserve(byCell.size());
		for (auto& [key, rows] : byCell)
		{
			(void)key;
			std::sort(rows.begin(), rows.end(), rowOrder);
			orientations += std::uint32_t(rows.size() - 1);
			groups.push_back(std::move(rows));
		}
		// Two cells are at two pixels, so `stronger` orders them totally.
		std::sort(groups.begin(), groups.end(), [&f](const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
				  { return stronger(f.keypoints[a.front()], f.keypoints[b.front()]); });
		Cells out = cellsLike(f);
		for (const std::vector<std::uint32_t>& rows : groups)
		{
			out.start.push_back(std::uint32_t(out.rows.size()));
			for (const std::uint32_t r : rows)
			{
				out.cellOf.push_back(std::uint32_t(out.start.size() - 1));
				out.rows.keypoints.push_back(f.keypoints[r]);
				const std::uint8_t* d = f.descriptor(r);
				out.rows.descriptors.insert(out.rows.descriptors.end(), d, d + f.descriptorBytes);
			}
		}
		return out;
	}

	// The cells a match between rows joins, each pair once, ascending.
	static std::vector<CellPair> cellPairs(const feature::MatchResult& m, const Cells& a, const Cells& b)
	{
		std::set<CellPair> pairs;
		for (const feature::Match& r : m.matches)
			pairs.insert({a.cellOf[r.a], b.cellOf[r.b]});
		return {pairs.begin(), pairs.end()};
	}

	// A cell whose rows matched two cells of the other side says nothing about which is its feature,
	// so every pair holding it is dropped and counted in `ambiguous`.
	static std::vector<CellPair> unambiguous(const std::vector<CellPair>& pairs, std::uint32_t& ambiguous)
	{
		std::map<std::uint32_t, std::size_t> fromA, fromB;
		for (const CellPair& p : pairs)
		{
			++fromA[p.first];
			++fromB[p.second];
		}
		std::vector<CellPair> kept;
		for (const CellPair& p : pairs)
		{
			if (fromA[p.first] == 1 && fromB[p.second] == 1)
				kept.push_back(p);
			else
				++ambiguous;
		}
		return kept;
	}

	// --- cells -----------------------------------------------------------------------

	long long cell(double v)
	{
		return std::llround(v * 64.0);
	}

	bool stronger(const feature::Keypoint& a, const feature::Keypoint& b)
	{
		if (a.response != b.response)
			return a.response > b.response;
		if (a.pixel.y != b.pixel.y)
			return a.pixel.y < b.pixel.y;
		if (a.pixel.x != b.pixel.x)
			return a.pixel.x < b.pixel.x;
		if (a.size != b.size)
			return a.size < b.size;
		return wrapped(a.angle) < wrapped(b.angle);
	}

	Cells cellsLike(const feature::Features& f)
	{
		Cells out;
		out.rows = subset(f, {});
		return out;
	}

	void appendCell(Cells& to, const Cells& from, std::size_t c, const feature::Keypoint* at)
	{
		to.start.push_back(std::uint32_t(to.rows.size()));
		for (std::uint32_t r = from.start[c]; r < from.end(c); ++r)
		{
			feature::Keypoint k = from.rows.keypoints[r];
			if (at)
			{
				k.pixel = at->pixel;
				k.size = at->size;
			}
			to.cellOf.push_back(std::uint32_t(to.start.size() - 1));
			to.rows.keypoints.push_back(k);
			const std::uint8_t* d = from.rows.descriptor(r);
			to.rows.descriptors.insert(to.rows.descriptors.end(), d, d + from.rows.descriptorBytes);
		}
	}

	Cells firstCells(const Cells& c, std::size_t count)
	{
		Cells out = cellsLike(c.rows);
		for (std::size_t k = 0; k < std::min(count, c.size()); ++k)
			appendCell(out, c, k);
		return out;
	}

	ImageCells imageCells(const image::Image& image, const feature::ExtractionSettings& settings)
	{
		ImageCells out;
		feature::ExtractResult found = feature::extract(image, settings.scale);
		out.provenance = found.provenance;
		if (!found.ok())
		{
			out.status = found.status;
			out.detail = std::move(found.detail);
			return out;
		}
		out.scale = found.features.scale;
		out.found = std::uint32_t(found.features.size());
		out.cells = cellsOf(found.features, out.orientations);
		if (out.cells.size() > settings.featureCap)
		{
			out.capped = std::uint32_t(out.cells.size() - settings.featureCap);
			out.cells = firstCells(out.cells, settings.featureCap);
		}
		return out;
	}

	// --- pairs -----------------------------------------------------------------------

	MatchedCells matchCells(const Cells& a, const Cells& b, feature::PairExtraction& entry,
							const feature::ExtractionSettings& settings, const char* features)
	{
		MatchedCells out;
		const auto fail = [&out](const feature::MatchResult& m)
		{
			out.failure = m.status;
			out.detail = m.detail;
			return std::move(out);
		};
		if (a.size() < 2 || b.size() < 2)
		{
			entry.outcome = feature::PairOutcome::TooFew;
			entry.detail = string::format("{} and {} {}", a.size(), b.size(), features);
			return out;
		}

		// The pre-screen matches each side's strongest features, with all their orientations. When
		// both sides fit inside it, it is the full match.
		const bool whole = a.size() <= settings.prescreenFeatures && b.size() <= settings.prescreenFeatures;
		feature::MatchResult full;
		std::size_t screened = 0;
		if (whole)
		{
			full = feature::match(a.rows, b.rows, settings.matching);
			if (!full.ok())
				return fail(full);
			out.matcher = full.provenance;
			screened = cellPairs(full, a, b).size();
		}
		else
		{
			const Cells strongestA = firstCells(a, settings.prescreenFeatures);
			const Cells strongestB = firstCells(b, settings.prescreenFeatures);
			const feature::MatchResult prescreen = feature::match(strongestA.rows, strongestB.rows, settings.matching);
			if (!prescreen.ok())
				return fail(prescreen);
			out.matcher = prescreen.provenance;
			screened = cellPairs(prescreen, strongestA, strongestB).size();
		}
		entry.prescreenMatches = std::uint32_t(screened);
		if (screened < settings.prescreenMatches)
		{
			entry.outcome = feature::PairOutcome::Skipped;
			entry.detail = string::format("{} matches among the strongest {}; the pre-screen asks for {}", screened,
										  settings.prescreenFeatures, settings.prescreenMatches);
			return out;
		}
		if (!whole)
		{
			full = feature::match(a.rows, b.rows, settings.matching);
			if (!full.ok())
				return fail(full);
		}
		out.matches = unambiguous(cellPairs(full, a, b), entry.ambiguous);
		entry.matches = std::uint32_t(out.matches.size());
		out.verify = true;
		return out;
	}

	// --- tracks ----------------------------------------------------------------------

	JoinedTracks joinTracks(const std::vector<std::uint32_t>& cellsPerView, const std::vector<VerifiedMatches>& verified)
	{
		std::vector<std::uint32_t> offset;
		std::uint32_t nodes = 0;
		for (const std::uint32_t count : cellsPerView)
		{
			offset.push_back(nodes);
			nodes += count;
		}
		std::vector<std::uint32_t> parent(nodes);
		std::iota(parent.begin(), parent.end(), 0u);
		for (const VerifiedMatches& p : verified)
		{
			for (const auto& [ca, cb] : p.matches)
				join(parent, offset[p.a] + ca, offset[p.b] + cb);
		}
		std::map<std::uint32_t, std::vector<CellPair>> components; // root -> (view, cell)
		for (std::uint32_t v = 0; v < cellsPerView.size(); ++v)
		{
			for (std::uint32_t c = 0; c < cellsPerView[v]; ++c)
				components[root(parent, offset[v] + c)].push_back({v, c});
		}
		JoinedTracks out;
		for (auto& [r, members] : components)
		{
			(void)r;
			if (members.size() < 2)
				continue;
			// Members are in view order, so two of one view are neighbours.
			bool conflict = false;
			for (std::size_t m = 1; m < members.size(); ++m)
				conflict = conflict || members[m].first == members[m - 1].first;
			if (conflict)
			{
				++out.conflicts;
				out.conflictObservations += std::uint32_t(members.size());
				continue;
			}
			out.tracks.push_back(std::move(members));
		}
		return out;
	}

	// --- checks ----------------------------------------------------------------------

	std::optional<std::string> settingsProblem(const feature::ExtractionSettings& s, std::uint32_t fewestInliers,
											   const char* verification)
	{
		const auto positive = [](double v)
		{ return std::isfinite(v) && v > 0; };
		if (s.featureCap < 2)
			return string::format("a feature cap of {} leaves nothing to match", s.featureCap);
		if (!(s.matching.ratio > 0 && s.matching.ratio <= 1))
			return string::format("a ratio of {} is outside (0, 1]", s.matching.ratio);
		if (s.minimumPairInliers < fewestInliers)
		{
			return string::format("a pair verifies with at least {} inliers, the fewest {} needs; {} were asked for",
								  fewestInliers, verification, s.minimumPairInliers);
		}
		if (s.prescreenFeatures < 2)
			return string::format("a pre-screen of {} features has no second neighbour to test", s.prescreenFeatures);
		if (!positive(s.localisationPerSize))
			return string::format("a localisation of {} per unit of size is not positive", s.localisationPerSize);
		return std::nullopt;
	}

	std::optional<std::string> searchProblem(const feature::ExtractionSettings& settings)
	{
		if (settings.matching.search == feature::MatchSearch::Approximate && settings.execution == ExecutionPolicy::DeterministicDebug)
		{
			return std::string("approximate matching is reproducible only within a tolerance, and deterministic "
							   "debugging asks for the same answer every run");
		}
		bool searchable = false;
		for (const std::string& key : feature::matcherRegistry().keys())
			searchable = searchable || feature::matcherRegistry().create(key)->supports(settings.matching.search);
		if (!searchable)
			return string::format("no matcher supports {} search", meta::enums::name(settings.matching.search));
		return std::nullopt;
	}
} // namespace lain::camera::detail
