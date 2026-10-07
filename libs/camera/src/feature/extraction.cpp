#include "lain/camera/feature/extraction.h"

#include "execution.h"
#include "footageindex.h"
#include "lain/camera/projection.h"

#include <lain/core/sha256.h>
#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace lain::camera::feature
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static constexpr double kTau = 6.283185307179586;

	// The 1/64-pixel cell a source-pixel coordinate falls in: the grid a track's identity is spelled
	// on, and the grid on which two features of one image are one feature.
	static long long cell(double v)
	{
		return std::llround(v * 64.0);
	}

	// An angle in [0, 2π).
	static double wrapped(double angle)
	{
		const double a = std::fmod(angle, kTau);
		return a < 0 ? a + kTau : a;
	}

	// The order a camera's features are kept in, strongest first: by response, then where they are.
	// Two cells of one image are at two pixels, so it orders them totally.
	static bool stronger(const Keypoint& a, const Keypoint& b)
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

	// The features of `f` at `keep`, in that order.
	static Features subset(const Features& f, const std::vector<std::uint32_t>& keep)
	{
		Features out;
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

	// The features of one image as CELLS (ADR-0016): every feature in one 1/64-pixel cell is one
	// feature, found at one or more orientations, each its own descriptor row. SIFT reports a
	// keypoint with two strong orientation peaks twice, at one pixel and one size, and which of the
	// two a view would keep depends on its roll, so every row is matched and a match between rows is
	// a match between their cells. A cell's rows are contiguous, strongest first, and the cells are in
	// `stronger` order of their first row.
	struct Cells
	{
		Features rows;
		std::vector<std::uint32_t> start;  // per cell, its first row
		std::vector<std::uint32_t> cellOf; // per row, its cell

		std::size_t size() const { return start.size(); }
		std::uint32_t end(std::size_t c) const { return c + 1 < start.size() ? start[c + 1] : std::uint32_t(rows.size()); }
		// A cell's first row: its strongest, and the pixel and size the cell is at.
		const Keypoint& keypoint(std::size_t c) const { return rows.keypoints[start[c]]; }
	};

	// No cells, over `f`'s descriptor kind, image and scale.
	static Cells cellsLike(const Features& f)
	{
		Cells out;
		out.rows = subset(f, {});
		return out;
	}

	// Appends cell `c` of `from` to `to`, every row at `at`'s pixel and size when given (a static
	// feature's medians).
	static void appendCell(Cells& to, const Cells& from, std::size_t c, const Keypoint* at = nullptr)
	{
		to.start.push_back(std::uint32_t(to.rows.size()));
		for (std::uint32_t r = from.start[c]; r < from.end(c); ++r)
		{
			Keypoint k = from.rows.keypoints[r];
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

	// `f`'s features grouped into cells. A cell's rows run by response, then angle in [0, 2π), then
	// size, then their order in `f`, so the grouping does not depend on how the backend listed them.
	// Adds the rows beyond each cell's first to `orientations`.
	static Cells cellsOf(const Features& f, std::uint32_t& orientations)
	{
		std::map<std::pair<long long, long long>, std::vector<std::uint32_t>> byCell; // (y, x) cell -> rows
		for (std::uint32_t i = 0; i < f.size(); ++i)
			byCell[{cell(f.keypoints[i].pixel.y), cell(f.keypoints[i].pixel.x)}].push_back(i);
		const auto rowOrder = [&f](std::uint32_t a, std::uint32_t b)
		{
			const Keypoint& x = f.keypoints[a];
			const Keypoint& y = f.keypoints[b];
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

	// The first `count` cells of `c`, its strongest, with all their rows.
	static Cells firstCells(const Cells& c, std::size_t count)
	{
		Cells out = cellsLike(c.rows);
		for (std::size_t k = 0; k < std::min(count, c.size()); ++k)
			appendCell(out, c, k);
		return out;
	}

	using CellPair = std::pair<std::uint32_t, std::uint32_t>;

	// The cells a match between rows joins, each pair once, ascending.
	static std::vector<CellPair> cellPairs(const MatchResult& m, const Cells& a, const Cells& b)
	{
		std::set<CellPair> pairs;
		for (const Match& r : m.matches)
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

	static std::uint32_t root(std::vector<std::uint32_t>& parent, std::uint32_t i)
	{
		while (parent[i] != i)
		{
			parent[i] = parent[parent[i]];
			i = parent[i];
		}
		return i;
	}

	static void join(std::vector<std::uint32_t>& parent, std::uint32_t a, std::uint32_t b)
	{
		const std::uint32_t ra = root(parent, a);
		const std::uint32_t rb = root(parent, b);
		if (ra != rb)
			parent[std::max(ra, rb)] = std::min(ra, rb);
	}

	// The median of `values`, the mean of the middle two when there is an even count.
	static double median(std::vector<double> values)
	{
		std::sort(values.begin(), values.end());
		const std::size_t n = values.size();
		return n % 2 == 1 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
	}

	// The lower median, for a value that has no mean.
	template <typename T>
	static T lowerMedian(std::vector<T> values)
	{
		std::sort(values.begin(), values.end());
		return values[(values.size() - 1) / 2];
	}

	// A group's place in the capture: its members' median timestamp, then their median ordinal, then
	// its identity, so the order is the same however the groups were listed.
	static auto captureOrder(const capture::CaptureGroup& group)
	{
		std::vector<core::Time> timestamps;
		std::vector<std::size_t> ordinals;
		for (const capture::CaptureMember& m : group.members())
		{
			timestamps.push_back(m.frame.timestamp);
			ordinals.push_back(m.frame.ordinal);
		}
		return std::tuple<core::Time, std::size_t, const std::string&>(lowerMedian(timestamps), lowerMedian(ordinals),
																	   group.identity());
	}

	// The text a track's identity is the digest of: a version line, then each observation's camera
	// identity, length-prefixed so no two different lists spell the same text, and its pixel on the
	// 1/64-pixel grid, as integers so the text does not depend on how a double prints. The version line
	// changes if this does, and every identity with it.
	static std::string canonicalText(const Track& track, const std::vector<View>& views)
	{
		std::string text = "lain feature track 1\n";
		for (const SceneObservation& o : track.observations)
		{
			const std::string& camera = views[o.view].camera.value;
			text += std::to_string(camera.size()) + ":" + camera + " ";
			text += std::to_string(cell(o.pixel.x)) + " " + std::to_string(cell(o.pixel.y)) + "\n";
		}
		return text;
	}

	// The checks on the request alone; the first problem, or nothing.
	static std::optional<std::string> requestProblem(const ExtractionRequest& r)
	{
		const auto positive = [](double v)
		{ return std::isfinite(v) && v > 0; };
		if (r.samples == 0)
			return std::string("a sample count of 0 examines no frame");
		if (r.featureCap < 2)
			return string::format("a feature cap of {} leaves nothing to match", r.featureCap);
		if (!(r.matching.ratio > 0 && r.matching.ratio <= 1))
			return string::format("a ratio of {} is outside (0, 1]", r.matching.ratio);
		if (!positive(r.geometry.angle))
			return string::format("an inlier angle of {} is not positive", r.geometry.angle);
		if (r.minimumPairInliers < 5)
		{
			return string::format("a pair verifies with at least 5 inliers, the fewest a relative pose needs; {} were asked for",
								  r.minimumPairInliers);
		}
		if (r.prescreenFeatures < 2)
			return string::format("a pre-screen of {} features has no second neighbour to test", r.prescreenFeatures);
		if (!positive(r.staticDistance))
			return string::format("a static distance of {} is not positive", r.staticDistance);
		if (!(std::isfinite(r.staticSizeRatio) && r.staticSizeRatio >= 1))
			return string::format("a static size ratio of {} is below 1", r.staticSizeRatio);
		if (!positive(r.localisationPerSize))
			return string::format("a localisation of {} per unit of size is not positive", r.localisationPerSize);
		return std::nullopt;
	}

	// What one camera's task produced.
	struct CameraWork
	{
		// In: the footage positions of its sampled frames, and their identities, in capture order.
		std::vector<std::size_t> positions;
		std::vector<media::FrameRef> frames;
		// Out.
		std::vector<media::FrameRef> extracted; // the frames whose features it used
		Cells statics;							// strongest first
		std::vector<std::uint32_t> support;		// per static feature
		CameraExtraction counts;
		Status failure = Status::Ok;
		std::string detail;
		std::optional<Provenance> extractor;
		std::optional<Provenance> matcher;
	};

	// One camera: its sampled frames' features, then the static ones among them.
	static void extractCamera(const RigFootage& camera, CameraWork& work, const ExtractionRequest& request)
	{
		std::vector<Cells> frames;
		for (std::size_t k = 0; k < work.positions.size(); ++k)
		{
			ExtractResult found;
			{
				const image::Image image = camera.footage.image(work.positions[k]);
				if (!image.valid())
				{
					++work.counts.framesFailed;
					continue;
				}
				found = extract(image, request.scale);
			} // the frame is released before the next is decoded
			if (!found.ok())
			{
				work.failure = found.status;
				work.detail = string::format("camera \"{}\", {}: {}", camera.camera.value, work.frames[k].toString(),
											 found.detail);
				return;
			}
			if (!work.extractor)
				work.extractor = found.provenance;
			work.counts.scale = found.features.scale;
			work.counts.found += std::uint32_t(found.features.size());
			Cells cells = cellsOf(found.features, work.counts.orientations);
			if (cells.size() > request.featureCap)
			{
				work.counts.capped += std::uint32_t(cells.size() - request.featureCap);
				cells = firstCells(cells, request.featureCap);
			}
			frames.push_back(std::move(cells));
			work.extracted.push_back(work.frames[k]);
		}
		if (frames.empty())
			return;

		// The same feature in two frames: a row of each matched, at one pixel and one size in the
		// pixels searched. What joins is the cells the rows belong to.
		std::vector<std::uint32_t> offset;
		std::uint32_t nodes = 0;
		for (const Cells& f : frames)
		{
			offset.push_back(nodes);
			nodes += std::uint32_t(f.size());
		}
		std::vector<std::uint32_t> parent(nodes);
		std::iota(parent.begin(), parent.end(), 0u);
		const double scale = frames.front().rows.scale;
		for (std::size_t i = 0; i < frames.size(); ++i)
		{
			for (std::size_t j = i + 1; j < frames.size(); ++j)
			{
				const MatchResult m = match(frames[i].rows, frames[j].rows, request.matching);
				if (m.status == Status::TooFew)
					continue;
				if (!m.ok())
				{
					work.failure = m.status;
					work.detail = string::format("camera \"{}\": {}", camera.camera.value, m.detail);
					return;
				}
				if (!work.matcher)
					work.matcher = m.provenance;
				for (const Match& match : m.matches)
				{
					const Keypoint& a = frames[i].rows.keypoints[match.a];
					const Keypoint& b = frames[j].rows.keypoints[match.b];
					if (math::length(a.pixel - b.pixel) * scale > request.staticDistance)
						continue;
					const double small = std::min(a.size, b.size);
					const double large = std::max(a.size, b.size);
					if (large > small * request.staticSizeRatio)
						continue;
					join(parent, offset[i] + frames[i].cellOf[match.a], offset[j] + frames[j].cellOf[match.b]);
				}
			}
		}

		// Each cluster of one feature across frames: static when found in at least half of them.
		std::map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>> clusters; // root -> (frame, cell)
		for (std::uint32_t f = 0; f < frames.size(); ++f)
		{
			for (std::uint32_t c = 0; c < frames[f].size(); ++c)
				clusters[root(parent, offset[f] + c)].push_back({f, c});
		}
		const std::size_t needed = (frames.size() + 1) / 2;
		// A static feature: where it sits, and the frame and cell whose orientations it takes.
		struct Static
		{
			Keypoint at;
			std::uint32_t frame = 0;
			std::uint32_t cell = 0;
			std::uint32_t support = 0;
		};
		std::vector<Static> found;
		for (const auto& [r, members] : clusters)
		{
			(void)r;
			std::set<std::uint32_t> seen;
			for (const auto& [f, c] : members)
				seen.insert(f);
			if (seen.size() != members.size())
			{
				work.counts.inconsistent += std::uint32_t(members.size());
				continue;
			}
			if (members.size() < needed)
			{
				work.counts.transient += std::uint32_t(members.size());
				continue;
			}
			std::vector<double> xs, ys, sizes;
			for (const auto& [f, c] : members)
			{
				const Keypoint& k = frames[f].keypoint(c);
				xs.push_back(k.pixel.x);
				ys.push_back(k.pixel.y);
				sizes.push_back(k.size);
			}
			const math::Vec2d centre{median(xs), median(ys)};
			// The member nearest the median pixel speaks for the cluster, with all its orientations;
			// members are in frame order, so a tie goes to the earlier frame.
			std::size_t representative = 0;
			for (std::size_t n = 1; n < members.size(); ++n)
			{
				const auto [f, c] = members[n];
				const auto [rf, rc] = members[representative];
				if (math::length(frames[f].keypoint(c).pixel - centre) < math::length(frames[rf].keypoint(rc).pixel - centre))
					representative = n;
			}
			const auto [rf, rc] = members[representative];
			const Keypoint& k = frames[rf].keypoint(rc);
			found.push_back({{centre, median(sizes), k.angle, k.response}, rf, rc, std::uint32_t(members.size())});
		}

		// Strongest first; and two static features whose medians share a cell are one, as two
		// features of one frame are, so the stronger is kept with its orientations and the other
		// dropped. Stable, so two that tie keep their clusters' order.
		std::stable_sort(found.begin(), found.end(), [](const Static& a, const Static& b)
						 { return stronger(a.at, b.at); });
		std::set<std::pair<long long, long long>> taken;
		work.statics = cellsLike(frames.front().rows);
		for (const Static& f : found)
		{
			if (!taken.insert({cell(f.at.pixel.y), cell(f.at.pixel.x)}).second)
			{
				++work.counts.duplicates;
				continue;
			}
			appendCell(work.statics, frames[f.frame], f.cell, &f.at);
			work.support.push_back(f.support);
		}
		work.counts.staticFeatures = std::uint32_t(work.statics.size());
	}

	// What one pair's task produced.
	struct PairWork
	{
		PairExtraction entry;
		std::vector<CellPair> verified; // (A's static feature, B's)
		Status failure = Status::Ok;
		std::string detail;
		std::optional<Provenance> matcher;
		std::optional<Provenance> geometry;
	};

	// One camera pair: pre-screened, matched and verified.
	static void extractPair(const RigFootage& cameraA, const CameraWork& a, const RigFootage& cameraB, const CameraWork& b,
							PairWork& work, const ExtractionRequest& request)
	{
		const auto fail = [&](const MatchResult& m)
		{
			work.failure = m.status;
			work.detail = string::format("cameras \"{}\" and \"{}\": {}", cameraA.camera.value, cameraB.camera.value, m.detail);
		};
		if (a.statics.size() < 2 || b.statics.size() < 2)
		{
			work.entry.outcome = PairOutcome::TooFew;
			work.entry.detail = string::format("{} and {} static features", a.statics.size(), b.statics.size());
			return;
		}

		// The pre-screen matches each side's strongest features, with all their orientations. When
		// both sides fit inside it, it is the full match.
		const bool whole = a.statics.size() <= request.prescreenFeatures && b.statics.size() <= request.prescreenFeatures;
		MatchResult full;
		std::size_t screened = 0;
		if (whole)
		{
			full = match(a.statics.rows, b.statics.rows, request.matching);
			if (!full.ok())
				return fail(full);
			work.matcher = full.provenance;
			screened = cellPairs(full, a.statics, b.statics).size();
		}
		else
		{
			const Cells strongestA = firstCells(a.statics, request.prescreenFeatures);
			const Cells strongestB = firstCells(b.statics, request.prescreenFeatures);
			const MatchResult prescreen = match(strongestA.rows, strongestB.rows, request.matching);
			if (!prescreen.ok())
				return fail(prescreen);
			work.matcher = prescreen.provenance;
			screened = cellPairs(prescreen, strongestA, strongestB).size();
		}
		work.entry.prescreenMatches = std::uint32_t(screened);
		if (screened < request.prescreenMatches)
		{
			work.entry.outcome = PairOutcome::Skipped;
			work.entry.detail = string::format("{} matches among the strongest {}; the pre-screen asks for {}", screened,
											   request.prescreenFeatures, request.prescreenMatches);
			return;
		}
		if (!whole)
		{
			full = match(a.statics.rows, b.statics.rows, request.matching);
			if (!full.ok())
				return fail(full);
		}
		const std::vector<CellPair> matches = unambiguous(cellPairs(full, a.statics, b.statics), work.entry.ambiguous);
		work.entry.matches = std::uint32_t(matches.size());

		// Each match as a ray from each camera. A pixel its model cannot unproject is a zero ray, which
		// the facade sets aside as unusable.
		std::vector<RayPair> pairs;
		pairs.reserve(matches.size());
		for (const auto& [ca, cb] : matches)
		{
			const math::Vec2d pa = a.statics.keypoint(ca).pixel;
			const math::Vec2d pb = b.statics.keypoint(cb).pixel;
			const Unprojection<double> ra = unproject(cameraA.model, pa.x, pa.y);
			const Unprojection<double> rb = unproject(cameraB.model, pb.x, pb.y);
			pairs.push_back({ra.ok() ? math::Vec3d{ra.x, ra.y, ra.z} : math::Vec3d{0.0},
							 rb.ok() ? math::Vec3d{rb.x, rb.y, rb.z} : math::Vec3d{0.0}});
		}
		const GeometryResult g = relativePose(pairs, request.geometry);
		work.geometry = g.provenance;
		if (!g.ok())
		{
			work.entry.outcome = PairOutcome::Unverified;
			work.entry.detail = g.detail;
			return;
		}
		work.entry.inliers = std::uint32_t(g.inliers.size());
		if (g.inliers.size() < request.minimumPairInliers)
		{
			work.entry.outcome = PairOutcome::Unverified;
			work.entry.detail = string::format("its best relative pose explains {} of {} matches; a pair needs {}",
											   g.inliers.size(), matches.size(), request.minimumPairInliers);
			return;
		}
		work.entry.outcome = PairOutcome::Verified;
		for (const std::uint32_t k : g.inliers)
			work.verified.push_back(matches[k]);
	}

	// --- extractTracks ------------------------------------------------------------------

	ExtractionResult extractTracks(const std::vector<RigFootage>& input, const std::vector<capture::CaptureGroup>& groups,
								   const ExtractionRequest& request)
	{
		const core::Time start = core::Time::now();
		ExtractionResult out;
		out.report.request = request;
		const auto finish = [&](Status status, std::string detail)
		{
			out.status = status;
			out.detail = std::move(detail);
			out.report.elapsed = core::Time::now() - start;
			return std::move(out);
		};

		// 1. Every check that needs no frame.
		if (!canExtract())
			return finish(Status::NoBackend, "this build has no feature extractor (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!canMatch())
			return finish(Status::NoBackend, "this build has no feature matcher (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!canSolveGeometry())
			return finish(Status::NoBackend, "this build has no geometry solver (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (const std::optional<std::string> problem = requestProblem(request))
			return finish(Status::Unsupported, *problem);
		if (request.matching.search == MatchSearch::Approximate && request.execution == ExecutionPolicy::DeterministicDebug)
		{
			return finish(Status::Unsupported, "approximate matching is reproducible only within a tolerance, and "
											   "deterministic debugging asks for the same answer every run");
		}
		bool searchable = false;
		for (const std::string& key : matcherRegistry().keys())
			searchable = searchable || matcherRegistry().create(key)->supports(request.matching.search);
		if (!searchable)
		{
			return finish(Status::Unsupported,
						  string::format("no matcher supports {} search", meta::enums::name(request.matching.search)));
		}
		if (input.size() < 2)
			return finish(Status::TooFew, string::format("{} camera(s): a track joins two", input.size()));
		if (groups.empty())
			return finish(Status::TooFew, "no capture group to sample");

		// The cameras in identity order, so nothing depends on the order they were listed in.
		std::vector<const RigFootage*> cameras;
		for (const RigFootage& c : input)
			cameras.push_back(&c);
		std::stable_sort(cameras.begin(), cameras.end(),
						 [](const RigFootage* a, const RigFootage* b)
						 { return a->camera < b->camera; });
		std::map<capture::CameraIdentity, std::size_t> indexOf;
		std::vector<detail::FootageIndex> footage;
		for (std::size_t c = 0; c < cameras.size(); ++c)
		{
			const RigFootage& camera = *cameras[c];
			if (camera.camera.empty())
				return finish(Status::InvalidInput, "a camera has no identity");
			if (!indexOf.emplace(camera.camera, c).second)
				return finish(Status::InvalidInput, "camera \"" + camera.camera.value + "\" is given more than once");
			const math::Vec2i extent = camera.footage.spec().extent;
			const ImageGeometry& image = camera.model.image();
			if (std::int64_t(image.width) != extent.x || std::int64_t(image.height) != extent.y)
			{
				return finish(Status::InvalidInput,
							  string::format("camera \"{}\"'s model is {}x{} and its footage {}x{}", camera.camera.value,
											 image.width, image.height, extent.x, extent.y));
			}
			footage.emplace_back(camera.footage);
		}
		for (const capture::CaptureGroup& group : groups)
		{
			for (const capture::CaptureMember& m : group.members())
			{
				const auto c = indexOf.find(m.camera);
				if (c == indexOf.end())
				{
					return finish(Status::InvalidInput, string::format("{} names camera \"{}\", which was not given",
																	   group.toString(), m.camera.value));
				}
				if (!footage[c->second].position(m.frame))
				{
					return finish(Status::InvalidInput, string::format("camera \"{}\"'s footage has no {}", m.camera.value,
																	   m.frame.toString()));
				}
			}
		}

		// 2. The groups to sample: evenly across the capture, at the centres of equal bins.
		std::vector<const capture::CaptureGroup*> ordered;
		for (const capture::CaptureGroup& g : groups)
			ordered.push_back(&g);
		std::sort(ordered.begin(), ordered.end(), [](const capture::CaptureGroup* a, const capture::CaptureGroup* b)
				  { return captureOrder(*a) < captureOrder(*b); });
		const std::size_t n = ordered.size();
		const std::size_t k = std::min<std::size_t>(request.samples, n);
		std::vector<CameraWork> work(cameras.size());
		for (std::size_t i = 0; i < k; ++i)
		{
			const capture::CaptureGroup& group = *ordered[(2 * i + 1) * n / (2 * k)];
			out.trackSet.groups.push_back(group);
			for (const capture::CaptureMember& m : group.members())
			{
				const std::size_t c = indexOf.at(m.camera);
				work[c].positions.push_back(*footage[c].position(m.frame));
				work[c].frames.push_back(m.frame);
			}
		}

		// 3. Each camera's static features, one task per camera.
		detail::forEach(cameras.size(), request.execution,
						[&](std::size_t c)
						{ extractCamera(*cameras[c], work[c], request); });
		for (std::size_t c = 0; c < cameras.size(); ++c)
		{
			work[c].counts.frames = std::uint32_t(work[c].positions.size());
			if (work[c].failure != Status::Ok)
				return finish(work[c].failure, work[c].detail);
			if (work[c].extractor && out.report.extractor.backend.empty())
				out.report.extractor = *work[c].extractor;
			if (work[c].matcher && out.report.matcher.backend.empty())
				out.report.matcher = *work[c].matcher;
			out.report.cameras.push_back(work[c].counts);
			out.trackSet.views.push_back({cameras[c]->camera, cameras[c]->model.image(), work[c].extracted});
		}

		// 4. Every camera pair, one task per pair.
		std::vector<PairWork> pairs;
		for (std::uint32_t a = 0; a < cameras.size(); ++a)
		{
			for (std::uint32_t b = a + 1; b < cameras.size(); ++b)
			{
				pairs.emplace_back();
				pairs.back().entry.a = a;
				pairs.back().entry.b = b;
			}
		}
		detail::forEach(pairs.size(), request.execution,
						[&](std::size_t p)
						{
							const std::uint32_t a = pairs[p].entry.a;
							const std::uint32_t b = pairs[p].entry.b;
							extractPair(*cameras[a], work[a], *cameras[b], work[b], pairs[p], request);
						});
		for (const PairWork& p : pairs)
		{
			if (p.failure != Status::Ok)
				return finish(p.failure, p.detail);
			if (p.matcher && out.report.matcher.backend.empty())
				out.report.matcher = *p.matcher;
			if (p.geometry && out.report.geometry.backend.empty())
				out.report.geometry = *p.geometry;
			out.report.pairs.push_back(p.entry);
		}

		// 5. The verified matches joined into tracks, serially in canonical pair order.
		std::vector<std::uint32_t> offset;
		std::uint32_t nodes = 0;
		for (const CameraWork& w : work)
		{
			offset.push_back(nodes);
			nodes += std::uint32_t(w.statics.size());
		}
		std::vector<std::uint32_t> parent(nodes);
		std::iota(parent.begin(), parent.end(), 0u);
		for (const PairWork& p : pairs)
		{
			for (const auto& [ca, cb] : p.verified)
				join(parent, offset[p.entry.a] + ca, offset[p.entry.b] + cb);
		}
		std::map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint32_t>>> components; // root -> (view, feature)
		for (std::uint32_t v = 0; v < work.size(); ++v)
		{
			for (std::uint32_t i = 0; i < work[v].statics.size(); ++i)
				components[root(parent, offset[v] + i)].push_back({v, i});
		}
		for (const auto& [r, members] : components)
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
				++out.report.conflicts;
				out.report.conflictObservations += std::uint32_t(members.size());
				continue;
			}
			Track track;
			for (const auto& [v, i] : members)
			{
				// A static feature's size is its median across frames, in source pixels, so the
				// covariance is in source pixels too, and a coarser search reaches it through the size.
				const Keypoint& k = work[v].statics.keypoint(i);
				const double sigma = request.localisationPerSize * k.size;
				track.observations.push_back({v, k.pixel, work[v].support[i], std::array<double, 3>{sigma * sigma, 0.0, sigma * sigma}});
			}
			track.identity = core::sha256(canonicalText(track, out.trackSet.views)).toString();
			out.trackSet.tracks.push_back(std::move(track));
		}
		std::sort(out.trackSet.tracks.begin(), out.trackSet.tracks.end(),
				  [](const Track& a, const Track& b)
				  { return a.identity < b.identity; });
		assert(std::adjacent_find(out.trackSet.tracks.begin(), out.trackSet.tracks.end(), [](const Track& a, const Track& b)
								  { return a.identity == b.identity; }) == out.trackSet.tracks.end());
		out.report.tracks = std::uint32_t(out.trackSet.tracks.size());
		return finish(Status::Ok, {});
	}
} // namespace lain::camera::feature
