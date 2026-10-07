#include "lain/camera/registration/targetless.h"

#include "common.h"
#include "lain/camera/feature/geometry.h"
#include "lain/camera/feature/matching.h"
#include "lain/camera/feature/triangulation.h"
#include "lain/camera/projection.h"
#include "lain/camera/registration/observationgraph.h"
#include "lain/camera/registration/refiner.h"
#include "lain/camera/registration/validation.h"

#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <variant>

namespace lain::camera::registration::targetless
{
	using detail::angleBetween;
	using detail::failed;
	using detail::median;
	using detail::rotationBetween;

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Fewer usable tracks than this and none are held out: a track is one point, so a rig with a
	// handful has too little to register from and too little to validate with.
	static constexpr std::size_t kMinimumToHoldOut = 20;
	// The geometry facades' own minima (feature/geometry.h): five pairs pose two cameras, four
	// point-rays one. A seed pair needs this many shared tracks, and a camera this many landmarks to be
	// placed, or to be kept once the landmarks behind it are left out.
	static constexpr std::uint32_t kSeedShared = 5;
	static constexpr std::uint32_t kPlaceLandmarks = 4;
	// How many camera pairs are posed as seed candidates: of those whose placement reaches every
	// camera, the ones sharing the most tracks.
	static constexpr std::size_t kSeedCandidates = 16;
	// The median triangulation angle a seed should clear, radians (2 degrees): a landmark the seed
	// triangulates is as uncertain in depth as its rays' noise over its parallax. Provisional until
	// sub-slice 9 measures real rigs; it ranks pairs and never refuses a rig.
	static constexpr double kSeedAngle = 0.03490658503988659;

	// The geometry every pose of initialisation is measured by: the facades' default inlier angle, which
	// is also where a triangulation becomes a point at infinity, and the request's seed. The same in
	// both entry points, so registering footage is extracting and then registering its tracks, exactly.
	static feature::GeometryRequest geometryRequest(const Request& request)
	{
		feature::GeometryRequest geometry;
		geometry.seed = request.seed;
		return geometry;
	}

	static double minimumParallax()
	{
		return feature::GeometryRequest{}.angle;
	}

	// One usable observation of a track: a canonical camera, where it saw the landmark, and the ray.
	struct Seen
	{
		std::uint32_t camera = 0;
		math::Vec2d pixel{0.0};
		std::optional<std::array<double, 3>> covariance;
		math::Vec3d ray{0.0}; // unit, in the camera's frame
	};

	// A track with two or more usable observations, ascending by camera.
	struct Usable
	{
		const feature::Track* track = nullptr;
		std::vector<Seen> seen;
	};

	// The usable evidence: tracks in identity order, over the canonical cameras.
	struct Evidence
	{
		std::vector<RigCamera> cameras; // canonical: ascending by identity
		std::vector<Usable> tracks;
	};

	// What one registration fits: indices into Evidence::tracks. A bootstrap resample may name a track
	// twice; each entry is a landmark of its own, as each entry of a board resample is a body of its own.
	using Units = std::vector<std::uint32_t>;

	// The cameras of each unit, for the observation graph and placeability.
	static std::vector<std::vector<std::uint32_t>> camerasOf(const Evidence& evidence, const Units& units)
	{
		std::vector<std::vector<std::uint32_t>> out;
		out.reserve(units.size());
		for (const std::uint32_t u : units)
		{
			std::vector<std::uint32_t> cameras;
			for (const Seen& s : evidence.tracks[u].seen)
				cameras.push_back(s.camera);
			out.push_back(std::move(cameras));
		}
		return out;
	}

	// Per camera, the units that saw it.
	static std::vector<std::vector<std::uint32_t>> unitsOf(std::size_t cameras,
														   const std::vector<std::vector<std::uint32_t>>& units)
	{
		std::vector<std::vector<std::uint32_t>> out(cameras);
		for (std::uint32_t u = 0; u < units.size(); ++u)
		{
			for (const std::uint32_t c : units[u])
				out[c].push_back(u);
		}
		return out;
	}

	// --- placeability --------------------------------------------------------------

	// Which cameras a seed pair can place (CONTEXT.md, "Placeable camera"): from the pair, repeatedly the
	// unplaced camera in the most units having two or more placed cameras, needing kPlaceLandmarks of
	// them, ties to the lower index. Placing a camera only adds to every other camera's count, so the
	// set reached is the least fixed point whatever order cameras are added in; it depends on the seed.
	struct Closure
	{
		std::uint32_t a = 0, b = 0;
		std::vector<bool> placed;
		std::vector<std::uint32_t> rank;	// per placed camera: its place in the order
		std::vector<std::uint32_t> support; // per placed camera but the seeds: the units it was placed with
		std::uint32_t shared = 0;			// the units the seed pair shares

		bool covers(const std::vector<std::uint32_t>& cameras) const
		{
			return std::all_of(cameras.begin(), cameras.end(), [this](std::uint32_t c)
							   { return bool(placed[c]); });
		}
	};

	static Closure closure(const std::vector<std::vector<std::uint32_t>>& units,
						   const std::vector<std::vector<std::uint32_t>>& ofCamera, std::uint32_t a, std::uint32_t b)
	{
		const std::size_t cameras = ofCamera.size();
		Closure out;
		out.a = a;
		out.b = b;
		out.placed.assign(cameras, false);
		out.rank.assign(cameras, std::numeric_limits<std::uint32_t>::max());
		out.support.assign(cameras, 0);
		std::vector<std::uint32_t> inPlaced(units.size(), 0), count(cameras, 0);
		std::uint32_t next = 0;
		const auto place = [&](std::uint32_t c)
		{
			out.placed[c] = true;
			out.rank[c] = next++;
			for (const std::uint32_t u : ofCamera[c])
			{
				if (++inPlaced[u] != 2)
					continue;
				for (const std::uint32_t other : units[u])
				{
					if (!out.placed[other])
						++count[other];
				}
			}
		};
		for (const std::uint32_t u : ofCamera[a])
		{
			if (std::find(units[u].begin(), units[u].end(), b) != units[u].end())
				++out.shared;
		}
		place(a);
		place(b);
		for (;;)
		{
			std::optional<std::uint32_t> best;
			for (std::uint32_t c = 0; c < cameras; ++c)
			{
				if (!out.placed[c] && count[c] >= kPlaceLandmarks && (!best || count[c] > count[*best]))
					best = c;
			}
			if (!best)
				break;
			out.support[*best] = count[*best];
			place(*best);
		}
		return out;
	}

