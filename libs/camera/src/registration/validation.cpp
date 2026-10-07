#include "lain/camera/registration/validation.h"

#include "common.h"
#include "lain/camera/feature/geometry.h"
#include "lain/camera/feature/triangulation.h"
#include "lain/camera/projection.h"
#include "lain/camera/registration/refiner.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace lain::camera::registration
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// One registered camera, as validation reads it.
	struct Registered
	{
		capture::CameraIdentity camera;
		const CameraModel* model = nullptr;
		math::RigidTransformd referenceFromCamera;
		math::RigidTransformd cameraFromReference;
	};

	// One member of a held-out track: a registered camera's observation of it.
	struct Member
	{
		std::uint32_t camera = 0; // into the registered cameras
		math::Vec2d pixel{0.0};
		std::optional<std::array<double, 3>> covariance;
		math::Vec3d ray{0.0}; // unit, in the camera's frame; meaningful when `usable`
		bool usable = false;  // its pixel unprojects
	};

	enum class Scoring
	{
		Unpredicted,
		Transfer, // against a predicted point
		Epipolar, // against the epipolar plane of a two-member track
	};

	struct Scored
	{
		std::uint32_t camera = 0;
		Scoring scoring = Scoring::Unpredicted;
		double angle = 0;			  // radians
		std::optional<double> pixels; // the predicted pixel's distance, where it projects
	};

	// Below this parallax the rays of a triangulation are a point at infinity, as the geometry
	// facades say (feature::GeometryRequest).
	static double minimumParallax()
	{
		return feature::GeometryRequest{}.angle;
	}

	// The fixed procedure of every prediction: measured covariances, else the default noise model,
	// and least squares. Whatever the report's request was.
	static Problem predictionProblem()
	{
		Problem problem;
		problem.freeCameras = false;
		problem.noise = NoiseModel{};
		problem.loss = RobustLoss{LossFamily::None, RobustLoss{}.scale};
		problem.maximumIterations = Request{}.maximumIterations;
		return problem;
	}

	// The angle between `ray` and the plane through the baseline `b` and the direction `other`; unset
	// when the plane is undefined (no baseline, or `other` within the minimum parallax of it).
	static std::optional<double> epipolarAngle(const math::Vec3d& ray, const math::Vec3d& b, const math::Vec3d& other)
	{
		if (!(math::length(b) > 0))
			return std::nullopt;
		if (std::min(detail::angleBetween(other, b), detail::angleBetween(other, -b)) < minimumParallax())
			return std::nullopt;
		const math::Vec3d normal = math::normalize(math::cross(b, other));
		const double out = std::abs(math::dot(ray, normal));
		const double in = math::length(ray - math::dot(ray, normal) * normal);
		return std::atan2(out, in);
	}

	// A two-member track: each member against the epipolar plane the other defines.
	static void scoreEpipolar(const std::vector<Registered>& registered, const Member& m0, const Member& m1,
							  Scored& s0, Scored& s1)
	{
		const Registered& c0 = registered[m0.camera];
		const Registered& c1 = registered[m1.camera];
		const math::Vec3d centre0 = c0.referenceFromCamera.translation();
		const math::Vec3d centre1 = c1.referenceFromCamera.translation();
		const math::Vec3d d0 = c0.referenceFromCamera.rotate(m0.ray);
		const math::Vec3d d1 = c1.referenceFromCamera.rotate(m1.ray);
		// The plane cannot see a point behind a camera: the pair's own triangulation can.
		if (feature::triangulate({{centre0, d0}, {centre1, d1}}, minimumParallax()).status ==
			feature::TriangulationStatus::Behind)
			return;
		const std::optional<double> a0 = epipolarAngle(d0, centre0 - centre1, d1);
		const std::optional<double> a1 = epipolarAngle(d1, centre1 - centre0, d0);
		if (!a0 || !a1)
			return;
		s0.scoring = s1.scoring = Scoring::Epipolar;
		s0.angle = *a0;
		s1.angle = *a1;
	}

	// A track of three or more members: each predicted from the others, every camera held, in one
	// refinement.
	static void scoreTransfers(const std::vector<Registered>& registered, const std::vector<const Member*>& members,
							   std::vector<Scored*>& scored)
	{
		// The cameras the track's members are in, numbered locally, so a problem holds only them.
		std::map<std::uint32_t, std::uint32_t> local;
		for (const Member* m : members)
			local.emplace(m->camera, 0u);
		Problem problem = predictionProblem();
		for (auto& [camera, index] : local)
		{
			index = std::uint32_t(problem.models.size());
			problem.models.push_back(*registered[camera].model);
			problem.cameraFromReference.push_back(registered[camera].cameraFromReference);
		}

		std::vector<std::size_t> predicted; // the members whose landmark is in the problem, in order
		for (std::size_t m = 0; m < members.size(); ++m)
		{
			std::vector<feature::Ray> rays;
			for (std::size_t o = 0; o < members.size(); ++o)
			{
				if (o == m)
					continue;
				const Registered& c = registered[members[o]->camera];
				rays.push_back({c.referenceFromCamera.translation(), c.referenceFromCamera.rotate(members[o]->ray)});
			}
			const feature::Triangulation start = feature::triangulate(rays, minimumParallax());
			if (!start.ok())
				continue;
			// A start every predicting camera can project: one that cannot fails the whole solve.
			bool projects = true;
			for (std::size_t o = 0; o < members.size() && projects; ++o)
			{
				if (o == m)
					continue;
				const math::Vec3d p = registered[members[o]->camera].cameraFromReference.apply(start.point);
				projects = project(*registered[members[o]->camera].model, p.x, p.y, p.z).ok();
			}
			if (!projects)
				continue;
			const std::uint32_t landmark = std::uint32_t(problem.landmarks.size());
			problem.landmarks.push_back(start.point);
			for (std::size_t o = 0; o < members.size(); ++o)
			{
				if (o != m)
					problem.landmarkObservations.push_back(
						{local.at(members[o]->camera), landmark, members[o]->pixel, members[o]->covariance});
			}
			predicted.push_back(m);
		}
		if (predicted.empty())
			return;
		const Solution solution = refine(problem);
		if (!solution.usable() || solution.landmarks.size() != predicted.size())
			return;

		for (std::size_t i = 0; i < predicted.size(); ++i)
		{
			const Member& member = *members[predicted[i]];
			const Registered& c = registered[member.camera];
			const math::Vec3d q = c.cameraFromReference.apply(solution.landmarks[i]);
			Scored& s = *scored[predicted[i]];
			s.scoring = Scoring::Transfer;
			s.angle = detail::angleBetween(member.ray, q);
			const Projection<double> pixel = project(*c.model, q.x, q.y, q.z);
			if (pixel.ok())
				s.pixels = math::length(math::Vec2d{pixel.u, pixel.v} - member.pixel);
		}
	}

	// --- validate ----------------------------------------------------------------------

	std::variant<HeldOutEvidence, Unavailable> validate(const std::vector<RigCamera>& cameras, const Report& report,
														const feature::TrackSet& trackSet,
														const std::vector<std::string>& trackIds, ExecutionPolicy execution)
	{
		if (report.status != RegistrationStatus::Succeeded)
			return Unavailable{"the registration failed"};
		if (trackIds.empty())
			return Unavailable{"no tracks were held out"};

		std::vector<std::string> ids = trackIds;
		std::sort(ids.begin(), ids.end());
		ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
		std::map<std::string, std::size_t> trackOf;
		for (std::size_t t = 0; t < trackSet.tracks.size(); ++t)
			trackOf.emplace(trackSet.tracks[t].identity, t);
		std::vector<std::size_t> held;
		for (const std::string& id : ids)
		{
			const auto found = trackOf.find(id);
			if (found == trackOf.end())
				return Unavailable{"track " + id.substr(0, 12) + " is not in the track set"};
			held.push_back(found->second);
		}
		if (!canRefine())
			return Unavailable{"this build has no registration refiner (configure with -DLAIN_CAMERA_CERES=ON)"};

		// The registered cameras with a model, in identity order, and each view's among them.
		std::map<capture::CameraIdentity, const CameraModel*> modelOf;
		for (const RigCamera& c : cameras)
			modelOf.emplace(c.camera, &c.model);
		std::vector<Registered> registered;
		for (const RegisteredCamera& c : report.cameras)
		{
			const auto model = modelOf.find(c.camera);
			if (model != modelOf.end())
				registered.push_back({c.camera, model->second, c.referenceFromCamera, c.referenceFromCamera.inverse()});
		}
		std::sort(registered.begin(), registered.end(), [](const Registered& a, const Registered& b)
				  { return a.camera < b.camera; });
		std::vector<std::optional<std::uint32_t>> cameraOfView(trackSet.views.size());
		for (std::size_t v = 0; v < trackSet.views.size(); ++v)
		{
			for (std::uint32_t c = 0; c < registered.size(); ++c)
			{
				if (registered[c].camera == trackSet.views[v].camera)
					cameraOfView[v] = c;
			}
		}

		// Each track's members scored, one track per task.
		std::vector<std::vector<Scored>> scores(held.size());
		detail::forEach(held.size(), execution,
						[&](std::size_t i)
						{
							const feature::Track& track = trackSet.tracks[held[i]];
							std::vector<Member> members;
							for (const feature::SceneObservation& o : track.observations)
							{
								if (o.view >= cameraOfView.size() || !cameraOfView[o.view])
									continue;
								Member m;
								m.camera = *cameraOfView[o.view];
								m.pixel = o.pixel;
								m.covariance = o.covariance;
								const Unprojection<double> ray = unproject(*registered[m.camera].model, o.pixel.x, o.pixel.y);
								m.usable = ray.ok();
								if (m.usable)
									m.ray = math::normalize(math::Vec3d{ray.x, ray.y, ray.z});
								members.push_back(m);
							}
							std::vector<Scored>& out = scores[i];
							out.resize(members.size());
							std::vector<const Member*> usable;
							std::vector<Scored*> usableScores;
							for (std::size_t m = 0; m < members.size(); ++m)
							{
								out[m].camera = members[m].camera;
								if (members[m].usable)
								{
									usable.push_back(&members[m]);
									usableScores.push_back(&out[m]);
								}
							}
							if (usable.size() == 2)
								scoreEpipolar(registered, *usable[0], *usable[1], *usableScores[0], *usableScores[1]);
							else if (usable.size() > 2)
								scoreTransfers(registered, usable, usableScores);
						});

		// Gathered serially, in track identity order.
		HeldOutEvidence evidence;
		evidence.units = std::uint32_t(held.size());
		std::vector<double> cameraAngles(registered.size(), 0);
		std::vector<std::uint32_t> cameraPredictions(registered.size(), 0);
		double angles = 0, pixels = 0;
		std::uint32_t pixelCount = 0;
		for (const std::vector<Scored>& track : scores)
		{
			for (const Scored& s : track)
			{
				if (s.scoring == Scoring::Unpredicted)
				{
					++evidence.unpredicted;
					continue;
				}
				++evidence.predictions;
				++evidence.residuals;
				// An epipolar residual has one degree of freedom, a transfer residual two.
				const std::uint32_t weight = s.scoring == Scoring::Epipolar ? 2 : 1;
				if (s.scoring == Scoring::Epipolar)
					++evidence.epipolar;
				angles += weight * s.angle * s.angle;
				cameraAngles[s.camera] += weight * s.angle * s.angle;
				++cameraPredictions[s.camera];
				if (s.pixels)
				{
					pixels += *s.pixels * *s.pixels;
					++pixelCount;
					evidence.worstPixels = std::max(evidence.worstPixels, *s.pixels);
				}
			}
		}
		if (evidence.predictions == 0)
			return Unavailable{"no held-out member could be predicted from the others"};
		evidence.rmsAngle = std::sqrt(angles / evidence.predictions);
		evidence.rmsPixels = pixelCount > 0 ? std::sqrt(pixels / pixelCount) : 0.0;
		for (std::size_t c = 0; c < registered.size(); ++c)
		{
			if (cameraPredictions[c] > 0)
				evidence.perCamera.push_back({registered[c].camera, std::sqrt(cameraAngles[c] / cameraPredictions[c])});
		}
		return evidence;
	}
} // namespace lain::camera::registration
