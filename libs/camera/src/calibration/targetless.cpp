#include "lain/camera/calibration/targetless.h"

#include "common.h"
#include "lain/camera/feature/features.h"
#include "lain/camera/feature/geometry.h"
#include "lain/camera/feature/matching.h"
#include "lain/camera/feature/sequenceextraction.h"
#include "lain/camera/observationgraph.h"
#include "lain/camera/projection.h"
#include "reconstruction.h"

#include <lain/string/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace lain::camera::calibration::targetless
{
	using detail::failed;

	// The incremental reconstruction (reconstruction.h), shared with targetless registration. A view
	// there is a frame here.
	using reconstruction::chooseSeed;
	using reconstruction::Closure;
	using reconstruction::Evidence;
	using reconstruction::kPlaceLandmarks;
	using reconstruction::kSeedCandidates;
	using reconstruction::kSeedShared;
	using reconstruction::place;
	using reconstruction::Placement;
	using reconstruction::Seed;
	using reconstruction::Seen;
	using reconstruction::Start;
	using reconstruction::startFrom;
	using reconstruction::Units;
	using reconstruction::unitsOf;
	using reconstruction::Usable;
	using reconstruction::viewsOf;
	using reconstruction::widest;

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The profile a request naming none is judged by: this method's own.
	static constexpr const char* kOwnProfile = "reconstruction-targetless/1";
	// Every inlier test, in source pixels: the sweep's, the seed's and placement's, and a held-out
	// frame's posing. The geometry facades' own 4 mrad at a focal length of 1000, and the sequence
	// extraction's default threshold. Fixed rather than read from the extraction request, because a
	// track set does not say what scale it was searched at, and the two entry points must score alike.
	// Provisional until M9 slice 4's sub-slice 10 measures it.
	static constexpr double kInlierPixels = 4.0;
	// Fewer frames than this and none are held out: a calibration of four frames has too little to
	// reconstruct from and too little to validate with.
	static constexpr std::size_t kMinimumToHoldOut = 5;
	// Fewer frames in the bundle adjustment than this and no model is estimated: two frames are posed
	// by any focal length that explains their pair, so a model needs a third to be constrained.
	static constexpr std::size_t kMinimumFrames = 3;
	// The focal sweep (CONTEXT.md, "Focal sweep"): this many starts, across the focal lengths whose
	// neutral projection spans these fields of view across the image's longest side. Provisional, as
	// kInlierPixels is.
	static constexpr std::size_t kSweepStarts = 3;
	static constexpr double kNarrowestField = 25.0 * 3.14159265358979323846 / 180.0;
	static constexpr double kWidestField = 120.0 * 3.14159265358979323846 / 180.0;
	// A pair of frames a homography explains at this share of the tracks they share is not a seed
	// (SeedPairs). COLMAP's max_H_inlier_ratio; provisional, as kInlierPixels is.
	static constexpr double kHomographyShare = 0.8;

	// A frame's place in capture order, as the sequence extraction sorts frames.
	static auto captureOrder(const media::FrameRef& frame)
	{
		return std::make_tuple(frame.timestamp, frame.ordinal, frame.source.toString());
	}

	// The angle `kInlierPixels` is at `model`'s focal length.
	static double inlierAngle(const CameraModel& model)
	{
		return kInlierPixels / ((model.intrinsics().fx + model.intrinsics().fy) / 2);
	}

	static feature::GeometryRequest geometryRequest(double angle, const Request& request)
	{
		feature::GeometryRequest geometry;
		geometry.angle = angle;
		geometry.seed = request.seed;
		return geometry;
	}

	static bool positiveDefinite(const std::array<double, 3>& c)
	{
		return std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]) && c[0] > 0 && c[2] > 0 &&
			   c[0] * c[2] - c[1] * c[1] > 0;
	}

	static std::string shortId(const std::string& identity)
	{
		return identity.substr(0, 12);
	}

	// The frames examined: every view with a frame, in capture order.
	struct Frames
	{
		std::vector<media::FrameRef> refs;				  // in capture order
		std::vector<std::optional<std::uint32_t>> ofView; // per view: its frame, or none (it did not decode)
		ImageGeometry image;
	};

	// One observation of a track, by frame.
	struct Observed
	{
		std::uint32_t frame = 0;
		math::Vec2d pixel{0.0};
		std::optional<std::array<double, 3>> covariance;
	};

	// A track in capture order, its observations ascending by frame.
	struct Ordered
	{
		const feature::Track* track = nullptr;
		std::vector<Observed> seen;
	};

	// The tracks in capture order: by each one's first frame, then that observation's pixel, then its
	// identity, so the order a track set lists them in, and how they are named, decide nothing.
	static std::vector<Ordered> orderTracks(const feature::TrackSet& set, const Frames& frames)
	{
		std::vector<Ordered> out;
		for (const feature::Track& track : set.tracks)
		{
			Ordered ordered;
			ordered.track = &track;
			for (const feature::SceneObservation& o : track.observations)
				ordered.seen.push_back({*frames.ofView[o.view], o.pixel, o.covariance});
			std::sort(ordered.seen.begin(), ordered.seen.end(), [](const Observed& x, const Observed& y)
					  { return x.frame < y.frame; });
			if (!ordered.seen.empty())
				out.push_back(std::move(ordered));
		}
		std::sort(out.begin(), out.end(), [](const Ordered& x, const Ordered& y)
				  {
					  const Observed& a = x.seen.front();
					  const Observed& b = y.seen.front();
					  return std::make_tuple(a.frame, a.pixel.x, a.pixel.y, x.track->identity) <
							 std::make_tuple(b.frame, b.pixel.x, b.pixel.y, y.track->identity); });
		return out;
	}

	// The frames each track is seen in.
	static std::vector<std::vector<std::uint32_t>> framesOf(const std::vector<Ordered>& tracks)
	{
		std::vector<std::vector<std::uint32_t>> out;
		for (const Ordered& t : tracks)
		{
			std::vector<std::uint32_t> f;
			for (const Observed& o : t.seen)
				f.push_back(o.frame);
			out.push_back(std::move(f));
		}
		return out;
	}

	// The same, without the frames `removed`; a track left with fewer than two frames connects none.
	static std::vector<std::vector<std::uint32_t>> without(const std::vector<std::vector<std::uint32_t>>& units,
														   const std::vector<bool>& removed)
	{
		std::vector<std::vector<std::uint32_t>> out;
		for (const std::vector<std::uint32_t>& u : units)
		{
			std::vector<std::uint32_t> kept;
			for (const std::uint32_t f : u)
			{
				if (!removed[f])
					kept.push_back(f);
			}
			if (kept.size() >= 2)
				out.push_back(std::move(kept));
		}
		return out;
	}

	// Where `track` is seen in frame `f`; it is known to be seen there.
	static const math::Vec2d& pixelIn(const Ordered& track, std::uint32_t f)
	{
		const auto at = std::lower_bound(track.seen.begin(), track.seen.end(), f, [](const Observed& o, std::uint32_t frame)
										 { return o.frame < frame; });
		return at->pixel;
	}

	// The frame pairs that may not seed the reconstruction, decided once on pixels, before any start, so
	// whether a pair seeds depends on no starting model: each pair sharing at least kSeedShared tracks
	// whose shared pixels a homography explains, kHomographyShare of them within kInlierPixels both
	// ways. A homography explains two frames of a camera that turned without moving, and two frames of
	// one plane, whatever the camera's model, so such a pair says nothing about it: started from it, a
	// focal length a few percent out poses a pan as an orbit (CONTEXT.md, "Targetless calibration").
	struct SeedPairs
	{
		std::set<std::pair<std::uint32_t, std::uint32_t>> refused; // (a, b), a < b
		std::uint32_t tested = 0;
		Provenance geometry; // what proposed the homographies
	};

	static SeedPairs seedPairsOf(std::size_t frames, const std::vector<Ordered>& tracks, const Request& request)
	{
		const std::vector<std::vector<std::uint32_t>> units = framesOf(tracks);
		const std::vector<std::vector<std::uint32_t>> ofFrame = unitsOf(frames, units);
		std::vector<GraphEdge> candidates;
		for (const GraphEdge& e : observationGraph(std::uint32_t(frames), units).edges)
		{
			if (e.shared >= kSeedShared)
				candidates.push_back(e);
		}
		std::vector<feature::HomographyResult> results(candidates.size());
		detail::forEach(candidates.size(), request.execution,
						[&](std::size_t i)
						{
							// The pixels of every track the two frames share, in capture order.
							const GraphEdge& e = candidates[i];
							std::vector<std::uint32_t> shared;
							std::set_intersection(ofFrame[e.a].begin(), ofFrame[e.a].end(), ofFrame[e.b].begin(),
												  ofFrame[e.b].end(), std::back_inserter(shared));
							std::vector<feature::PixelPair> pairs;
							for (const std::uint32_t t : shared)
								pairs.push_back({pixelIn(tracks[t], e.a), pixelIn(tracks[t], e.b)});
							feature::HomographyRequest homography;
							homography.threshold = kInlierPixels;
							homography.seed = request.seed;
							results[i] = feature::homography(pairs, homography);
						});
		SeedPairs out;
		out.tested = std::uint32_t(candidates.size());
		for (std::size_t i = 0; i < candidates.size(); ++i)
		{
			if (out.geometry.backend.empty())
				out.geometry = results[i].provenance;
			if (results[i].ok() && double(results[i].inliers.size()) >= kHomographyShare * double(candidates[i].shared))
				out.refused.insert({candidates[i].a, candidates[i].b});
		}
		return out;
	}

	// The edges of `graph` that may seed: those `pairs` did not refuse.
	static std::vector<GraphEdge> seedable(const ObservationGraph& graph, const SeedPairs& pairs)
	{
		std::vector<GraphEdge> out;
		for (const GraphEdge& e : graph.edges)
		{
			if (pairs.refused.count({e.a, e.b}) == 0)
				out.push_back(e);
		}
		return out;
	}

	// Which frames are held out (CONTEXT.md, "Held-out frame"): every k-th in capture order, starting
	// half a stride in (detail::holdOut), each only while the anchor, the first of the widest seeds over
	// every frame among the pairs that may seed, still reaches every other frame it reached without it
	// and every frame held out before it. The anchor's own two frames are never held out. A held-out frame must not decide
	// whether the reconstruction can start.
	static std::vector<bool> holdFramesOut(std::size_t frames, const std::vector<Ordered>& tracks, const SeedPairs& seedPairs,
										   const Request& request)
	{
		const std::vector<std::vector<std::uint32_t>> units = framesOf(tracks);
		const std::vector<std::vector<std::uint32_t>> ofFrame = unitsOf(frames, units);
		const std::vector<Closure> anchors =
			widest(seedable(observationGraph(std::uint32_t(frames), units), seedPairs), units, ofFrame, 1);
		std::vector<bool> removed(frames, false);
		return detail::holdOut(frames, request.heldOutFraction, kMinimumToHoldOut,
							   [&](std::size_t f)
							   {
								   if (anchors.empty())
									   return true;
								   const Closure& anchor = anchors.front();
								   if (f == anchor.a || f == anchor.b)
									   return false;
								   removed[f] = true;
								   const std::vector<std::vector<std::uint32_t>> left = without(units, removed);
								   const Closure after = reconstruction::closure(left, unitsOf(frames, left), anchor.a, anchor.b);
								   for (std::size_t g = 0; g < frames; ++g)
								   {
									   if (anchor.placed[g] && !removed[g] && !after.placed[g])
									   {
										   removed[f] = false;
										   return false;
									   }
								   }
								   return true;
							   });
	}

	// The usable evidence under `model`: each track's observations outside the held-out frames whose
	// pixels unproject, a track with two of them, in capture order. `orderedOf` receives each one's
	// index into `tracks`.
	static Evidence evidenceUnder(const CameraModel& model, std::size_t frames, const std::vector<Ordered>& tracks,
								  const std::vector<bool>& heldOut, std::vector<std::uint32_t>& orderedOf)
	{
		Evidence evidence;
		evidence.models.assign(frames, model);
		for (std::uint32_t t = 0; t < tracks.size(); ++t)
		{
			Usable usable;
			usable.track = tracks[t].track;
			for (const Observed& o : tracks[t].seen)
			{
				if (heldOut[o.frame])
					continue;
				const Unprojection<double> ray = unproject(model, o.pixel.x, o.pixel.y);
				if (ray.ok())
					usable.seen.push_back({o.frame, o.pixel, o.covariance, math::normalize(math::Vec3d{ray.x, ray.y, ray.z})});
			}
			if (usable.seen.size() >= 2)
			{
				evidence.tracks.push_back(std::move(usable));
				orderedOf.push_back(t);
			}
		}
		return evidence;
	}

	// The radius the neutral projection of `model` puts a ray `angle` off the axis at, for a unit focal
	// length: tan for a pinhole, the angle itself for an equidistant fisheye.
	static double neutralRadius(DistortionModel model, double angle)
	{
		return std::visit(
			[angle](const auto& d)
			{
				using Model = std::decay_t<decltype(d)>;
				const DistortedPoint<double> p = Model::project(d.coefficients(), std::sin(angle), 0.0, std::cos(angle), d.domain());
				return p.ok() ? p.x : std::numeric_limits<double>::quiet_NaN();
			},
			neutral(model));
	}

	// One focal length the reconstruction starts from, and its model. A model that is not valid is left
	// unset, and starts nothing.
	struct Candidate
	{
		double focalLength = 0;
		std::optional<CameraModel> model;
	};

	// The focal sweep's starts for `model` over `image` (CONTEXT.md, "Focal sweep"), in ascending focal
	// length: the centres of the geometric thirds of the range from the focal length whose neutral
	// projection spans kWidestField across the image's longest side to the one spanning
	// kNarrowestField, each with fx = fy, the principal point at the image centre, and neutral
	// distortion. A bundle adjustment converges from about 0.7 to 3 times the true focal length, and
	// every focal length in a pinhole's range is within a factor of 1.41 of a start.
	static std::vector<Candidate> sweepStarts(DistortionModel model, const ImageGeometry& image)
	{
		const double half = double(std::max(image.width, image.height)) / 2;
		const double shortest = half / neutralRadius(model, kWidestField / 2);
		const double longest = half / neutralRadius(model, kNarrowestField / 2);
		std::vector<Candidate> out;
		for (std::size_t i = 0; i < kSweepStarts; ++i)
		{
			const double f = shortest * std::pow(longest / shortest, (double(i) + 0.5) / double(kSweepStarts));
			CameraModelParameters p;
			p.image = image;
			p.intrinsics = {f, f, (double(image.width) - 1) / 2, (double(image.height) - 1) / 2};
			p.distortion = neutral(model);
			out.push_back({f, CameraModel::create(p).model});
		}
		return out;
	}

	// The reconstruction over `units` narrowed to the frames `keep` says: their views, renumbered in frame
	// order, and every entry that still has two observations among them, an entry drawn twice being two.
	// `placement` holds a landmark per entry of `units`.
	struct Narrowed
	{
		Evidence evidence;
		Units units;
		Placement placement;
		std::vector<std::uint32_t> frameOf;	  // per view: its view in the evidence narrowed from, a frame in the calibration's own
		std::vector<std::uint32_t> fromEntry; // per entry: its entry in the units narrowed from
	};

	static Narrowed narrow(const Evidence& evidence, const Units& units, const Placement& placement, const std::vector<bool>& keep)
	{
		Narrowed out;
		std::vector<std::optional<std::uint32_t>> viewOf(keep.size());
		for (std::uint32_t f = 0; f < keep.size(); ++f)
		{
			if (!keep[f])
				continue;
			viewOf[f] = std::uint32_t(out.frameOf.size());
			out.frameOf.push_back(f);
			out.evidence.models.push_back(evidence.models[f]);
			out.placement.viewFromWorld.push_back(placement.viewFromWorld[f]);
		}
		for (const std::uint32_t f : placement.order)
		{
			if (viewOf[f])
				out.placement.order.push_back(*viewOf[f]);
		}
		for (std::uint32_t e = 0; e < units.size(); ++e)
		{
			Usable usable;
			usable.track = evidence.tracks[units[e]].track;
			for (Seen s : evidence.tracks[units[e]].seen)
			{
				if (!viewOf[s.view])
					continue;
				s.view = *viewOf[s.view];
				usable.seen.push_back(s);
			}
			if (usable.seen.size() < 2)
				continue;
			out.units.push_back(std::uint32_t(out.evidence.tracks.size()));
			out.evidence.tracks.push_back(std::move(usable));
			out.placement.landmarks.push_back(placement.landmarks[e]);
			out.fromEntry.push_back(e);
		}
		return out;
	}

	// What a bundle adjustment starts from over the frames `keep` holds (reconstruction::startFrom): the
	// reconstruction narrowed to them, then narrowed again without every frame the start leaves seeing
	// fewer than kPlaceLandmarks landmarks, until it leaves none. The reference is the first frame placed
	// that is kept. Unset when fewer than kMinimumFrames frames are left; `keep` receives the frames that
	// are. The adjustment and every bootstrap resample start through this one routine, so they cannot
	// disagree about which observations a start can see.
	struct Started
	{
		Narrowed narrowed;
		Start start;
		std::uint32_t reference = 0; // a view of `narrowed`
	};

	static std::optional<Started> startOver(const Evidence& evidence, const Units& units, const Placement& placement,
											std::vector<bool>& keep)
	{
		for (;;)
		{
			if (std::size_t(std::count(keep.begin(), keep.end(), true)) < kMinimumFrames)
				return std::nullopt;
			Started out;
			out.narrowed = narrow(evidence, units, placement, keep);
			const std::uint32_t first = *std::find_if(placement.order.begin(), placement.order.end(), [&](std::uint32_t f)
													  { return bool(keep[f]); });
			out.reference = std::uint32_t(std::find(out.narrowed.frameOf.begin(), out.narrowed.frameOf.end(), first) -
										  out.narrowed.frameOf.begin());
			out.start = startFrom(out.narrowed.evidence, out.narrowed.units, out.narrowed.placement, out.reference);
			if (out.start.starved.empty())
				return out;
			for (const std::uint32_t v : out.start.starved)
				keep[out.narrowed.frameOf[v]] = false;
		}
	}

	// The bundle adjustment's problem from `start`: every refined landmark and its kept observations,
	// over one model shared by every frame. `entryOf` receives each problem landmark's entry.
	static refinement::Problem problemOf(const Narrowed& n, const Start& start, std::uint32_t reference, const CameraModel& model,
										 const refinement::ModelFreedom& freedom, const Settings& settings,
										 std::vector<std::uint32_t>& entryOf)
	{
		refinement::Problem problem;
		problem.models = {model};
		problem.modelOf.assign(n.evidence.models.size(), 0);
		if (freedom.any())
			problem.modelFreedom = {freedom};
		problem.cameraFromReference = start.viewFromReference;
		problem.reference = reference;
		problem.loss = settings.loss;
		for (std::uint32_t e = 0; e < n.units.size(); ++e)
		{
			if (!start.landmarks[e])
				continue;
			const std::uint32_t landmark = std::uint32_t(problem.landmarks.size());
			problem.landmarks.push_back(*start.landmarks[e]);
			entryOf.push_back(e);
			const std::vector<Seen>& members = n.evidence.tracks[n.units[e]].seen;
			for (std::size_t m = 0; m < members.size(); ++m)
			{
				if (start.kept[e][m])
					problem.landmarkObservations.push_back({members[m].view, landmark, members[m].pixel, members[m].covariance});
			}
		}
		return problem;
	}

	// The adjustment's evidence under its result, for its resamples to draw from: over its frames and the
	// refined model, a track per refined landmark in their order, each holding the observations the
	// adjustment fitted, their rays through the refined model; one whose pixel does not unproject through
	// it is left out. A landmark left with fewer than two observations is still listed, since a draw
	// names a landmark by its place; narrowing leaves it out.
	static Evidence fittedUnder(const CameraModel& model, const Narrowed& narrowed, const Start& start,
								const std::vector<std::uint32_t>& entryOf)
	{
		Evidence out;
		out.models.assign(narrowed.evidence.models.size(), model);
		for (const std::uint32_t e : entryOf)
		{
			const Usable& track = narrowed.evidence.tracks[narrowed.units[e]];
			Usable fitted;
			fitted.track = track.track;
			for (std::size_t m = 0; m < track.seen.size(); ++m)
			{
				if (!start.kept[e][m])
					continue;
				Seen s = track.seen[m];
				const Unprojection<double> ray = unproject(model, s.pixel.x, s.pixel.y);
				if (!ray.ok())
					continue;
				s.ray = math::normalize(math::Vec3d{ray.x, ray.y, ray.z});
				fitted.seen.push_back(s);
			}
			out.tracks.push_back(std::move(fitted));
		}
		return out;
	}

	// An outlier, keyed by its unit in the evidence the adjustment was narrowed from, which is capture
	// order.
	struct NamedOutlier
	{
		std::uint32_t unit = 0;
		Outlier outlier;
	};

	// The adjustment's evidence under its result, as fittedUnder, without what the result does not
	// explain: each observation whose whitened residual exceeds `scale` is left out and named into
	// `pruned` with it, as is one the result cannot project or whose pixel the refined model cannot
	// unproject, named with an infinite residual. An observation the adjustment's start left out is
	// named into `leftOut`, with an infinite residual. Every landmark's track is listed, as fittedUnder
	// lists them, however few observations it keeps; narrowing leaves out one with fewer than two.
	static Evidence explainedUnder(const CameraModel& model, const Narrowed& narrowed, const Start& start,
								   const std::vector<std::uint32_t>& entryOf, const refinement::Solution& result,
								   const refinement::NoiseModel& noise, double scale, std::vector<NamedOutlier>& pruned,
								   std::vector<NamedOutlier>& leftOut)
	{
		Evidence out;
		out.models.assign(narrowed.evidence.models.size(), model);
		for (std::size_t l = 0; l < entryOf.size(); ++l)
		{
			const std::uint32_t e = entryOf[l];
			const Usable& track = narrowed.evidence.tracks[narrowed.units[e]];
			Usable explained;
			explained.track = track.track;
			for (std::size_t m = 0; m < track.seen.size(); ++m)
			{
				Seen s = track.seen[m];
				if (!start.kept[e][m])
				{
					leftOut.push_back({narrowed.fromEntry[e],
									   {track.track->identity, narrowed.frameOf[s.view], std::numeric_limits<double>::infinity()}});
					continue;
				}
				const math::Vec3d q = result.cameraFromReference[s.view].apply(result.landmarks[l]);
				const Projection<double> predicted = project(model, q.x, q.y, q.z);
				const Unprojection<double> ray = unproject(model, s.pixel.x, s.pixel.y);
				const double rmsWhitened =
					predicted.ok() && ray.ok()
						? std::sqrt(detail::whitenedSquared(math::Vec2d{predicted.u, predicted.v} - s.pixel, s.covariance, noise))
						: std::numeric_limits<double>::infinity();
				if (rmsWhitened > scale)
				{
					pruned.push_back({narrowed.fromEntry[e], {track.track->identity, narrowed.frameOf[s.view], rmsWhitened}});
					continue;
				}
				s.ray = math::normalize(math::Vec3d{ray.x, ray.y, ray.z});
				explained.seen.push_back(s);
			}
			out.tracks.push_back(std::move(explained));
		}
		return out;
	}

	// `again`, started from evidence explainedUnder took from `first`, with its frames and entries named
	// as `first`'s are: frames among the calibration's, entries among the units `first` was narrowed
	// from.
	static Started rebased(Started again, const Narrowed& first, const std::vector<std::uint32_t>& firstEntryOf)
	{
		for (std::uint32_t& f : again.narrowed.frameOf)
			f = first.frameOf[f];
		for (std::uint32_t& e : again.narrowed.fromEntry)
			e = first.fromEntry[firstEntryOf[e]];
		return again;
	}

	// A bundle adjustment started from an adjustment's result, over `fitted`, whose views and tracks are
	// the result's frames and landmarks: the `units` at their refined positions, a landmark listed twice
	// being two, started as the adjustment was (startOver) but under `fitted`'s model. Its domain may end
	// where the adjustment's start's did not, so an observation the result cannot project is left out, as
	// the adjustment's own start leaves one out, rather than failing at the first evaluation. Unset when
	// fewer than kMinimumFrames frames are left.
	static std::optional<Started> restartFrom(const Evidence& fitted, const refinement::Solution& result,
											  const std::vector<std::uint32_t>& order, const Units& units)
	{
		Placement placement;
		placement.viewFromWorld.assign(result.cameraFromReference.begin(), result.cameraFromReference.end());
		placement.order = order;
		for (const std::uint32_t l : units)
			placement.landmarks.push_back(result.landmarks[l]);
		std::vector<bool> keep(fitted.models.size(), true);
		return startOver(fitted, units, placement, keep);
	}

	// One bootstrap resample adjusted from the result (restartFrom): the drawn landmarks, a landmark drawn
	// twice being two. The refined model's parameters, unchecked, or nothing when too few frames are left
	// or the adjustment gives no usable solution.
	static std::optional<CameraModelParameters> resampled(const Evidence& fitted, const refinement::Solution& result,
														  const std::vector<std::uint32_t>& order,
														  const refinement::ModelFreedom& freedom, const Settings& settings,
														  const Units& draw)
	{
		const std::optional<Started> started = restartFrom(fitted, result, order, draw);
		if (!started)
			return std::nullopt;
		std::vector<std::uint32_t> entryOf;
		const refinement::Problem problem = problemOf(started->narrowed, started->start, started->reference,
													  fitted.models.front(), freedom, settings, entryOf);
		const refinement::Solution solution = refinement::refine(problem);
		if (!solution.usable() || solution.models.size() != 1)
			return std::nullopt;
		return solution.models.front();
	}

	// One held-out frame's evidence: the landmarks it sees, where it saw them, and how precisely each
	// observation says it was located.
	struct HeldOutFrame
	{
		std::vector<math::Vec3d> points; // in the reference frame
		std::vector<math::Vec2d> pixels;
		std::vector<std::optional<std::array<double, 3>>> covariances;
	};

	struct FrameMeasure
	{
		bool posed = false;
		std::uint32_t residuals = 0, outliers = 0;
		double sumAngles = 0, sumPixels = 0, worstPixels = 0;
		// The squared residuals that project, whitened by their covariances; `unstated` when one of
		// the frame's measured observations states none.
		double sumWhitened = 0;
		std::uint32_t whitened = 0;
		bool unstated = false;
		std::vector<math::Vec2d> measured; // the pixels within the inlier angle
	};

	// A held-out frame posed against the refined landmarks with the model held, through rays from the
	// model, and its observations measured through lain's projection: those within the pose's inlier
	// angle, by the angle between the observed ray and the predicted point; the rest counted.
	static FrameMeasure measureFrame(const CameraModel& model, const HeldOutFrame& frame, const Request& request)
	{
		FrameMeasure out;
		const double angle = inlierAngle(model);
		std::vector<feature::PointRay> pointRays;
		std::vector<math::Vec2d> pixels;
		std::vector<std::optional<std::array<double, 3>>> covariances;
		for (std::size_t i = 0; i < frame.points.size(); ++i)
		{
			const Unprojection<double> ray = unproject(model, frame.pixels[i].x, frame.pixels[i].y);
			if (!ray.ok())
				continue;
			pointRays.push_back({frame.points[i], {ray.x, ray.y, ray.z}});
			pixels.push_back(frame.pixels[i]);
			covariances.push_back(frame.covariances[i]);
		}
		const feature::GeometryResult posed = feature::absolutePose(pointRays, geometryRequest(angle, request));
		if (!posed.ok())
			return out;
		out.posed = true;
		for (std::size_t i = 0; i < pointRays.size(); ++i)
		{
			const math::Vec3d q = posed.pose->apply(pointRays[i].point);
			const double a = detail::angleBetween(pointRays[i].ray, q);
			if (!(a <= angle))
			{
				++out.outliers;
				continue;
			}
			++out.residuals;
			out.sumAngles += a * a;
			out.measured.push_back(pixels[i]);
			const Projection<double> predicted = project(model, q.x, q.y, q.z);
			if (predicted.ok())
			{
				const math::Vec2d r = math::Vec2d{predicted.u, predicted.v} - pixels[i];
				const double d = math::length(r);
				out.sumPixels += d * d;
				out.worstPixels = std::max(out.worstPixels, d);
				if (covariances[i])
				{
					out.sumWhitened += detail::whitenedSquared(r, covariances[i], refinement::NoiseModel{});
					++out.whitened;
				}
				else
					out.unstated = true;
			}
		}
		return out;
	}

	// --- the checks ------------------------------------------------------------------

	// The checks that read the track set: one camera's views, each of at most one frame and none twice,
	// with one image geometry; each observation of a view with a frame, one per view, its covariance
	// positive definite; each track's identity once. On failure the report failed.
	static std::optional<Report> checkTrackSet(Report& report, const feature::TrackSet& set, Frames& out, core::Time start)
	{
		if (set.views.empty())
			return failed(report, start, Failure::NoFootage, "the track set has no views");
		const capture::CameraIdentity& camera = set.views.front().camera;
		if (camera.empty())
			return failed(report, start, Failure::InvalidDataset, "the camera has no identity");
		std::vector<std::pair<media::FrameRef, std::uint32_t>> given;
		for (std::uint32_t v = 0; v < set.views.size(); ++v)
		{
			const feature::View& view = set.views[v];
			if (view.camera != camera)
				return failed(report, start, Failure::InvalidDataset,
							  "views of two cameras, \"" + camera.value + "\" and \"" + view.camera.value +
								  "\": a targetless calibration is of one camera");
			if (view.image != set.views.front().image)
				return failed(report, start, Failure::InvalidDataset,
							  lain::string::format("view {} is {}x{} and view 0 is {}x{}", v, view.image.width, view.image.height,
												   set.views.front().image.width, set.views.front().image.height));
			if (view.frames.size() > 1)
				return failed(report, start, Failure::InvalidDataset,
							  lain::string::format("view {} holds {} frames: a moving camera's view is one frame", v,
												   view.frames.size()));
			if (!view.frames.empty())
				given.push_back({view.frames.front(), v});
		}
		if (given.empty())
			return failed(report, start, Failure::NoFootage, "no view of the track set holds a frame");
		std::sort(given.begin(), given.end(), [](const auto& x, const auto& y)
				  { return captureOrder(x.first) < captureOrder(y.first); });
		out.image = set.views.front().image;
		out.ofView.assign(set.views.size(), std::nullopt);
		for (std::size_t i = 0; i < given.size(); ++i)
		{
			if (i > 0 && given[i].first == given[i - 1].first)
				return failed(report, start, Failure::InvalidDataset, given[i].first.toString() + " is given twice");
			out.ofView[given[i].second] = std::uint32_t(out.refs.size());
			out.refs.push_back(given[i].first);
		}

		std::vector<const std::string*> identities;
		for (const feature::Track& track : set.tracks)
		{
			identities.push_back(&track.identity);
			std::set<std::uint32_t> views;
			for (const feature::SceneObservation& o : track.observations)
			{
				if (o.view >= set.views.size())
					return failed(report, start, Failure::InvalidDataset,
								  lain::string::format("track {} names view {}, of {}", shortId(track.identity), o.view,
													   set.views.size()));
				if (!out.ofView[o.view])
					return failed(report, start, Failure::InvalidDataset,
								  lain::string::format("track {} observes view {}, which holds no frame", shortId(track.identity),
													   o.view));
				if (!views.insert(o.view).second)
					return failed(report, start, Failure::InvalidDataset,
								  lain::string::format("track {} holds two observations of view {}", shortId(track.identity), o.view));
				if (o.covariance && !positiveDefinite(*o.covariance))
					return failed(report, start, Failure::InvalidDataset,
								  "track " + shortId(track.identity) + " has a covariance that is not positive definite");
			}
		}
		std::sort(identities.begin(), identities.end(), [](const std::string* x, const std::string* y)
				  { return *x < *y; });
		for (std::size_t i = 1; i < identities.size(); ++i)
		{
			if (*identities[i] == *identities[i - 1])
				return failed(report, start, Failure::InvalidDataset, "track " + shortId(*identities[i]) + " is given twice");
		}
		return std::nullopt;
	}

	// The checks on the request that need nothing of the evidence but its geometry: the profile, the
	// imported model, and a refiner able to estimate the requested model unless it is held. The
	// backends a track set needs (a geometry solver and a refiner) come between the imported model and
	// the estimate.
	static std::optional<Report> checkRequest(Report& report, const Request& request, const ImageGeometry& image,
											  bool extracting, core::Time start)
	{
		if (std::optional<Report> failure = detail::resolveProfile(report, request, kOwnProfile, start))
			return failure;
		if (std::optional<Report> failure = detail::checkImported(report, request, image, start))
			return failure;
		if (extracting && !feature::canExtract())
			return failed(report, start, Failure::NoFeatureExtractor,
						  "this build has no feature extractor (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (extracting && !feature::canMatch())
			return failed(report, start, Failure::NoFeatureMatcher,
						  "this build has no feature matcher (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!feature::canSolveGeometry())
			return failed(report, start, Failure::NoGeometrySolver,
						  "this build has no geometry solver (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!refinement::canRefine())
			return failed(report, start, Failure::NoRefiner,
						  "this build has no bundle adjustment (configure with -DLAIN_CAMERA_CERES=ON)");
		if (request.importedPolicy != ImportedModelPolicy::HoldAndValidate && !refinement::canEstimate(request.model))
			return failed(report, start, Failure::NoEstimator,
						  "no registered refiner can estimate " + std::string(displayName(request.model)));
		return std::nullopt;
	}

	// --- one start ----------------------------------------------------------------

	// How far a start got before it failed. A later stage says more about the footage, so when every
	// start fails, the one that got furthest is reported first.
	enum class Stage
	{
		Started, // a starting model, and no seed posed from it
		Seeded,	 // a seed pair posed
		Placed,	 // enough frames placed to adjust
		Refined, // a usable bundle adjustment
	};

	// One start's reconstruction and bundle adjustment (steps 5 and 6): everything from a starting model
	// to the refined model the rest of the calibration validates, or why there is none.
	struct Fit
	{
		Stage stage = Stage::Started;
		std::vector<FailureReason> failures; // empty when it ended in a model, the most specific first
		// What the reconstruction reports: the usable tracks, the seed, the placement, the frames left
		// out, the observations behind, the landmarks and the refinement.
		TargetlessDiagnostics diagnostics;
		Provenance geometry; // what proposed its poses
		Provenance refiner;
		std::vector<std::uint32_t> orderedOf; // per usable track, which is a unit of its own: its place in capture order
		std::optional<Started> started;
		refinement::ModelFreedom freedom;
		refinement::Problem problem;
		refinement::Solution solution;
		std::vector<std::uint32_t> entryOf; // per problem landmark: its entry
		std::optional<CameraModel> model;
		// When there is a second adjustment, the outliers its evidence no longer holds: what the first's
		// start left out, and what the first's result did not explain.
		std::vector<NamedOutlier> named;
		// What the start is ranked by: its first adjustment's, over every observation it fitted.
		std::uint32_t placed = 0;	// frames placed and kept for the first adjustment
		double costPerResidual = 0; // its final cost over its residuals
	};

	static Fit fitFrom(const CameraModel& startModel, std::size_t n, const std::vector<Ordered>& tracks,
					   const std::vector<bool>& heldOut, const SeedPairs& seedPairs, const Settings& settings,
					   const Request& request)
	{
		const bool hold = request.importedPolicy == ImportedModelPolicy::HoldAndValidate;
		const double startAngle = inlierAngle(startModel);
		Fit out;
		TargetlessDiagnostics& d = out.diagnostics;
		const auto fail = [&out](Failure failure, std::string detail)
		{
			out.failures.push_back({failure, std::move(detail)});
			return std::move(out);
		};

		// 5. The reconstruction, through the shared module.
		const Evidence evidence = evidenceUnder(startModel, n, tracks, heldOut, out.orderedOf);
		d.tracksUsable = std::uint32_t(evidence.tracks.size());
		Units all(evidence.tracks.size());
		std::iota(all.begin(), all.end(), 0u);
		const std::vector<std::vector<std::uint32_t>> unitFrames = viewsOf(evidence, all);
		const std::vector<Closure> seeds = widest(seedable(observationGraph(std::uint32_t(n), unitFrames), seedPairs),
												  unitFrames, unitsOf(n, unitFrames), kSeedCandidates);
		if (seeds.empty())
			return fail(Failure::InitialisationFailed,
						lain::string::format("no two frames share the {} tracks a reconstruction starts from", kSeedShared));
		const std::optional<Seed> seed = chooseSeed(evidence, all, seeds, geometryRequest(startAngle, request), out.geometry);
		if (!seed)
			return fail(Failure::InitialisationFailed, "no pair of frames could be posed from the tracks it shares");
		out.stage = Stage::Seeded;
		d.seed = {seed->a, seed->b};
		d.seedAngle = seed->angle;
		d.seedInliers = seed->inliers;
		d.seedAtInfinity = seed->atInfinity;
		d.seedBelowFloor = seed->belowFloor;
		// The seed's median parallax over every inlier, a point at infinity counting as none, is below
		// the least a landmark triangulates at exactly when most of its inliers are at infinity. The
		// median over the ones that triangulate would not do: none triangulates below that least, so it
		// clears it whenever any match triangulates, and moving content across a pan has real parallax.
		if (2 * seed->atInfinity >= seed->inliers)
			return fail(Failure::InitialisationFailed,
						lain::string::format("the widest seed, frames {} and {}, has no parallax: {} of its {} matches meet at "
											 "less than the {:.3g} mrad a landmark needs, so they are points at infinity; "
											 "footage that only turns has nothing to reconstruct from",
											 seed->a, seed->b, seed->atInfinity, seed->inliers,
											 feature::GeometryRequest{}.angle * 1000));
		const Placement placement = place(evidence, all, *seed, geometryRequest(startAngle, request));
		d.placement = placement.order;

		// The frames placed, narrowed until none is starved by the cheirality filter.
		std::vector<bool> keep(n, false);
		for (std::uint32_t f = 0; f < n; ++f)
			keep[f] = placement.viewFromWorld[f].has_value();
		out.started = startOver(evidence, all, placement, keep);
		out.placed = std::uint32_t(std::count(keep.begin(), keep.end(), true));
		for (std::uint32_t f = 0; f < n; ++f)
		{
			if (!heldOut[f] && !keep[f])
				d.unplaced.push_back(f);
		}
		if (!out.started)
			return fail(Failure::TooFewViews,
						lain::string::format("{} of {} frames could be placed; a calibration needs at least {}", out.placed,
											 n - std::size_t(std::count(heldOut.begin(), heldOut.end(), true)), kMinimumFrames));
		out.stage = Stage::Placed;
		d.behind = out.started->start.behind;

		// 6. The bundle adjustment, then a second from its result without the observations it does not
		// explain. Robust as its loss is, the first is still pulled by them: on rendered footage with a
		// printed board in close views it ended 2.7 to 4.4 px out on half the seeds, its principal point
		// off, an error the held-out frames' poses absorb so that the verdict does not see it; the second
		// ended within 0.6 px on every seed. The start is ranked by its first adjustment, since a second
		// leaves out whatever its own start's result does not explain: from a wrong minimum, that is
		// whatever disagrees with it, and what is left is fitted about as closely as the right start fits
		// its own (repeated, it left the long start 56% of the observations, at 0.32 per residual against
		// the right start's 0.34).
		if (!hold)
		{
			out.freedom.focalLength = true;
			out.freedom.coefficients = true;
			out.freedom.principalPoint = settings.freePrincipalPoint;
		}
		const auto adjusted = [&](const Started& started)
		{
			out.entryOf.clear();
			out.problem = problemOf(started.narrowed, started.start, started.reference, *out.model, out.freedom, settings,
									out.entryOf);
			out.solution = refinement::refine(out.problem);
			out.refiner = out.solution.provenance;
			refinement::Summary summary;
			summary.status = out.solution.status;
			summary.detail = out.solution.detail;
			summary.iterations = out.solution.iterations;
			summary.initialCost = out.solution.initialCost;
			summary.finalCost = out.solution.finalCost;
			summary.residuals = std::uint32_t(out.problem.landmarkObservations.size());
			summary.landmarks = std::uint32_t(out.problem.landmarks.size());
			summary.elapsed = out.solution.elapsed;
			summary.noise = out.problem.noise;
			summary.loss = out.problem.loss;
			d.refinement = summary;
			std::optional<FailureReason> failure;
			if (out.solution.status == refinement::Status::ResourceExhausted)
				failure = FailureReason{Failure::ResourceExhausted, out.solution.detail};
			else if (!out.solution.usable())
				failure = FailureReason{Failure::RefinementFailed, out.solution.detail.empty()
																	   ? "the bundle adjustment produced no usable solution"
																	   : out.solution.detail};
			else if (out.solution.cameraFromReference.size() != out.problem.cameraFromReference.size() ||
					 out.solution.landmarks.size() != out.problem.landmarks.size() || out.solution.models.size() != 1)
				failure = FailureReason{Failure::RefinementFailed, "the refiner returned a solution of the wrong shape"};
			else
				out.stage = Stage::Refined;
			if (!failure && !hold)
			{
				ModelResult created = CameraModel::create(out.solution.models.front());
				if (!created.model)
				{
					for (const ModelDiagnostic& diagnostic : created.diagnostics)
						out.failures.push_back({Failure::InvalidModel, diagnostic.detail});
					failure = FailureReason{Failure::InvalidModel, "the estimate is not a valid camera model"};
				}
				else
					out.model = std::move(created.model);
			}
			return failure;
		};
		out.model = hold ? request.imported : std::optional<CameraModel>{startModel};
		if (std::optional<FailureReason> failure = adjusted(*out.started))
		{
			out.model.reset();
			return fail(failure->failure, std::move(failure->detail));
		}
		out.costPerResidual = out.problem.landmarkObservations.empty()
								  ? 0.0
								  : out.solution.finalCost / double(out.problem.landmarkObservations.size());

		std::vector<NamedOutlier> pruned, leftOut;
		const Evidence explained = explainedUnder(*out.model, out.started->narrowed, out.started->start, out.entryOf,
												  out.solution, out.problem.noise, settings.loss.scale, pruned, leftOut);
		d.pruned = std::uint32_t(pruned.size());
		if (!pruned.empty())
		{
			out.named = std::move(pruned);
			out.named.insert(out.named.end(), leftOut.begin(), leftOut.end());
			Units fitted(explained.tracks.size());
			std::iota(fitted.begin(), fitted.end(), 0u);
			std::optional<Started> again =
				restartFrom(explained, out.solution, out.started->narrowed.placement.order, fitted);
			if (!again)
			{
				out.model.reset();
				return fail(Failure::TooFewViews,
							lain::string::format("without the {} observations the bundle adjustment's result does not explain, "
												 "fewer than {} frames see enough landmarks to adjust again",
												 d.pruned, kMinimumFrames));
			}
			out.started = rebased(std::move(*again), out.started->narrowed, out.entryOf);
			if (std::optional<FailureReason> failure = adjusted(*out.started))
			{
				out.model.reset();
				return fail(failure->failure, std::move(failure->detail));
			}
			// A frame the second start left seeing too few landmarks is left out of the result.
			d.unplaced.clear();
			std::vector<bool> adjustedFrame(n, false);
			for (const std::uint32_t f : out.started->narrowed.frameOf)
				adjustedFrame[f] = true;
			for (std::uint32_t f = 0; f < n; ++f)
			{
				if (!heldOut[f] && !adjustedFrame[f])
					d.unplaced.push_back(f);
			}
			d.behind += out.started->start.behind;
		}
		d.landmarks = std::uint32_t(out.entryOf.size());
		d.untriangulated = d.tracksUsable - d.landmarks;
		return out;
	}

	// What a start's reconstruction reports, into the report's diagnostics: everything but the frames,
	// the tracks given, the sweep, the extraction and the outliers, which the calibration fills.
	static void reportReconstruction(TargetlessDiagnostics& into, const TargetlessDiagnostics& from)
	{
		into.tracksUsable = from.tracksUsable;
		into.seed = from.seed;
		into.seedAngle = from.seedAngle;
		into.seedInliers = from.seedInliers;
		into.seedAtInfinity = from.seedAtInfinity;
		into.seedBelowFloor = from.seedBelowFloor;
		into.placement = from.placement;
		into.unplaced = from.unplaced;
		into.behind = from.behind;
		into.landmarks = from.landmarks;
		into.untriangulated = from.untriangulated;
		into.pruned = from.pruned;
		into.refinement = from.refinement;
	}

	// --- the calibration --------------------------------------------------------------

	// Everything after the checks. `report` arrives prepared, with its frames in place.
	static Report analyse(Report report, const Frames& frames, const std::vector<Ordered>& tracks, const Settings& settings,
						  const Request& request, core::Time start)
	{
		const std::size_t n = frames.refs.size();
		Diagnostics& diagnostics = report.diagnostics;
		TargetlessDiagnostics& targetless = std::get<TargetlessDiagnostics>(diagnostics.method);
		TargetlessRecord& record = std::get<TargetlessRecord>(report.reproducibility.method);
		const bool hold = request.importedPolicy == ImportedModelPolicy::HoldAndValidate;

		// The pairs that may seed, once, on pixels. When a homography explains every pair that shares
		// enough to seed, no start can: the footage turned without moving, or saw one plane.
		const SeedPairs seedPairs = seedPairsOf(n, tracks, request);
		targetless.seedPairs = seedPairs.tested;
		targetless.seedPairsRefused = std::uint32_t(seedPairs.refused.size());
		record.geometry = seedPairs.geometry;
		if (seedPairs.tested > 0 && seedPairs.refused.size() == seedPairs.tested)
			return failed(std::move(report), start, Failure::InitialisationFailed,
						  lain::string::format("a homography explains {:.0f}% or more of the tracks shared by each of the {} pairs "
											   "of frames that could seed the reconstruction, so the camera turned without "
											   "moving, or saw one plane; footage that does neither says nothing of its model",
											   kHomographyShare * 100, seedPairs.tested));

		// 3. The held-out frames.
		const std::vector<bool> heldOut = holdFramesOut(n, tracks, seedPairs, request);
		for (std::uint32_t f = 0; f < n; ++f)
		{
			if (heldOut[f])
				diagnostics.heldOut.push_back(f);
		}
		diagnostics.viewsHeldOut = std::uint32_t(diagnostics.heldOut.size());

		// 4. The starts: an imported model alone, or the focal sweep's.
		std::vector<Candidate> starts;
		bool swept = false;
		if (hold)
			starts.push_back({request.imported->intrinsics().fx, request.imported});
		else if (const std::optional<CameraModelParameters> initial = detail::initialFrom(report, request, frames.image))
		{
			ModelResult created = CameraModel::create(*initial);
			if (!created.model)
				return failed(std::move(report), start, Failure::InvalidModel,
							  "the imported model, seeding the requested distortion model, is not a valid camera model");
			starts.push_back({created.model->intrinsics().fx, std::move(created.model)});
		}
		else
		{
			starts = sweepStarts(request.model, frames.image);
			swept = true;
		}

		// 5 and 6. Each start reconstructed and adjusted on its own. The one whose adjustment kept the most
		// frames wins, as the seed is the one whose placement reaches the most: a cost per residual is only
		// comparable over the same footage, and a start that reconstructs three frames of nineteen fits
		// them more closely than the right one fits all of them. Among those, the lowest final cost per
		// residual, since a wrong minimum costs several times a right one; ties to the shorter focal length.
		std::vector<Fit> fits(starts.size());
		detail::forEach(starts.size(), request.execution,
						[&](std::size_t i)
						{
							if (starts[i].model)
								fits[i] = fitFrom(*starts[i].model, n, tracks, heldOut, seedPairs, settings, request);
							else
								fits[i].failures.push_back(
									{Failure::InitialisationFailed, lain::string::format("a focal length of {:.4g} gives no valid {} model",
																						 starts[i].focalLength, displayName(request.model))});
						});
		std::optional<std::uint32_t> winner;
		for (std::uint32_t i = 0; i < fits.size(); ++i)
		{
			if (record.geometry.backend.empty())
				record.geometry = fits[i].geometry;
			if (fits[i].model &&
				(!winner || fits[i].placed > fits[*winner].placed ||
				 (fits[i].placed == fits[*winner].placed && fits[i].costPerResidual < fits[*winner].costPerResidual)))
				winner = i;
			if (swept)
			{
				SweepStart tried;
				tried.focalLength = starts[i].focalLength;
				tried.placed = fits[i].placed;
				if (fits[i].model)
					tried.outcome = fits[i].costPerResidual;
				else
					tried.outcome = fits[i].failures.front();
				targetless.sweep.push_back(std::move(tried));
			}
		}
		if (!winner)
		{
			// Every start failed: the one that got furthest first, with what it reconstructed, then the
			// others in order. A sweep's start says which it was.
			std::uint32_t furthest = 0;
			for (std::uint32_t i = 1; i < fits.size(); ++i)
			{
				if (fits[i].stage > fits[furthest].stage)
					furthest = i;
			}
			reportReconstruction(targetless, fits[furthest].diagnostics);
			record.refiner = fits[furthest].refiner;
			std::vector<FailureReason> reasons;
			const auto addReasons = [&](std::uint32_t i)
			{
				for (const FailureReason& reason : fits[i].failures)
					reasons.push_back(swept ? FailureReason{reason.failure, lain::string::format("from the start at f = {:.1f}: {}",
																								 starts[i].focalLength, reason.detail)}
											: reason);
			};
			addReasons(furthest);
			for (std::uint32_t i = 0; i < fits.size(); ++i)
			{
				if (i != furthest)
					addReasons(i);
			}
			report = failed(std::move(report), start, reasons.front().failure, reasons.front().detail);
			report.failures.insert(report.failures.end(), reasons.begin() + 1, reasons.end());
			return report;
		}
		if (swept)
			targetless.swept = winner;
		const Fit& fit = fits[*winner];
		reportReconstruction(targetless, fit.diagnostics);
		record.refiner = fit.refiner;
		if (!hold)
			report.reproducibility.estimator = fit.solution.provenance;
		report.model = fit.model;
		const CameraModel& model = *report.model;
		const Narrowed& narrowed = fit.started->narrowed;
		const Start& begin = fit.started->start;
		const refinement::Problem& problem = fit.problem;
		const refinement::Solution& solution = fit.solution;
		const std::vector<std::uint32_t>& entryOf = fit.entryOf;

		// Residuals and outliers, per fitted observation. One left out for cheirality is an outlier an
		// adjustment could not even start from, and one the first adjustment's result did not explain is
		// named with its residual there. The fit RMS is over the observations the result explains, those
		// not named: a wrong match hundreds of pixels out would otherwise decide it alone, as a held-out
		// frame's outliers would decide its RMS.
		std::vector<NamedOutlier> named = fit.named;
		detail::CoverageCells fittedCells{};
		double sumPixels = 0;
		std::uint32_t explained = 0;
		for (std::size_t l = 0; l < entryOf.size(); ++l)
		{
			const std::uint32_t e = entryOf[l];
			const Usable& track = narrowed.evidence.tracks[narrowed.units[e]];
			for (std::size_t m = 0; m < track.seen.size(); ++m)
			{
				const Seen& s = track.seen[m];
				const std::uint32_t frame = narrowed.frameOf[s.view];
				if (!begin.kept[e][m])
				{
					named.push_back({narrowed.fromEntry[e], {track.track->identity, frame, std::numeric_limits<double>::infinity()}});
					continue;
				}
				detail::cover(fittedCells, s.pixel, frames.image);
				const math::Vec3d q = solution.cameraFromReference[s.view].apply(solution.landmarks[l]);
				const Projection<double> predicted = project(model, q.x, q.y, q.z);
				const math::Vec2d r = predicted.ok() ? math::Vec2d{predicted.u, predicted.v} - s.pixel : math::Vec2d{0.0};
				const double rmsWhitened = predicted.ok() ? std::sqrt(detail::whitenedSquared(r, s.covariance, problem.noise))
														  : std::numeric_limits<double>::infinity();
				if (rmsWhitened > settings.loss.scale)
				{
					named.push_back({narrowed.fromEntry[e], {track.track->identity, frame, rmsWhitened}});
					continue;
				}
				sumPixels += math::dot(r, r);
				++explained;
			}
		}
		std::sort(named.begin(), named.end(), [](const NamedOutlier& a, const NamedOutlier& b)
				  { return std::tie(a.unit, a.outlier.frame) < std::tie(b.unit, b.outlier.frame); });
		for (NamedOutlier& p : named)
			targetless.outliers.push_back(std::move(p.outlier));
		if (!hold)
		{
			for (std::uint32_t v = 0; v < narrowed.frameOf.size(); ++v)
				diagnostics.selected.push_back(narrowed.frameOf[v]);
			diagnostics.viewsSelected = std::uint32_t(diagnostics.selected.size());
			diagnostics.fitResiduals = explained;
			diagnostics.fitRmsPixels = explained > 0 ? std::sqrt(sumPixels / explained) : 0.0;
		}
		report.status = CalibrationStatus::Succeeded;

		// 7. The held-out frames, each posed against the refined landmarks.
		std::vector<HeldOutFrame> held(diagnostics.heldOut.size());
		{
			std::vector<std::optional<std::uint32_t>> heldIndex(n);
			for (std::uint32_t h = 0; h < diagnostics.heldOut.size(); ++h)
				heldIndex[diagnostics.heldOut[h]] = h;
			for (std::size_t l = 0; l < entryOf.size(); ++l)
			{
				const Ordered& t = tracks[fit.orderedOf[narrowed.fromEntry[entryOf[l]]]];
				for (const Observed& o : t.seen)
				{
					if (const std::optional<std::uint32_t> h = heldIndex[o.frame])
					{
						held[*h].points.push_back(solution.landmarks[l]);
						held[*h].pixels.push_back(o.pixel);
						held[*h].covariances.push_back(o.covariance);
					}
				}
			}
		}
		std::vector<FrameMeasure> measures(held.size());
		detail::forEach(held.size(), request.execution, [&](std::size_t h)
						{ measures[h] = measureFrame(model, held[h], request); });
		detail::CoverageCells heldCells{};
		std::uint32_t posed = 0;
		if (held.empty())
			report.heldOut = Unavailable{"no frames were held out"};
		else
		{
			HeldOutEvidence evidenceOut;
			evidenceOut.views = std::uint32_t(held.size());
			double angles = 0, pixels = 0, whitened = 0;
			std::uint32_t whitenedResiduals = 0;
			bool unstated = false;
			for (const FrameMeasure& m : measures)
			{
				if (!m.posed)
				{
					++evidenceOut.viewsWithoutPose;
					continue;
				}
				++posed;
				evidenceOut.residuals += m.residuals;
				evidenceOut.outliers += m.outliers;
				angles += m.sumAngles;
				pixels += m.sumPixels;
				whitened += m.sumWhitened;
				whitenedResiduals += m.whitened;
				unstated = unstated || m.unstated;
				evidenceOut.worstPixels = std::max(evidenceOut.worstPixels, m.worstPixels);
				for (const math::Vec2d& p : m.measured)
					detail::cover(heldCells, p, frames.image);
			}
			if (evidenceOut.residuals == 0)
				report.heldOut = Unavailable{"no held-out observation could be measured: no held-out frame was posed"};
			else
			{
				evidenceOut.rmsAngle = std::sqrt(angles / evidenceOut.residuals);
				evidenceOut.rmsPixels = std::sqrt(pixels / evidenceOut.residuals);
				// Per axis: a squared whitened residual has two degrees of freedom.
				if (!unstated && whitenedResiduals > 0)
					evidenceOut.rmsWhitened = std::sqrt(whitened / (2.0 * whitenedResiduals));
				report.heldOut = evidenceOut;
			}
		}
		diagnostics.coverage = detail::coveredFraction(hold ? heldCells : fittedCells);

		// 8. Stability: bundle adjustments of bootstrap resamples of the refined landmarks, drawn before
		// anything runs (detail::bootstrapDraws), each from the result under the refined model.
		if (hold)
			report.resampling = Unavailable{"the model was held, not estimated"};
		else if (request.resamples == 0)
			report.resampling = Unavailable{"resampling was not requested"};
		else if (entryOf.size() < 2)
			report.resampling = Unavailable{"too few landmarks to resample"};
		else
		{
			std::vector<std::uint32_t> landmarks(entryOf.size());
			std::iota(landmarks.begin(), landmarks.end(), 0u);
			const std::vector<std::vector<std::uint32_t>> draws = detail::bootstrapDraws(landmarks, request.resamples, request.seed);
			const Evidence fitted = fittedUnder(model, narrowed, begin, entryOf);
			std::vector<std::optional<CameraModelParameters>> estimates(draws.size());
			detail::forEach(draws.size(), request.execution,
							[&](std::size_t r)
							{ estimates[r] = resampled(fitted, solution, narrowed.placement.order, fit.freedom, settings, draws[r]); });
			report.resampling = detail::stability(estimates, !settings.freePrincipalPoint);
		}

		// 9. The verdict. A held model is judged on the frames that validated it.
		const Judgement judgement = judge(report.thresholds, hold ? posed : diagnostics.viewsSelected, diagnostics.coverage,
										  report.heldOut, report.resampling);
		report.verdict = judgement.verdict;
		report.fitnessNotes = judgement.notes;
		report.elapsed = core::Time::now() - start;
		return report;
	}

	// The track-set entry point's work, shared with the footage one after it has extracted: every check
	// that reads the track set, then the analysis.
	static Report calibrateTracks(Report report, const feature::TrackSet& set, const Settings& settings,
								  const Request& request, core::Time start)
	{
		TargetlessDiagnostics& targetless = std::get<TargetlessDiagnostics>(report.diagnostics.method);
		targetless.tracks = std::uint32_t(set.tracks.size());

		Frames frames;
		if (std::optional<Report> failure = checkTrackSet(report, set, frames, start))
			return std::move(*failure);
		targetless.frames = frames.refs;
		std::set<std::string> sources;
		for (const media::FrameRef& frame : frames.refs)
			sources.insert(frame.source.toString());
		report.reproducibility.sources.assign(sources.begin(), sources.end());
		report.reproducibility.frames = std::uint32_t(frames.refs.size());
		if (std::optional<Report> failure = checkRequest(report, request, frames.image, false, start))
			return std::move(*failure);
		return analyse(std::move(report), frames, orderTracks(set, frames), settings, request, start);
	}

	// A report with this method's diagnostics and record, the settings recorded as run.
	static Report startReport(const Settings& settings, const Request& request)
	{
		Report report;
		report.reproducibility.request = request;
		report.diagnostics.method = TargetlessDiagnostics{};
		TargetlessRecord record;
		record.settings = settings;
		record.settings.extraction.execution = request.execution;
		report.reproducibility.method = record;
		return report;
	}

	// --- calibrate ------------------------------------------------------------------

	Report calibrate(const feature::TrackSet& tracks, const Settings& settings, const Request& request)
	{
		const core::Time start = core::Time::now();
		return calibrateTracks(startReport(settings, request), tracks, settings, request, start);
	}

	Report calibrate(const capture::CameraFootage& camera, const Settings& settings, const Request& request)
	{
		const core::Time start = core::Time::now();
		Report report = startReport(settings, request);
		TargetlessRecord& record = std::get<TargetlessRecord>(report.reproducibility.method);
		report.reproducibility.frames = std::uint32_t(camera.footage.size());
		std::set<std::string> sources;
		for (std::size_t i = 0; i < camera.footage.size(); ++i)
			sources.insert(camera.footage.frame(i).source.toString());
		report.reproducibility.sources.assign(sources.begin(), sources.end());

		// Every check that needs no frame comes first, so a request that cannot succeed decodes nothing.
		if (camera.footage.size() == 0)
			return failed(std::move(report), start, Failure::NoFootage, "the frame sequence is empty");
		const math::Vec2i extent = camera.footage.spec().extent;
		const ImageGeometry image{std::uint32_t(std::max(extent.x, 0)), std::uint32_t(std::max(extent.y, 0))};
		if (std::optional<Report> failure = checkRequest(report, request, image, true, start))
			return std::move(*failure);

		const feature::SequenceExtractionResult extracted = feature::extractSequenceTracks(camera, record.settings.extraction);
		std::get<TargetlessDiagnostics>(report.diagnostics.method).extraction = extracted.report;
		record.extractor = extracted.report.extractor;
		record.matcher = extracted.report.matcher;
		if (!extracted.ok())
		{
			const Failure failure =
				extracted.status == feature::Status::InvalidInput ? Failure::InvalidDataset : Failure::ExtractionFailed;
			return failed(std::move(report), start, failure, extracted.detail);
		}
		return calibrateTracks(std::move(report), extracted.trackSet, settings, request, start);
	}
} // namespace lain::camera::calibration::targetless