	// The seed candidates over `units` for placing `required`: each pair of required cameras sharing at
	// least kSeedShared units, by how many they share, ties to (a, b). `covering` holds the closures that
	// reach every required camera, at most `limit`, in that order; `top` is the first candidate's,
	// covering or not. A pair inside a closure that fell short cannot reach further (its own closure
	// lies inside that one), so it is skipped unexamined.
	struct Candidates
	{
		std::vector<Closure> covering;
		std::optional<Closure> top;
	};

	static Candidates candidates(const ObservationGraph& graph, const std::vector<std::vector<std::uint32_t>>& units,
								 const std::vector<std::vector<std::uint32_t>>& ofCamera,
								 const std::vector<std::uint32_t>& required, std::size_t limit)
	{
		std::vector<bool> isRequired(ofCamera.size(), false);
		for (const std::uint32_t c : required)
			isRequired[c] = true;
		std::vector<const GraphEdge*> order;
		for (const GraphEdge& e : graph.edges)
		{
			if (e.shared >= kSeedShared && isRequired[e.a] && isRequired[e.b])
				order.push_back(&e);
		}
		std::stable_sort(order.begin(), order.end(), [](const GraphEdge* x, const GraphEdge* y)
						 { return x->shared > y->shared; });

		Candidates out;
		std::vector<std::vector<bool>> shortOf;
		for (const GraphEdge* e : order)
		{
			const bool inside = std::any_of(shortOf.begin(), shortOf.end(), [e](const std::vector<bool>& placed)
											{ return placed[e->a] && placed[e->b]; });
			if (inside)
				continue;
			Closure c = closure(units, ofCamera, e->a, e->b);
			if (!out.top)
				out.top = c;
			if (c.covers(required))
			{
				out.covering.push_back(std::move(c));
				if (out.covering.size() == limit)
					break;
			}
			else
				shortOf.push_back(c.placed);
		}
		return out;
	}

	// --- initialisation ------------------------------------------------------------

	struct Seed
	{
		std::uint32_t a = 0, b = 0;
		math::RigidTransformd bFromA; // a unit baseline
		std::uint32_t inliers = 0;
		double angle = 0; // the median triangulation angle of its inliers that triangulate, radians
		bool belowFloor = false;
	};

	// The seed: each candidate's relative pose from the tracks its two cameras share, then the one with
	// the most inliers whose median angle clears kSeedAngle, or the widest when none does. Unset when
	// no candidate could be posed.
	static std::optional<Seed> chooseSeed(const Evidence& evidence, const Units& units, const std::vector<Closure>& covering,
										  const Request& request, Provenance& geometry)
	{
		std::optional<Seed> best, widest;
		for (const Closure& candidate : covering)
		{
			std::vector<feature::RayPair> pairs;
			for (const std::uint32_t u : units)
			{
				const Seen* inA = nullptr;
				const Seen* inB = nullptr;
				for (const Seen& s : evidence.tracks[u].seen)
				{
					if (s.camera == candidate.a)
						inA = &s;
					else if (s.camera == candidate.b)
						inB = &s;
				}
				if (inA && inB)
					pairs.push_back({inA->ray, inB->ray});
			}
			const feature::GeometryResult result = feature::relativePose(pairs, geometryRequest(request));
			if (geometry.backend.empty())
				geometry = result.provenance;
			if (!result.ok())
				continue;
			Seed seed;
			seed.a = candidate.a;
			seed.b = candidate.b;
			seed.bFromA = *result.pose;
			seed.inliers = std::uint32_t(result.inliers.size());
			const math::RigidTransformd aFromB = seed.bFromA.inverse();
			std::vector<double> angles;
			for (const std::uint32_t i : result.inliers)
			{
				const feature::Triangulation t = feature::triangulate(
					{{math::Vec3d{0.0}, pairs[i].a}, {aFromB.translation(), aFromB.rotate(pairs[i].b)}}, minimumParallax());
				if (t.ok())
					angles.push_back(t.angle);
			}
			seed.angle = angles.empty() ? 0.0 : median(std::move(angles));
			if (seed.angle >= kSeedAngle && (!best || seed.inliers > best->inliers))
				best = seed;
			if (!widest || seed.angle > widest->angle)
				widest = seed;
		}
		if (best)
			return best;
		if (widest)
			widest->belowFloor = true;
		return widest;
	}

	// Every camera the seed can place, in the seed's first camera's frame (the world) at a unit
	// baseline: landmarks triangulated from the placed cameras, then the unplaced camera seeing the most
	// of them placed by absolute pose, until none more can be.
	struct Placement
	{
		std::vector<std::optional<math::RigidTransformd>> cameraFromWorld;
		std::vector<std::optional<math::Vec3d>> landmarks; // per unit entry, in the world
		std::vector<std::uint32_t> order;				   // the cameras, as placed
	};

