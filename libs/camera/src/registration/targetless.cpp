#include "lain/camera/registration/targetless.h"

#include "common.h"
#include "lain/camera/feature/geometry.h"
#include "lain/camera/feature/matching.h"
#include "lain/camera/projection.h"
#include "lain/camera/registration/observationgraph.h"
#include "lain/camera/registration/refiner.h"
#include "lain/camera/registration/validation.h"
#include "reconstruction.h"

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

	// The incremental reconstruction (reconstruction.h), which targetless calibration will share. A view
	// there is a camera here.
	using reconstruction::Candidates;
	using reconstruction::candidates;
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

	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// Fewer usable tracks than this and none are held out: a track is one point, so a rig with a
	// handful has too little to register from and too little to validate with.
	static constexpr std::size_t kMinimumToHoldOut = 20;

	// The geometry every pose of initialisation is measured by: the facades' default inlier angle, which
	// is also where a triangulation becomes a point at infinity, and the request's seed. The same in
	// both entry points, so registering footage is extracting and then registering its tracks, exactly.
	static feature::GeometryRequest geometryRequest(const Request& request)
	{
		feature::GeometryRequest geometry;
		geometry.seed = request.seed;
		return geometry;
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
				if (members[m].view == reference && kept[e][m])
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
		problem.models = evidence.models;
		problem.cameraFromReference = start.viewFromReference;
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
						{members[m].view, landmark, members[m].pixel, members[m].covariance});
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
		const std::size_t n = evidence.models.size();
		const std::vector<std::vector<std::uint32_t>> cameras = viewsOf(evidence, units);
		const ObservationGraph graph = observationGraph(std::uint32_t(n), cameras);
		if (graph.components.size() != 1)
			return std::nullopt;
		std::vector<std::uint32_t> all(n);
		std::iota(all.begin(), all.end(), 0u);
		const Candidates found = candidates(graph, cameras, unitsOf(n, cameras), all, kSeedCandidates);
		Provenance unused;
		const std::optional<Seed> seed = chooseSeed(evidence, units, found.covering, geometryRequest(request), unused);
		if (!seed)
			return std::nullopt;
		const Placement placement = place(evidence, units, *seed, geometryRequest(request));
		if (placement.order.size() != n)
			return std::nullopt;
		Start start = startFrom(evidence, units, placement, reference);
		if (!start.starved.empty())
			return std::nullopt;
		scaleBy(normalisation(evidence, units, start.landmarks, start.kept, reference).first, start.viewFromReference,
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
	static std::optional<ComponentEstimate> estimate(const std::vector<RigCamera>& rig, const Evidence& evidence,
													 const Units& units,
													 const ObservationGraph& graph,
													 const std::vector<std::vector<std::uint32_t>>& cameras,
													 const std::vector<std::vector<std::uint32_t>>& ofCamera,
													 const std::vector<std::uint32_t>& component, const Request& request)
	{
		Candidates found = candidates(graph, cameras, ofCamera, component, kSeedCandidates);
		if (found.covering.empty() && found.top)
			found.covering.push_back(*found.top);
		Provenance unused;
		const std::optional<Seed> seed = chooseSeed(evidence, units, found.covering, geometryRequest(request), unused);
		if (!seed)
			return std::nullopt;
		const Placement placement = place(evidence, units, *seed, geometryRequest(request));
		ComponentEstimate out;
		out.reference = rig[seed->a].camera;
		for (const std::uint32_t c : placement.order)
			out.cameras.push_back({rig[c].camera, placement.viewFromWorld[c]->inverse()});
		std::sort(out.cameras.begin(), out.cameras.end(), [](const RegisteredCamera& x, const RegisteredCamera& y)
				  { return x.camera < y.camera; });
		return out;
	}

	static std::string listOf(const std::vector<RigCamera>& rig, const std::vector<std::uint32_t>& cameras)
	{
		std::string out;
		for (std::size_t i = 0; i < cameras.size(); ++i)
			out += (i ? ", " : "") + rig[cameras[i]].camera.value;
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
	static Report analyse(Report report, const std::vector<RigCamera>& rig, const Evidence& evidence,
						  const feature::TrackSet& set,
						  const std::vector<Applicability>& applicability, std::optional<std::uint32_t> requested,
						  const Request& request, core::Time start)
	{
		const std::size_t n = rig.size();
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
		const std::vector<std::vector<std::uint32_t>> usableCameras = viewsOf(evidence, usable);
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
		const std::vector<std::vector<std::uint32_t>> fittingCameras = viewsOf(evidence, fitting);
		const std::vector<std::vector<std::uint32_t>> fittingOf = unitsOf(n, fittingCameras);
		const ObservationGraph graph = observationGraph(std::uint32_t(n), fittingCameras);
		const auto edgeReport = [&](const GraphEdge& e)
		{ return GraphEdgeReport{rig[e.a].camera, rig[e.b].camera, e.shared, e.bridge}; };
		for (const GraphEdge& e : graph.edges)
			diagnostics.edges.push_back(edgeReport(e));
		for (const GraphEdge& e : weakBridges(graph, report.thresholds.ready.minimumBridgeShared))
			diagnostics.weakBridges.push_back(edgeReport(e));
		for (const std::vector<std::uint32_t>& component : graph.components)
		{
			std::vector<capture::CameraIdentity> ids;
			for (const std::uint32_t c : component)
				ids.push_back(rig[c].camera);
			diagnostics.components.push_back(std::move(ids));
		}
		for (std::size_t c = 0; c < n; ++c)
		{
			CameraEvidence camera;
			camera.camera = rig[c].camera;
			camera.applicability = applicability[c];
			camera.shared = graph.sharedPerCamera[c];
			diagnostics.cameras.push_back(std::move(camera));
		}
		if (graph.components.size() > 1)
		{
			for (const std::vector<std::uint32_t>& component : graph.components)
			{
				if (std::optional<ComponentEstimate> e =
						estimate(rig, evidence, fitting, graph, fittingCameras, fittingOf, component, request))
					diagnostics.componentEstimates.push_back(std::move(*e));
			}
			std::string detail = lain::string::format("the cameras fall into {} groups that share no track:",
													  graph.components.size());
			for (const std::vector<std::uint32_t>& component : graph.components)
				detail += " {" + listOf(rig, component) + "}";
			return failed(std::move(report), start, Failure::Disconnected, detail);
		}

		// The reference: requested, or the camera in the most shared tracks, ties to the lower identity.
		report.referenceRequested = requested.has_value();
		const std::uint32_t reference = detail::chooseReference(graph.sharedPerCamera, requested);
		report.reference = rig[reference].camera;

		// 5. Placeability, then the seed.
		const Candidates found = candidates(graph, fittingCameras, fittingOf, all, kSeedCandidates);
		if (found.covering.empty())
		{
			if (found.top)
			{
				for (std::uint32_t c = 0; c < n; ++c)
				{
					if (!found.top->placed[c])
						targetless.unplaceable.push_back(rig[c].camera);
				}
				if (std::optional<ComponentEstimate> e =
						estimate(rig, evidence, fitting, graph, fittingCameras, fittingOf, all, request))
					diagnostics.componentEstimates.push_back(std::move(*e));
			}
			else
				targetless.unplaceable = [&]
				{
					std::vector<capture::CameraIdentity> ids;
					for (const RigCamera& c : rig)
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
		const std::optional<Seed> seed = chooseSeed(evidence, fitting, found.covering, geometryRequest(request), record.geometry);
		if (!seed)
			return failed(std::move(report), start, Failure::InitialisationFailed,
						  "no camera pair's relative pose could be estimated from the tracks it shares");
		targetless.seed = {rig[seed->a].camera, rig[seed->b].camera};
		targetless.seedAngle = seed->angle;
		targetless.seedBelowFloor = seed->belowFloor;

		// 6. Incremental initialisation.
		const Placement placement = place(evidence, fitting, *seed, geometryRequest(request));
		for (const std::uint32_t c : placement.order)
			targetless.placement.push_back(rig[c].camera);
		if (placement.order.size() != n)
		{
			std::vector<std::uint32_t> unplaced;
			for (std::uint32_t c = 0; c < n; ++c)
			{
				if (!placement.viewFromWorld[c])
					unplaced.push_back(c);
			}
			return failed(std::move(report), start, Failure::InitialisationFailed,
						  "cameras " + listOf(rig, unplaced) + " could not be placed by absolute pose");
		}

		// 7. Cheirality.
		Start begin = startFrom(evidence, fitting, placement, reference);
		targetless.behind = begin.behind;
		if (!begin.starved.empty())
			return failed(std::move(report), start, Failure::InitialisationFailed,
						  lain::string::format("cameras {} see fewer than {} landmarks in front of them",
											   listOf(rig, begin.starved), kPlaceLandmarks));
		scaleBy(normalisation(evidence, fitting, begin.landmarks, begin.kept, reference).first, begin.viewFromReference,
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
				CameraEvidence& camera = diagnostics.cameras[s.view];
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
					const math::Vec3d q = cameraFromReference[s.view].apply(*landmarks[e]);
					const Projection<double> predicted = project(rig[s.view].model, q.x, q.y, q.z);
					++fitted;
					++camera.residuals;
					depths.push_back(q.z);
					sumAngles[s.view] += std::pow(angleBetween(s.ray, q), 2);
					if (predicted.ok())
					{
						const math::Vec2d r = math::Vec2d{predicted.u, predicted.v} - s.pixel;
						sumPixels[s.view] += math::dot(r, r);
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
			report.cameras.push_back({rig[c].camera, cameraFromReference[c].inverse()});
		report.cameras[reference].referenceFromCamera = math::RigidTransformd{};
		report.scale.scale = Scale::Arbitrary;
		report.scale.evidence =
			lain::string::format("normalised: the median depth of the {} landmarks the reference sees is 1", depthCount);

		// 9. Held-out tracks, member by member.
		if (validating.empty())
			report.heldOut = Unavailable{"no tracks were held out"};
		else
			report.heldOut = validate(rig, report, set, validating, request.execution);

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
						out.worstCamera = rig[c].camera;
					}
					out.translationVariationAbsolute = std::max(out.translationVariationAbsolute, t);
				}
				out.translationVariation = diagnostics.medianDepth > 0
											   ? out.translationVariationAbsolute / diagnostics.medianDepth
											   : std::numeric_limits<double>::infinity();
				if (out.worstCamera.empty())
					out.worstCamera = rig[reference].camera;
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
		for (const RigCamera& c : canonical.cameras)
			evidence.models.push_back(c.model);
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
					  { return x.view < y.view; });
			if (usable.seen.size() >= 2)
				evidence.tracks.push_back(std::move(usable));
		}
		return analyse(std::move(report), canonical.cameras, evidence, set, applicability, canonical.reference, request, start);
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