	static Placement place(const Evidence& evidence, const Units& units, const Seed& seed, const Request& request)
	{
		const std::size_t n = evidence.cameras.size();
		Placement out;
		out.cameraFromWorld.assign(n, std::nullopt);
		out.landmarks.assign(units.size(), std::nullopt);
		std::vector<std::vector<std::uint32_t>> entriesOf(n);
		for (std::uint32_t e = 0; e < units.size(); ++e)
		{
			for (const Seen& s : evidence.tracks[units[e]].seen)
				entriesOf[s.camera].push_back(e);
		}
		std::vector<std::uint32_t> landmarksSeen(n, 0); // per unplaced camera

		const auto triangulateEntry = [&](std::uint32_t e)
		{
			if (out.landmarks[e])
				return;
			std::vector<feature::Ray> rays;
			for (const Seen& s : evidence.tracks[units[e]].seen)
			{
				if (!out.cameraFromWorld[s.camera])
					continue;
				const math::RigidTransformd worldFromCamera = out.cameraFromWorld[s.camera]->inverse();
				rays.push_back({worldFromCamera.translation(), worldFromCamera.rotate(s.ray)});
			}
			if (rays.size() < 2)
				return;
			const feature::Triangulation t = feature::triangulate(rays, minimumParallax());
			if (!t.ok())
				return;
			out.landmarks[e] = t.point;
			for (const Seen& s : evidence.tracks[units[e]].seen)
			{
				if (!out.cameraFromWorld[s.camera])
					++landmarksSeen[s.camera];
			}
		};
		const auto setCamera = [&](std::uint32_t c, const math::RigidTransformd& cameraFromWorld)
		{
			out.cameraFromWorld[c] = cameraFromWorld;
			out.order.push_back(c);
			for (const std::uint32_t e : entriesOf[c])
				triangulateEntry(e);
		};
		setCamera(seed.a, math::RigidTransformd{});
		setCamera(seed.b, seed.bFromA);

		for (;;)
		{
			std::vector<std::uint32_t> waiting;
			for (std::uint32_t c = 0; c < n; ++c)
			{
				if (!out.cameraFromWorld[c] && landmarksSeen[c] >= kPlaceLandmarks)
					waiting.push_back(c);
			}
			std::stable_sort(waiting.begin(), waiting.end(), [&](std::uint32_t x, std::uint32_t y)
							 { return landmarksSeen[x] > landmarksSeen[y]; });
			bool placedOne = false;
			for (const std::uint32_t c : waiting)
			{
				std::vector<feature::PointRay> pointRays;
				for (const std::uint32_t e : entriesOf[c])
				{
					if (!out.landmarks[e])
						continue;
					for (const Seen& s : evidence.tracks[units[e]].seen)
					{
						if (s.camera == c)
							pointRays.push_back({*out.landmarks[e], s.ray});
					}
				}
				const feature::GeometryResult result = feature::absolutePose(pointRays, geometryRequest(request));
				if (!result.ok())
					continue;
				setCamera(c, *result.pose);
				placedOne = true;
				break;
			}
			if (!placedOne)
				break;
		}
		return out;
	}

	// A placed rig rebased on the reference, with every observation its camera cannot project left out:
	// a landmark behind a camera at the start fails the whole refinement (ADR-0017). What refinement
	// starts from.
	struct Start
	{
		std::vector<math::RigidTransformd> cameraFromReference;
		std::vector<std::optional<math::Vec3d>> landmarks; // per unit entry, in the reference frame; unset: not refined
		std::vector<std::vector<bool>> kept;			   // per unit entry, per observation: refined
		std::uint32_t behind = 0;						   // observations left out
		std::vector<std::uint32_t> starved;				   // cameras left seeing fewer than kPlaceLandmarks landmarks
	};

	static Start startFrom(const Evidence& evidence, const Units& units, const Placement& placement, std::uint32_t reference)
	{
		const std::size_t n = evidence.cameras.size();
		Start out;
		const math::RigidTransformd& referenceFromWorld = *placement.cameraFromWorld[reference];
		const math::RigidTransformd worldFromReference = referenceFromWorld.inverse();
		for (std::size_t c = 0; c < n; ++c)
			out.cameraFromReference.push_back(*placement.cameraFromWorld[c] * worldFromReference);
		out.cameraFromReference[reference] = math::RigidTransformd{};
		out.landmarks.assign(units.size(), std::nullopt);
		out.kept.resize(units.size());
		std::vector<std::uint32_t> seen(n, 0);
		for (std::size_t e = 0; e < units.size(); ++e)
		{
			const std::vector<Seen>& members = evidence.tracks[units[e]].seen;
			out.kept[e].assign(members.size(), false);
			if (!placement.landmarks[e])
				continue;
			const math::Vec3d point = referenceFromWorld.apply(*placement.landmarks[e]);
			std::uint32_t kept = 0;
			for (std::size_t m = 0; m < members.size(); ++m)
			{
				const math::Vec3d q = out.cameraFromReference[members[m].camera].apply(point);
				if (project(evidence.cameras[members[m].camera].model, q.x, q.y, q.z).ok())
				{
					out.kept[e][m] = true;
					++kept;
				}
				else
					++out.behind;
			}
			if (kept < 2)
			{
				out.kept[e].assign(members.size(), false);
				continue;
			}
			out.landmarks[e] = point;
			for (std::size_t m = 0; m < members.size(); ++m)
			{
				if (out.kept[e][m])
					++seen[members[m].camera];
			}
		}
		for (std::uint32_t c = 0; c < n; ++c)
		{
			if (seen[c] < kPlaceLandmarks)
				out.starved.push_back(c);
		}
		return out;
	}

	// The scale that puts the median depth of the landmarks the reference sees at 1 (CONTEXT.md, "Scale
	// normalisation"), and how many it was the median of.
	static std::pair<double, std::uint32_t> normalisation(const Evidence& evidence, const Units& units,
														  const std::vector<std::optional<math::Vec3d>>& landmarks,
														  const std::vector<std::vector<bool>>& kept, std::uint32_t reference)
	{
		std::vector<double> depths;
		for (std::size_t e = 0; e < units.size(); ++e)
		{
			if (!landmarks[e])
				continue;
			const std::vector<Seen>& members = evidence.tracks[units[e]].seen;
			for (std::size_t m = 0; m < members.size(); ++m)
			{
				if (members[m].camera == reference && kept[e][m])
					depths.push_back(landmarks[e]->z);
			}
		}
		const double depth = median(depths);
		return {std::isfinite(depth) && depth > 0 ? 1.0 / depth : 1.0, std::uint32_t(depths.size())};
	}

	static void scaleBy(double s, std::vector<math::RigidTransformd>& cameraFromReference,
						std::vector<std::optional<math::Vec3d>>& landmarks)
	{
		for (math::RigidTransformd& t : cameraFromReference)
			t = math::RigidTransformd{t.rotation(), t.translation() * s};
		for (std::optional<math::Vec3d>& p : landmarks)
		{
			if (p)
				*p *= s;
		}
	}

	// The refinement problem from `start`: every refined landmark and its kept observations. `entryOf`
	// receives each problem landmark's unit entry.
	static Problem problemOf(const Evidence& evidence, const Units& units, const Start& start, std::uint32_t reference,
							 const Request& request, std::vector<std::uint32_t>& entryOf)
	{
		Problem problem;
		for (const RigCamera& c : evidence.cameras)
			problem.models.push_back(c.model);
		problem.cameraFromReference = start.cameraFromReference;
		problem.reference = reference;
		problem.noise = request.noise;
		problem.loss = request.loss;
		problem.maximumIterations = request.maximumIterations;
		for (std::uint32_t e = 0; e < units.size(); ++e)
		{
			if (!start.landmarks[e])
				continue;
			const std::uint32_t landmark = std::uint32_t(problem.landmarks.size());
			problem.landmarks.push_back(*start.landmarks[e]);
			entryOf.push_back(e);
			const std::vector<Seen>& members = evidence.tracks[units[e]].seen;
			for (std::size_t m = 0; m < members.size(); ++m)
			{
				if (start.kept[e][m])
					problem.landmarkObservations.push_back(
						{members[m].camera, landmark, members[m].pixel, members[m].covariance});
			}
		}
		return problem;
	}

	// Initialise and refine over `units`: the whole registration short of its evidence, which is what a
	// bootstrap resample repeats. Unset when the units cannot place every camera or the refinement gives
	// no usable solution. The result is rebased on `reference`, its scale whatever the refinement left.
	static std::optional<std::vector<math::RigidTransformd>> registerUnits(const Evidence& evidence, const Units& units,
																		   std::uint32_t reference, const Request& request)
	{
		const std::size_t n = evidence.cameras.size();
		const std::vector<std::vector<std::uint32_t>> cameras = camerasOf(evidence, units);
		const ObservationGraph graph = observationGraph(std::uint32_t(n), cameras);
		if (graph.components.size() != 1)
			return std::nullopt;
		std::vector<std::uint32_t> all(n);
		std::iota(all.begin(), all.end(), 0u);
		const Candidates found = candidates(graph, cameras, unitsOf(n, cameras), all, kSeedCandidates);
		Provenance unused;
		const std::optional<Seed> seed = chooseSeed(evidence, units, found.covering, request, unused);
		if (!seed)
			return std::nullopt;
		const Placement placement = place(evidence, units, *seed, request);
		if (placement.order.size() != n)
			return std::nullopt;
		Start start = startFrom(evidence, units, placement, reference);
		if (!start.starved.empty())
			return std::nullopt;
		scaleBy(normalisation(evidence, units, start.landmarks, start.kept, reference).first, start.cameraFromReference,
				start.landmarks);
		std::vector<std::uint32_t> entryOf;
		const Solution solution = refine(problemOf(evidence, units, start, reference, request, entryOf));
		if (!solution.usable() || solution.cameraFromReference.size() != n)
			return std::nullopt;
		std::vector<math::RigidTransformd> out = solution.cameraFromReference;
		out[reference] = math::RigidTransformd{};
		return out;
	}

	// What the cameras of `component` place, relative to their seed's first camera: a diagnostic of a rig
	// that cannot register whole. Unset when no pair of them can seed.
	static std::optional<ComponentEstimate> estimate(const Evidence& evidence, const Units& units,
													 const ObservationGraph& graph,
													 const std::vector<std::vector<std::uint32_t>>& cameras,
													 const std::vector<std::vector<std::uint32_t>>& ofCamera,
													 const std::vector<std::uint32_t>& component, const Request& request)
	{
		Candidates found = candidates(graph, cameras, ofCamera, component, kSeedCandidates);
		if (found.covering.empty() && found.top)
			found.covering.push_back(*found.top);
		Provenance unused;
		const std::optional<Seed> seed = chooseSeed(evidence, units, found.covering, request, unused);
		if (!seed)
			return std::nullopt;
		const Placement placement = place(evidence, units, *seed, request);
		ComponentEstimate out;
		out.reference = evidence.cameras[seed->a].camera;
		for (const std::uint32_t c : placement.order)
			out.cameras.push_back({evidence.cameras[c].camera, placement.cameraFromWorld[c]->inverse()});
		std::sort(out.cameras.begin(), out.cameras.end(), [](const RegisteredCamera& x, const RegisteredCamera& y)
				  { return x.camera < y.camera; });
		return out;
	}

	static std::string listOf(const Evidence& evidence, const std::vector<std::uint32_t>& cameras)
	{
		std::string out;
		for (std::size_t i = 0; i < cameras.size(); ++i)
			out += (i ? ", " : "") + evidence.cameras[cameras[i]].camera.value;
		return out;
	}

	// --- the checks ------------------------------------------------------------------

	static bool positiveDefinite(const std::array<double, 3>& c)
	{
		return std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]) && c[0] > 0 && c[2] > 0 &&
			   c[0] * c[2] - c[1] * c[1] > 0;
	}

	// The checks that read the track set: each view a camera of the rig, none twice; each observation of
	// a view the set has, one per view, its covariance positive definite; each track's identity once. On
	// failure the report failed. `cameraOfView` receives each view's canonical camera.
	static std::optional<Report> checkTrackSet(Report& report, const detail::CanonicalCameras& canonical,
											   const feature::TrackSet& set,
											   std::vector<std::uint32_t>& cameraOfView, core::Time start)
	{
		std::vector<bool> hasView(canonical.cameras.size(), false);
		for (std::size_t v = 0; v < set.views.size(); ++v)
		{
			const std::optional<std::uint32_t> camera = canonical.indexOf(set.views[v].camera);
			if (!camera)
				return failed(report, start, Failure::InvalidDataset,
							  lain::string::format("view {} names camera \"{}\", which the dataset does not have", v,
												   set.views[v].camera.value));
			if (hasView[*camera])
				return failed(report, start, Failure::InvalidDataset,
							  "camera \"" + set.views[v].camera.value + "\" has two views");
			hasView[*camera] = true;
			cameraOfView.push_back(*camera);
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
								  lain::string::format("track {} names view {}, of {}", track.identity.substr(0, 12), o.view,
													   set.views.size()));
				if (!views.insert(o.view).second)
					return failed(report, start, Failure::InvalidDataset,
								  lain::string::format("track {} holds two observations of view {}",
													   track.identity.substr(0, 12), o.view));
				if (o.covariance && !positiveDefinite(*o.covariance))
					return failed(report, start, Failure::InvalidDataset,
								  "track " + track.identity.substr(0, 12) + " has a covariance that is not positive definite");
			}
		}
		std::sort(identities.begin(), identities.end(), [](const std::string* x, const std::string* y)
				  { return *x < *y; });
		for (std::size_t i = 1; i < identities.size(); ++i)
		{
			if (*identities[i] == *identities[i - 1])
				return failed(report, start, Failure::InvalidDataset,
							  "track " + identities[i]->substr(0, 12) + " is given twice");
		}
		return std::nullopt;
	}

	// The checks that need no evidence, beyond the track set: the profile, the cameras and the
	// reference. On failure the report failed.
	static std::optional<Report> prepareCameras(Report& report, detail::CanonicalCameras& canonical,
												const std::vector<RigCamera>& cameras, const Request& request,
												core::Time start)
	{
		report.reproducibility.request = request;
		if (std::optional<Report> failure =
				detail::resolveProfile(report, request, EvidenceUnit::Track, "registration-targetless/1", start))
			return failure;
		return detail::canonicalCameras(report, canonical, cameras, request, start);
	}

	static std::optional<Report> checkBackends(Report& report, core::Time start)
	{
		if (!feature::canSolveGeometry())
			return failed(report, start, Failure::NoGeometrySolver,
						  "this build has no geometry solver (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!canRefine())
			return failed(report, start, Failure::NoRefiner,
						  "this build has no registration refiner (configure with -DLAIN_CAMERA_CERES=ON)");
		return std::nullopt;
	}

	// --- the registration --------------------------------------------------------------

	// Everything after the checks. `report` arrives prepared.
	static Report analyse(Report report, const Evidence& evidence, const feature::TrackSet& set,
						  const std::vector<Applicability>& applicability, std::optional<std::uint32_t> requested,
						  const Request& request, core::Time start)
	{
		const std::size_t n = evidence.cameras.size();
		Diagnostics& diagnostics = report.diagnostics;
		TargetlessDiagnostics& targetless = std::get<TargetlessDiagnostics>(diagnostics.method);
		TargetlessRecord& record = std::get<TargetlessRecord>(report.reproducibility.method);
		targetless.tracksUsable = std::uint32_t(evidence.tracks.size());
		if (evidence.tracks.empty())
			return failed(std::move(report), start, Failure::TooFewCameras, "no track is seen by two or more cameras");
		std::vector<std::uint32_t> all(n);
		std::iota(all.begin(), all.end(), 0u);

		// 3. Every k-th usable track held out, unless that would leave a camera unplaceable from the
		// first seed that places them all: the anchor. What it placed each camera with is its support,
		// and a track is held out only while every camera it supports keeps enough. The anchor's order
		// then stays a valid placement, so a seed reaching every camera survives the hold-out.
		Units usable(evidence.tracks.size());
		std::iota(usable.begin(), usable.end(), 0u);
		const std::vector<std::vector<std::uint32_t>> usableCameras = camerasOf(evidence, usable);
		const Candidates anchors = candidates(observationGraph(std::uint32_t(n), usableCameras), usableCameras,
											  unitsOf(n, usableCameras), all, 1);
		std::function<bool(std::size_t)> mayHold = [](std::size_t)
		{ return true; };
		std::optional<Closure> anchor;
		if (!anchors.covering.empty())
			anchor = anchors.covering.front();
		if (anchor)
		{
			mayHold = [&](std::size_t u)
			{
				const std::vector<std::uint32_t>& seen = usableCameras[u];
				const bool both = std::count(seen.begin(), seen.end(), anchor->a) + std::count(seen.begin(), seen.end(), anchor->b) == 2;
				if (both && anchor->shared <= kSeedShared)
					return false;
				std::vector<std::uint32_t> supported;
				for (const std::uint32_t c : seen)
				{
					if (c == anchor->a || c == anchor->b)
						continue;
					const auto before = std::count_if(seen.begin(), seen.end(), [&](std::uint32_t o)
													  { return anchor->rank[o] < anchor->rank[c]; });
					if (before < 2)
						continue;
					if (anchor->support[c] <= kPlaceLandmarks)
						return false;
					supported.push_back(c);
				}
				if (both)
					--anchor->shared;
				for (const std::uint32_t c : supported)
					--anchor->support[c];
				return true;
			};
		}
		const std::vector<bool> held = detail::holdOut(usable.size(), request.heldOutFraction, kMinimumToHoldOut, mayHold);
		Units fitting;
		std::vector<std::string> validating;
		for (std::uint32_t u = 0; u < usable.size(); ++u)
		{
			if (held[u])
			{
				validating.push_back(evidence.tracks[u].track->identity);
				diagnostics.heldOutUnits.push_back(evidence.tracks[u].track->identity);
			}
			else
				fitting.push_back(u);
		}

		// 4. The camera observation graph over the fitting tracks.
		const std::vector<std::vector<std::uint32_t>> fittingCameras = camerasOf(evidence, fitting);
		const std::vector<std::vector<std::uint32_t>> fittingOf = unitsOf(n, fittingCameras);
		const ObservationGraph graph = observationGraph(std::uint32_t(n), fittingCameras);
		const auto edgeReport = [&](const GraphEdge& e)
		{ return GraphEdgeReport{evidence.cameras[e.a].camera, evidence.cameras[e.b].camera, e.shared, e.bridge}; };
		for (const GraphEdge& e : graph.edges)
			diagnostics.edges.push_back(edgeReport(e));
		for (const GraphEdge& e : weakBridges(graph, report.thresholds.ready.minimumBridgeShared))
			diagnostics.weakBridges.push_back(edgeReport(e));
		for (const std::vector<std::uint32_t>& component : graph.components)
		{
			std::vector<capture::CameraIdentity> ids;
			for (const std::uint32_t c : component)
				ids.push_back(evidence.cameras[c].camera);
			diagnostics.components.push_back(std::move(ids));
		}
		for (std::size_t c = 0; c < n; ++c)
		{
			CameraEvidence camera;
			camera.camera = evidence.cameras[c].camera;
			camera.applicability = applicability[c];
			camera.shared = graph.sharedPerCamera[c];
			diagnostics.cameras.push_back(std::move(camera));
		}
		if (graph.components.size() > 1)
		{
			for (const std::vector<std::uint32_t>& component : graph.components)
			{
				if (std::optional<ComponentEstimate> e =
						estimate(evidence, fitting, graph, fittingCameras, fittingOf, component, request))
					diagnostics.componentEstimates.push_back(std::move(*e));
			}
			std::string detail = lain::string::format("the cameras fall into {} groups that share no track:",
													  graph.components.size());
			for (const std::vector<std::uint32_t>& component : graph.components)
				detail += " {" + listOf(evidence, component) + "}";
			return failed(std::move(report), start, Failure::Disconnected, detail);
		}

		// The reference: requested, or the camera in the most shared tracks, ties to the lower identity.
		report.referenceRequested = requested.has_value();
		const std::uint32_t reference = detail::chooseReference(graph.sharedPerCamera, requested);
		report.reference = evidence.cameras[reference].camera;

		// 5. Placeability, then the seed.
		const Candidates found = candidates(graph, fittingCameras, fittingOf, all, kSeedCandidates);
		if (found.covering.empty())
		{
			if (found.top)
			{
				for (std::uint32_t c = 0; c < n; ++c)
				{
					if (!found.top->placed[c])
						targetless.unplaceable.push_back(evidence.cameras[c].camera);
				}
				if (std::optional<ComponentEstimate> e =
						estimate(evidence, fitting, graph, fittingCameras, fittingOf, all, request))
					diagnostics.componentEstimates.push_back(std::move(*e));
			}
			else
				targetless.unplaceable = [&]
				{
					std::vector<capture::CameraIdentity> ids;
					for (const RigCamera& c : evidence.cameras)
						ids.push_back(c.camera);
					return ids;
				}();
			std::string names;
			for (std::size_t i = 0; i < targetless.unplaceable.size(); ++i)
				names += (i ? ", " : "") + targetless.unplaceable[i].value;
			return failed(std::move(report), start, Failure::Disconnected,
						  found.top ? "cameras " + names +
										  " share no triangulable landmark with the cameras placed before them"
									: "no camera pair shares enough tracks to seed the rig");
		}
		const std::optional<Seed> seed = chooseSeed(evidence, fitting, found.covering, request, record.geometry);
		if (!seed)
			return failed(std::move(report), start, Failure::InitialisationFailed,
						  "no camera pair's relative pose could be estimated from the tracks it shares");
		targetless.seed = {evidence.cameras[seed->a].camera, evidence.cameras[seed->b].camera};
		targetless.seedAngle = seed->angle;
		targetless.seedBelowFloor = seed->belowFloor;

		// 6. Incremental initialisation.
		const Placement placement = place(evidence, fitting, *seed, request);
		for (const std::uint32_t c : placement.order)
			targetless.placement.push_back(evidence.cameras[c].camera);
		if (placement.order.size() != n)
		{
			std::vector<std::uint32_t> unplaced;
			for (std::uint32_t c = 0; c < n; ++c)
			{
				if (!placement.cameraFromWorld[c])
					unplaced.push_back(c);
			}
			return failed(std::move(report), start, Failure::InitialisationFailed,
						  "cameras " + listOf(evidence, unplaced) + " could not be placed by absolute pose");
		}

		// 7. Cheirality.
		Start begin = startFrom(evidence, fitting, placement, reference);
		targetless.behind = begin.behind;
		if (!begin.starved.empty())
			return failed(std::move(report), start, Failure::InitialisationFailed,
						  lain::string::format("cameras {} see fewer than {} landmarks in front of them",
											   listOf(evidence, begin.starved), kPlaceLandmarks));
		scaleBy(normalisation(evidence, fitting, begin.landmarks, begin.kept, reference).first, begin.cameraFromReference,
				begin.landmarks);

		// 8. The global refinement.
		std::vector<std::uint32_t> entryOf;
		const Problem problem = problemOf(evidence, fitting, begin, reference, request, entryOf);
		const Solution solution = refine(problem);
		report.reproducibility.refiner = solution.provenance;
		RefinementSummary summary;
		summary.status = solution.status;
		summary.detail = solution.detail;
		summary.iterations = solution.iterations;
		summary.initialCost = solution.initialCost;
		summary.finalCost = solution.finalCost;
		summary.residuals = std::uint32_t(problem.observations.size() + problem.landmarkObservations.size());
		summary.bodies = std::uint32_t(problem.referenceFromBody.size());
		summary.landmarks = std::uint32_t(problem.landmarks.size());
		summary.elapsed = solution.elapsed;
		summary.noise = request.noise;
		summary.loss = request.loss;
		diagnostics.refinement = summary;
		if (solution.status == RefinementStatus::ResourceExhausted)
			return failed(std::move(report), start, Failure::ResourceExhausted, solution.detail);
		if (!solution.usable())
			return failed(std::move(report), start, Failure::RefinementFailed,
						  solution.detail.empty() ? "the refinement produced no usable solution" : solution.detail);
		if (solution.cameraFromReference.size() != n || solution.landmarks.size() != problem.landmarks.size())
			return failed(std::move(report), start, Failure::RefinementFailed,
						  "the refiner returned a solution of the wrong shape");
		std::vector<math::RigidTransformd> cameraFromReference = solution.cameraFromReference;
		cameraFromReference[reference] = math::RigidTransformd{};
		std::vector<std::optional<math::Vec3d>> landmarks(fitting.size());
		for (std::size_t l = 0; l < entryOf.size(); ++l)
			landmarks[entryOf[l]] = solution.landmarks[l];
		const auto [scale, depthCount] = normalisation(evidence, fitting, landmarks, begin.kept, reference);
		scaleBy(scale, cameraFromReference, landmarks);
		targetless.landmarks = std::uint32_t(entryOf.size());
		targetless.untriangulated = std::uint32_t(fitting.size() - entryOf.size());

		// Residuals, outliers and depth, per fitted observation. One left out for cheirality is an
		// outlier the refinement could not even start from.
		std::vector<double> depths;
		std::vector<double> sumPixels(n, 0), sumAngles(n, 0);
		std::uint32_t outliers = 0, fitted = 0;
		for (std::size_t e = 0; e < fitting.size(); ++e)
		{
			const Usable& track = evidence.tracks[fitting[e]];
			for (std::size_t m = 0; m < track.seen.size(); ++m)
			{
				const Seen& s = track.seen[m];
				CameraEvidence& camera = diagnostics.cameras[s.camera];
				double rmsWhitened = std::numeric_limits<double>::infinity();
				if (landmarks[e])
				{
					if (!begin.kept[e][m])
					{
						++fitted;
						++outliers;
						++camera.outliers;
						diagnostics.outliers.push_back({track.track->identity, camera.camera, rmsWhitened});
						continue;
					}
					const math::Vec3d q = cameraFromReference[s.camera].apply(*landmarks[e]);
					const Projection<double> predicted = project(evidence.cameras[s.camera].model, q.x, q.y, q.z);
					++fitted;
					++camera.residuals;
					depths.push_back(q.z);
					sumAngles[s.camera] += std::pow(angleBetween(s.ray, q), 2);
					if (predicted.ok())
					{
						const math::Vec2d r = math::Vec2d{predicted.u, predicted.v} - s.pixel;
						sumPixels[s.camera] += math::dot(r, r);
						rmsWhitened = std::sqrt(detail::whitenedSquared(r, s.covariance, request.noise));
					}
					if (rmsWhitened > request.loss.scale)
					{
						++outliers;
						++camera.outliers;
						diagnostics.outliers.push_back({track.track->identity, camera.camera, rmsWhitened});
					}
				}
			}
		}
		for (std::size_t c = 0; c < n; ++c)
		{
			CameraEvidence& camera = diagnostics.cameras[c];
			if (camera.residuals > 0)
			{
				camera.rmsPixels = std::sqrt(sumPixels[c] / camera.residuals);
				camera.rmsAngle = std::sqrt(sumAngles[c] / camera.residuals);
			}
		}
		diagnostics.medianDepth = median(depths);

		report.status = RegistrationStatus::Succeeded;
		for (std::size_t c = 0; c < n; ++c)
			report.cameras.push_back({evidence.cameras[c].camera, cameraFromReference[c].inverse()});
		report.cameras[reference].referenceFromCamera = math::RigidTransformd{};
		report.scale.scale = Scale::Arbitrary;
		report.scale.evidence =
			lain::string::format("normalised: the median depth of the {} landmarks the reference sees is 1", depthCount);

		// 9. Held-out tracks, member by member.
		if (validating.empty())
			report.heldOut = Unavailable{"no tracks were held out"};
		else
			report.heldOut = validate(evidence.cameras, report, set, validating, request.execution);

		// 10. Stability: registrations of bootstrap resamples of the fitting tracks, drawn before anything
		// runs (detail::bootstrapDraws). A resample leaves its own scale, so each is aligned to the result
		// by the one factor that best maps its camera centres onto the result's before it is measured.
		if (request.resamples == 0)
			report.resampling = Unavailable{"resampling was not requested"};
		else if (fitting.size() < 2)
			report.resampling = Unavailable{"too few tracks to resample"};
		else
		{
			const std::vector<std::vector<std::uint32_t>> draws = detail::bootstrapDraws(fitting, request.resamples, request.seed);
			std::vector<std::optional<std::vector<math::RigidTransformd>>> registrations(draws.size());
			detail::forEach(draws.size(), request.execution, [&](std::size_t r)
							{ registrations[r] = registerUnits(evidence, draws[r], reference, request); });

			std::vector<double> rotation(n, 0), translation(n, 0);
			std::uint32_t produced = 0;
			for (const auto& registration : registrations)
			{
				if (!registration)
					continue;
				double across = 0, along = 0;
				for (std::size_t c = 0; c < n; ++c)
				{
					const math::Vec3d resampled = (*registration)[c].inverse().translation();
					across += math::dot(resampled, report.cameras[c].referenceFromCamera.translation());
					along += math::dot(resampled, resampled);
				}
				if (!(along > 0))
					continue;
				const double s = across / along;
				++produced;
				for (std::size_t c = 0; c < n; ++c)
				{
					const math::RigidTransformd resampled = (*registration)[c].inverse();
					const math::RigidTransformd& result = report.cameras[c].referenceFromCamera;
					const double angle = rotationBetween(resampled, result);
					const math::Vec3d moved = resampled.translation() * s - result.translation();
					rotation[c] += angle * angle;
					translation[c] += math::dot(moved, moved);
				}
			}
			if (produced < 2)
				report.resampling = Unavailable{"fewer than two resamples produced a registration"};
			else
			{
				ResamplingEvidence out;
				out.resamples = produced;
				for (std::size_t c = 0; c < n; ++c)
				{
					const double r = std::sqrt(rotation[c] / produced), t = std::sqrt(translation[c] / produced);
					if (r > out.rotationVariation)
					{
						out.rotationVariation = r;
						out.worstCamera = evidence.cameras[c].camera;
					}
					out.translationVariationAbsolute = std::max(out.translationVariationAbsolute, t);
				}
				out.translationVariation = diagnostics.medianDepth > 0
											   ? out.translationVariationAbsolute / diagnostics.medianDepth
											   : std::numeric_limits<double>::infinity();
				if (out.worstCamera.empty())
					out.worstCamera = evidence.cameras[reference].camera;
				report.resampling = out;
			}
		}

		// 11. The verdict.
		detail::judge(report, graph, outliers, fitted);
		report.elapsed = core::Time::now() - start;
		return report;
	}

	// The track-set entry point's work, shared with the footage one after it has extracted: every check
	// that reads the track set, then the analysis.
	static Report registerTrackSet(Report report, const std::vector<RigCamera>& cameras, const feature::TrackSet& set,
								   const Request& request, core::Time start)
	{
		TargetlessDiagnostics& targetless = std::get<TargetlessDiagnostics>(report.diagnostics.method);
		targetless.tracks = std::uint32_t(set.tracks.size());

		detail::CanonicalCameras canonical;
		if (std::optional<Report> failure = prepareCameras(report, canonical, cameras, request, start))
			return std::move(*failure);
		std::vector<std::uint32_t> cameraOfView;
		if (std::optional<Report> failure = checkTrackSet(report, canonical, set, cameraOfView, start))
			return std::move(*failure);

		// Each model against the geometry its view's pixels are in.
		std::vector<std::optional<ImageGeometry>> geometry(canonical.cameras.size());
		for (std::size_t v = 0; v < set.views.size(); ++v)
			geometry[cameraOfView[v]] = set.views[v].image;
		std::vector<Applicability> applicability;
		if (std::optional<Report> failure =
				detail::checkApplicability(report, applicability, canonical.cameras, geometry, request, start))
			return std::move(*failure);
		if (std::optional<Report> failure = checkBackends(report, start))
			return std::move(*failure);

		// The record: every source the views' frames came from.
		std::set<std::string> sources;
		report.reproducibility.frames = 0;
		for (const feature::View& view : set.views)
		{
			for (const media::FrameRef& frame : view.frames)
			{
				sources.insert(frame.source.toString());
				++report.reproducibility.frames;
			}
		}
		report.reproducibility.sources.assign(sources.begin(), sources.end());

		// The usable evidence, in track identity order.
		Evidence evidence;
		evidence.cameras = canonical.cameras;
		std::vector<const feature::Track*> ordered;
		for (const feature::Track& track : set.tracks)
			ordered.push_back(&track);
		std::sort(ordered.begin(), ordered.end(), [](const feature::Track* x, const feature::Track* y)
				  { return x->identity < y->identity; });
		for (const feature::Track* track : ordered)
		{
			Usable usable;
			usable.track = track;
			for (const feature::SceneObservation& o : track->observations)
			{
				const std::uint32_t camera = cameraOfView[o.view];
				const Unprojection<double> ray = unproject(canonical.cameras[camera].model, o.pixel.x, o.pixel.y);
				if (ray.ok())
					usable.seen.push_back({camera, o.pixel, o.covariance, math::normalize(math::Vec3d{ray.x, ray.y, ray.z})});
			}
			std::sort(usable.seen.begin(), usable.seen.end(), [](const Seen& x, const Seen& y)
					  { return x.camera < y.camera; });
			if (usable.seen.size() >= 2)
				evidence.tracks.push_back(std::move(usable));
		}
		return analyse(std::move(report), evidence, set, applicability, canonical.reference, request, start);
	}

	static Report startReport(const Request& request)
	{
		Report report;
		report.reproducibility.request = request;
		report.diagnostics.method = TargetlessDiagnostics{};
		report.reproducibility.method = TargetlessRecord{};
		return report;
	}

	// --- registerCameras ---------------------------------------------------------------

	Report registerCameras(const std::vector<RigCamera>& cameras, const feature::TrackSet& tracks, const Request& request)
	{
		const core::Time start = core::Time::now();
		return registerTrackSet(startReport(request), cameras, tracks, request, start);
	}

	Report registerCameras(const std::vector<RigFootage>& cameras, const std::vector<capture::CaptureGroup>& groups,
						   const feature::ExtractionRequest& extraction, const Request& request)
	{
		const core::Time start = core::Time::now();
		Report report = startReport(request);
		TargetlessRecord& record = std::get<TargetlessRecord>(report.reproducibility.method);
		feature::ExtractionRequest asRun = extraction;
		asRun.execution = request.execution;
		record.extraction = asRun;

		// Every check that needs no frame comes first, so a request that cannot succeed decodes nothing.
		std::vector<RigCamera> rig;
		std::vector<ImageGeometry> extents;
		for (const RigFootage& c : cameras)
		{
			rig.push_back({c.camera, c.model});
			const math::Vec2i extent = c.footage.spec().extent;
			extents.push_back(ImageGeometry{std::uint32_t(std::max(extent.x, 0)), std::uint32_t(std::max(extent.y, 0))});
		}
		detail::CanonicalCameras canonical;
		if (std::optional<Report> failure = prepareCameras(report, canonical, rig, request, start))
			return std::move(*failure);
		std::vector<std::optional<ImageGeometry>> geometry;
		for (const std::size_t given : canonical.given)
			geometry.push_back(extents[given]);
		std::vector<Applicability> applicability;
		if (std::optional<Report> failure =
				detail::checkApplicability(report, applicability, canonical.cameras, geometry, request, start))
			return std::move(*failure);
		if (!feature::canExtract())
			return failed(std::move(report), start, Failure::NoFeatureExtractor,
						  "this build has no feature extractor (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (!feature::canMatch())
			return failed(std::move(report), start, Failure::NoFeatureMatcher,
						  "this build has no feature matcher (configure with -DLAIN_CAMERA_OPENCV=ON)");
		if (std::optional<Report> failure = checkBackends(report, start))
			return std::move(*failure);

		feature::ExtractionResult extracted = feature::extractTracks(cameras, groups, asRun);
		TargetlessDiagnostics& targetless = std::get<TargetlessDiagnostics>(report.diagnostics.method);
		targetless.extraction = extracted.report;
		record.extractor = extracted.report.extractor;
		record.matcher = extracted.report.matcher;
		if (!extracted.ok())
		{
			const Failure failure =
				extracted.status == feature::Status::InvalidInput ? Failure::InvalidDataset : Failure::ExtractionFailed;
			return failed(std::move(report), start, failure, extracted.detail);
		}
		// The tracks outlive the report they are kept in while it is moved: validation reads them last.
		const feature::TrackSet set = std::move(extracted.trackSet);
		targetless.trackSet = set;
		return registerTrackSet(std::move(report), rig, set, request, start);
	}
} // namespace lain::camera::registration::targetless
