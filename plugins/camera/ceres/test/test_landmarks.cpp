// The Ceres refiner's landmarks (ADR-0017, amended 2026-10-06): scene points whose positions are
// themselves unknown, beside or instead of the bodies. Triangulation with every camera held, the
// whitening shared with a corner, the linear solver chosen by the problem's shape, and cameras and
// landmarks refined together with the scale left free, which is measured here because targetless
// registration (M9 slice 3) depends on how it behaves. Every tolerance is a measurement, written
// beside its check.

#include "syntheticrig.h"

#include <lain/camera/ceres/register.h>
#include <lain/camera/registration/refiner.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::registration;
using namespace lain::camera::testing;
using Catch::Matchers::EndsWith;
using Catch::Matchers::WithinRel;

namespace
{
	constexpr double kTau = 6.283185307179586;

	void ensureCeres()
	{
		static const bool once = []
		{
			lain::camera::ceres::registerBackend();
			return true;
		}();
		(void)once;
		REQUIRE(refinerRegistry().keys() == std::vector<std::string>{"ceres"});
	}

	double rotationBetween(const math::RigidTransformd& a, const math::RigidTransformd& b)
	{
		const math::Quatd r = math::conjugate(a.rotation()) * b.rotation();
		return 2.0 * std::atan2(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z), std::abs(r.w));
	}

	// Box-Muller over a seeded engine: the standard's distributions are implementation-defined, and
	// the measurements written beside the checks must not belong to one standard library.
	class Random
	{
	public:
		explicit Random(std::uint64_t seed)
			: m_engine(seed)
		{
		}

		double uniform() { return double(m_engine() >> 11) * 0x1.0p-53; } // [0, 1)
		double gaussian()
		{
			const double u1 = (double(m_engine() >> 11) + 0.5) * 0x1.0p-53;
			const double u2 = uniform();
			return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTau * u2);
		}
		math::Vec3d direction() { return math::normalize(math::Vec3d{gaussian(), gaussian(), gaussian()}); }

	private:
		std::mt19937_64 m_engine;
	};

	struct PointScene
	{
		std::vector<math::RigidTransformd> cameraFromReference; // the truth
		std::vector<math::Vec3d> points;						// the truth, in the reference frame
		Problem problem;										// started at the truth
	};

	// `cameras` cameras spread over `spread` radians of a circle `radius` metres about the origin,
	// looking at it (the whole circle when `spread` is kTau), and `landmarks` points in a cube of
	// half-side `half` about the origin. Landmark i is seen by the `views` cameras from i mod
	// `cameras` on, its pixels carrying `noise` px of Gaussian noise. The problem starts at the
	// truth with camera 0 the reference, its sigma the noise's, and no robust loss.
	PointScene pointScene(std::size_t cameras, std::size_t landmarks, std::size_t views, double noise, std::uint64_t seed,
						  double radius = 1.0, double spread = 1.4, double half = 0.25)
	{
		Random random(seed);
		PointScene scene;
		const bool ring = spread >= kTau;
		for (std::size_t c = 0; c < cameras; ++c)
		{
			const double a =
				ring ? spread * double(c) / double(cameras) : spread * (double(c) / double(cameras - 1) - 0.5);
			const math::Vec3d position{radius * std::sin(a), -0.05 * double(c % 3), -radius * std::cos(a)};
			scene.cameraFromReference.push_back(rig::lookingAt(position, math::Vec3d{0.0}).inverse());
			scene.problem.models.push_back(rig::model());
		}
		scene.problem.cameraFromReference = scene.cameraFromReference;
		for (std::size_t i = 0; i < landmarks; ++i)
		{
			const math::Vec3d p{half * (2 * random.uniform() - 1), half * (2 * random.uniform() - 1),
								half * (2 * random.uniform() - 1)};
			scene.points.push_back(p);
			for (std::size_t k = 0; k < views; ++k)
			{
				const std::size_t c = (i + k) % cameras;
				const math::Vec3d q = scene.cameraFromReference[c].apply(p);
				const Projection<double> pixel = project(scene.problem.models[c], q.x, q.y, q.z);
				REQUIRE(pixel.ok());
				const ImageGeometry& image = scene.problem.models[c].image();
				REQUIRE(pixel.u >= 0);
				REQUIRE(pixel.v >= 0);
				REQUIRE(pixel.u <= double(image.width - 1));
				REQUIRE(pixel.v <= double(image.height - 1));
				scene.problem.landmarkObservations.push_back(
					{std::uint32_t(c), std::uint32_t(i), {pixel.u + noise * random.gaussian(), pixel.v + noise * random.gaussian()}, std::nullopt});
			}
		}
		scene.problem.landmarks = scene.points;
		scene.problem.noise.pixelSigma = noise > 0 ? noise : 1.0;
		scene.problem.loss.family = LossFamily::None;
		return scene;
	}

	math::Vec3d centreOf(const math::RigidTransformd& cameraFromReference)
	{
		return cameraFromReference.inverse().translation();
	}

	// Every camera but the reference turned `angle` about a random axis and its centre moved
	// `fraction` of its distance from the reference's, in a random direction.
	void moveCameras(Problem& problem, double angle, double fraction, Random& random)
	{
		const math::Vec3d reference = centreOf(problem.cameraFromReference[problem.reference]);
		for (std::size_t c = 0; c < problem.cameraFromReference.size(); ++c)
		{
			if (c == problem.reference)
				continue;
			const math::RigidTransformd referenceFromCamera = problem.cameraFromReference[c].inverse();
			const math::Vec3d centre = referenceFromCamera.translation();
			const math::Vec3d moved = centre + fraction * math::length(centre - reference) * random.direction();
			const math::Quatd turned = math::angleAxis(angle, random.direction()) * referenceFromCamera.rotation();
			problem.cameraFromReference[c] = math::RigidTransformd{turned, moved}.inverse();
		}
	}

	// Every landmark moved `fraction` of its distance from the reference camera, in a random direction.
	void moveLandmarks(Problem& problem, double fraction, Random& random)
	{
		const math::Vec3d reference = centreOf(problem.cameraFromReference[problem.reference]);
		for (math::Vec3d& p : problem.landmarks)
			p += fraction * math::length(p - reference) * random.direction();
	}

	// The start a targetless initialisation might leave: cameras 5 mrad and 5% out, landmarks 5%.
	void perturb(Problem& problem, std::uint64_t seed)
	{
		Random random(seed);
		moveCameras(problem, 0.005, 0.05, random);
		moveLandmarks(problem, 0.05, random);
	}

	// The scale of an estimate against the truth about `about` (the centre a held reference camera
	// fixes, so the one degree of freedom it leaves): the s with estimate - about = s (truth - about)
	// in least squares, over every camera centre and landmark.
	double scaleOf(const std::vector<math::RigidTransformd>& cameras, const std::vector<math::Vec3d>& landmarks,
				   const PointScene& truth, const math::Vec3d& about)
	{
		double along = 0, squared = 0;
		const auto add = [&](const math::Vec3d& estimate, const math::Vec3d& actual)
		{
			along += math::dot(estimate - about, actual - about);
			squared += math::dot(actual - about, actual - about);
		};
		for (std::size_t c = 0; c < cameras.size(); ++c)
			add(centreOf(cameras[c]), centreOf(truth.cameraFromReference[c]));
		for (std::size_t i = 0; i < landmarks.size(); ++i)
			add(landmarks[i], truth.points[i]);
		return along / squared;
	}

	// The worst camera rotation, camera centre and landmark error of an estimate against the truth,
	// once its scale `s` about `about` is taken out.
	struct Errors
	{
		double rotation = 0, centre = 0, landmark = 0;
	};
	Errors errorsOf(const std::vector<math::RigidTransformd>& cameras, const std::vector<math::Vec3d>& landmarks,
					const PointScene& truth, const math::Vec3d& about, double s)
	{
		Errors out;
		for (std::size_t c = 0; c < cameras.size(); ++c)
		{
			out.rotation = std::max(out.rotation, rotationBetween(cameras[c], truth.cameraFromReference[c]));
			const math::Vec3d centre = about + (centreOf(cameras[c]) - about) / s;
			out.centre = std::max(out.centre, math::length(centre - centreOf(truth.cameraFromReference[c])));
		}
		for (std::size_t i = 0; i < landmarks.size(); ++i)
			out.landmark = std::max(out.landmark, math::length(about + (landmarks[i] - about) / s - truth.points[i]));
		return out;
	}

	bool unmoved(const Solution& solution, const Problem& problem)
	{
		for (std::size_t c = 0; c < problem.cameraFromReference.size(); ++c)
		{
			if (rotationBetween(solution.cameraFromReference[c], problem.cameraFromReference[c]) > 1e-12 ||
				math::length(solution.cameraFromReference[c].translation() - problem.cameraFromReference[c].translation()) > 1e-12)
				return false;
		}
		return true;
	}
} // namespace

TEST_CASE("a held-camera refinement triangulates a landmark", "[camera][ceres][landmark]")
{
	ensureCeres();
	PointScene scene = pointScene(3, 1, 3, 0.0, 1);
	Problem& problem = scene.problem;
	problem.freeCameras = false;
	problem.landmarks[0] += math::Vec3d{0.03, -0.04, 0.0}; // 5 cm out
	const Solution solution = refine(problem);
	REQUIRE(solution.status == RefinementStatus::Converged);
	CHECK(unmoved(solution, problem));
	REQUIRE(solution.landmarks.size() == 1);
	// Measured: 7e-12 m from the truth, in four iterations.
	CHECK(math::length(solution.landmarks[0] - scene.points[0]) < 1e-9);
	CHECK(solution.finalCost < 1e-12);
	CHECK_THAT(solution.detail, EndsWith("linear solver DENSE_QR"));
}

TEST_CASE("the refiner whitens a landmark observation as it whitens a corner", "[camera][ceres][landmark]")
{
	ensureCeres();
	// Every landmark 1 mm right of where its pixels say: the cost at the start is the whitened
	// residuals', whatever the solver would then do with them.
	PointScene scene = pointScene(4, 20, 3, 0.0, 2);
	Problem& problem = scene.problem;
	problem.freeCameras = false;
	problem.maximumIterations = 0;
	for (math::Vec3d& p : problem.landmarks)
		p += math::Vec3d{0.001, 0, 0};

	const auto residualOf = [&](const LandmarkObservation& o)
	{
		const math::Vec3d q = problem.cameraFromReference[o.camera].apply(problem.landmarks[o.landmark]);
		const Projection<double> predicted = project(problem.models[o.camera], q.x, q.y, q.z);
		return math::Vec2d{predicted.u, predicted.v} - o.pixel;
	};

	SECTION("by the noise model's sigma")
	{
		problem.noise.pixelSigma = 0.5;
		double expected = 0;
		for (const LandmarkObservation& o : problem.landmarkObservations)
		{
			const math::Vec2d r = residualOf(o);
			expected += math::dot(r, r) / 0.25;
		}
		const Solution solution = refine(problem);
		CHECK_THAT(solution.initialCost, WithinRel(0.5 * expected, 1e-9));
	}
	SECTION("by a covariance, through its Cholesky factor")
	{
		// An anisotropic, correlated covariance: the cost is 0.5 r^T C^-1 r, summed.
		const std::array<double, 3> c{0.04, 0.01, 0.09};
		const double det = c[0] * c[2] - c[1] * c[1];
		double expected = 0;
		for (LandmarkObservation& o : problem.landmarkObservations)
		{
			o.covariance = c;
			const math::Vec2d r = residualOf(o);
			expected += (c[2] * r.x * r.x - 2 * c[1] * r.x * r.y + c[0] * r.y * r.y) / det;
		}
		const Solution solution = refine(problem);
		CHECK_THAT(solution.initialCost, WithinRel(0.5 * expected, 1e-9));
	}
}

TEST_CASE("the linear solver follows the problem's shape", "[camera][ceres][landmark]")
{
	ensureCeres();
	PointScene scene = pointScene(4, 50, 3, 0.0, 3);
	Problem& problem = scene.problem;
	const std::vector<LandmarkObservation> seen = problem.landmarkObservations;

	// The same points as one rigid body at the reference frame's origin: a board's corners, as far as
	// the refiner can tell.
	const auto asBody = [&](Problem& p, std::size_t count)
	{
		p.referenceFromBody = {math::RigidTransformd{}};
		for (const LandmarkObservation& o : seen)
		{
			if (o.landmark < count)
				p.observations.push_back({o.camera, 0, scene.points[o.landmark], o.pixel, std::nullopt});
		}
	};
	const auto keepLandmarks = [](Problem& p, std::size_t count)
	{
		p.landmarks.resize(count);
		std::vector<LandmarkObservation> kept;
		for (const LandmarkObservation& o : p.landmarkObservations)
		{
			if (o.landmark < count)
				kept.push_back(o);
		}
		p.landmarkObservations = kept;
	};

	std::string expected;
	SECTION("free cameras eliminate the landmarks first")
	{
		expected = "SPARSE_SCHUR";
	}
	SECTION("held cameras and one body: dense")
	{
		problem.freeCameras = false;
		keepLandmarks(problem, 0);
		asBody(problem, 50);
		expected = "DENSE_QR";
	}
	SECTION("held cameras and one landmark: dense")
	{
		problem.freeCameras = false;
		keepLandmarks(problem, 1);
		expected = "DENSE_QR";
	}
	SECTION("held cameras and many landmarks: sparse")
	{
		problem.freeCameras = false;
		expected = "SPARSE_NORMAL_CHOLESKY";
	}
	SECTION("held cameras, a body and a landmark: both kinds count")
	{
		problem.freeCameras = false;
		keepLandmarks(problem, 1);
		asBody(problem, 50);
		expected = "SPARSE_NORMAL_CHOLESKY";
	}
	const Solution solution = refine(problem);
	INFO(expected << ": " << solution.detail);
	REQUIRE(solution.usable());
	CHECK_THAT(solution.detail, EndsWith("linear solver " + expected));
}

TEST_CASE("20000 landmarks triangulate against held cameras", "[camera][ceres][landmark]")
{
#ifndef NDEBUG
	SKIP("runs in Release builds: measured 26 s in Debug, against 0.5 s in Release");
#endif
	ensureCeres();
	// Eight cameras over 1.4 rad at 1 m, each landmark seen by four of them through 0.3 px of noise,
	// and started 5% of its depth out.
	PointScene scene = pointScene(8, 20000, 4, 0.3, 4);
	Problem& problem = scene.problem;
	problem.freeCameras = false;
	Random random(40);
	moveLandmarks(problem, 0.05, random);
	const Solution solution = refine(problem);
	REQUIRE(solution.status == RefinementStatus::Converged);
	CHECK(unmoved(solution, problem));
	CHECK_THAT(solution.detail, EndsWith("linear solver SPARSE_NORMAL_CHOLESKY"));
	double worst = 0, sum = 0;
	for (std::size_t i = 0; i < scene.points.size(); ++i)
	{
		const double e = math::length(solution.landmarks[i] - scene.points[i]);
		worst = std::max(worst, e);
		sum += e * e;
	}
	// Measured on linux-x86_64, Release: 0.54 s and 78 MB at peak, four iterations; the worst point
	// 6.7 mm out and the RMS 1.3 mm, and the final cost 0.997 of half the residual dimensions less the
	// unknowns, which is where whitened noise leaves it.
	CHECK(worst < 0.01);
	CHECK(std::sqrt(sum / double(scene.points.size())) < 0.002);
	const double dimensions = 2.0 * double(problem.landmarkObservations.size()) - 3.0 * double(scene.points.size());
	CHECK_THAT(solution.finalCost, WithinRel(0.5 * dimensions, 0.05));
}

TEST_CASE("cameras and landmarks converge together, with the scale left free", "[camera][ceres][landmark]")
{
	ensureCeres();
	// Eight cameras over 1.4 rad at 1 m and 2000 landmarks, each seen by four of them through 0.3 px
	// of noise. Every camera but the reference starts turned 5 mrad and moved 5% of its distance from
	// the reference; every landmark 5% of its distance.
	PointScene scene = pointScene(8, 2000, 4, 0.3, 5);
	Problem& problem = scene.problem;
	perturb(problem, 50);
	const math::Vec3d about = centreOf(problem.cameraFromReference[0]);
	const double started = scaleOf(problem.cameraFromReference, problem.landmarks, scene, about);
	const Solution solution = refine(problem);
	REQUIRE(solution.status == RefinementStatus::Converged);
	CHECK_THAT(solution.detail, EndsWith("linear solver SPARSE_SCHUR"));
	CHECK(rotationBetween(solution.cameraFromReference[0], problem.cameraFromReference[0]) < 1e-12);
	// The scale is wherever the solver's path left it: measured 1.022 here, from a start at 1.0004,
	// and between 0.980 and 1.012 over twelve other seeds, the same at 0 px of noise as at 0.3, in four
	// or five iterations every time. Within what the starts were moved, and normalised by the method
	// afterwards (ADR-0017), so nothing holds it here.
	const double s = scaleOf(solution.cameraFromReference, solution.landmarks, scene, about);
	CHECK(std::abs(started - 1) < 0.001);
	CHECK(std::abs(s - 1) < 0.05);
	CHECK(solution.iterations <= 10);
	// With the scale taken out, measured: the worst camera 0.40 mrad and 0.49 mm out, the worst
	// landmark 6.0 mm, and the final cost 1.015 of half the residual dimensions less the unknowns.
	const Errors e = errorsOf(solution.cameraFromReference, solution.landmarks, scene, about, s);
	CHECK(e.rotation < 0.001);
	CHECK(e.centre < 0.001);
	CHECK(e.landmark < 0.01);
	const double dimensions =
		2.0 * double(problem.landmarkObservations.size()) - 3.0 * double(scene.points.size()) - 6.0 * double(7);
	CHECK_THAT(solution.finalCost, WithinRel(0.5 * dimensions, 0.05));
}

TEST_CASE("a body and landmarks refine in one problem, and the body holds the scale", "[camera][ceres][landmark]")
{
	ensureCeres();
	// Four cameras over 1 rad at 0.6 m see a board at the origin, facing them, and 200 landmarks
	// behind and around it, with exact pixels. The board's corners are metric, so the result has
	// one scale: the truth's.
	PointScene scene = pointScene(4, 200, 3, 0.0, 6, 0.6, 1.0, 0.15);
	Problem& problem = scene.problem;
	const camera::board::Specification spec = specification();
	const math::RigidTransformd body{math::Quatd{1, 0, 0, 0}, -rig::centroid(spec)};
	problem.referenceFromBody = {body};
	for (std::uint32_t c = 0; c < 4; ++c)
	{
		for (std::uint32_t id = 0; id < spec.pattern().cornerCount(); ++id)
		{
			const math::Vec3d corner = *spec.cornerPosition(id);
			const math::Vec3d q = scene.cameraFromReference[c].apply(body.apply(corner));
			const Projection<double> pixel = project(problem.models[c], q.x, q.y, q.z);
			REQUIRE(pixel.ok());
			problem.observations.push_back({c, 0, corner, {pixel.u, pixel.v}, std::nullopt});
		}
	}
	perturb(problem, 60);
	problem.referenceFromBody[0] =
		math::RigidTransformd{math::angleAxis(0.01, math::Vec3d{1, 0, 0}) * body.rotation(), body.translation() + math::Vec3d{0.005, 0, 0}};
	const math::Vec3d about = centreOf(problem.cameraFromReference[0]);
	const Solution solution = refine(problem);
	REQUIRE(solution.status == RefinementStatus::Converged);
	CHECK_THAT(solution.detail, EndsWith("linear solver SPARSE_SCHUR"));
	// Measured: the scale 1 + 1e-9, and every camera, landmark and the board within 1e-9 of the truth
	// with nothing taken out, in five iterations.
	const double s = scaleOf(solution.cameraFromReference, solution.landmarks, scene, about);
	CHECK(std::abs(s - 1) < 1e-8);
	const Errors e = errorsOf(solution.cameraFromReference, solution.landmarks, scene, about, 1.0);
	CHECK(e.rotation < 1e-8);
	CHECK(e.centre < 1e-8);
	CHECK(e.landmark < 1e-8);
	CHECK(math::length(solution.referenceFromBody[0].translation() - body.translation()) < 1e-8);
}

TEST_CASE("the refiner refuses a landmark problem it cannot pose", "[camera][ceres][landmark]")
{
	ensureCeres();
	PointScene scene = pointScene(3, 5, 3, 0.0, 7);
	Problem& problem = scene.problem;
	std::string reason;
	SECTION("an observation of no camera")
	{
		problem.landmarkObservations[2].camera = 3;
		reason = "a landmark observation names no camera or landmark";
	}
	SECTION("an observation of no landmark")
	{
		problem.landmarkObservations[2].landmark = 5;
		reason = "a landmark observation names no camera or landmark";
	}
	SECTION("a covariance that is not positive definite")
	{
		problem.landmarkObservations[2].covariance = std::array<double, 3>{0.04, 0.05, 0.04};
		reason = "a landmark observation's covariance is not positive definite";
	}
	SECTION("neither kind of observation")
	{
		problem.landmarkObservations.clear();
		reason = "the problem has no observations";
	}
	const Solution solution = refine(problem);
	CHECK(solution.status == RefinementStatus::Failed);
	CHECK(solution.detail == reason);
	CHECK(solution.landmarks.empty());
}

TEST_CASE("a landmark behind a camera at the start fails the solve", "[camera][ceres][landmark]")
{
	ensureCeres();
	// Nothing drops the observation quietly: the method module must not hand one over (ADR-0017).
	PointScene scene = pointScene(3, 5, 3, 0.0, 8);
	Problem& problem = scene.problem;
	const math::Vec3d centre = centreOf(problem.cameraFromReference[1]);
	problem.landmarks[2] = centre + (centre - scene.points[2]); // mirrored through camera 1's centre
	const Solution solution = refine(problem);
	CHECK(solution.status == RefinementStatus::Failed);
	CHECK(solution.iterations == 0);
	CHECK(solution.landmarks.empty());
}

TEST_CASE("a rig of 100 cameras and 50000 landmarks refines", "[camera][ceres][landmark][scale]")
{
#ifndef NDEBUG
	SKIP("the scale test runs in Release builds: a Debug refinement is some 65 times slower");
#endif
	ensureCeres();
	// 100 cameras on a ring of 2 m radius, looking in; 50000 landmarks within 0.5 m of the centre,
	// each seen by eight neighbouring cameras through 0.3 px of noise, so the ring is closed by its
	// overlaps. Started as the smaller convergence case is.
	PointScene scene = pointScene(100, 50000, 8, 0.3, 9, 2.0, kTau, 0.5);
	Problem& problem = scene.problem;
	perturb(problem, 90);
	const math::Vec3d about = centreOf(problem.cameraFromReference[0]);
	const double started = scaleOf(problem.cameraFromReference, problem.landmarks, scene, about);
	const Solution solution = refine(problem);
	REQUIRE(solution.status == RefinementStatus::Converged);
	// Measured on linux-x86_64, Release, one thread: 4.2 s over 400,000 residuals, 324 MB at peak,
	// four iterations; the scale 1.004 from a start at 1.0002. With it taken out, the worst camera 0.79
	// mrad and 1.7 mm out and the worst landmark 16 mm, and the final cost 1.001 of half the residual
	// dimensions less the unknowns.
	const double s = scaleOf(solution.cameraFromReference, solution.landmarks, scene, about);
	CHECK(std::abs(started - 1) < 0.001);
	CHECK(std::abs(s - 1) < 0.05);
	CHECK(solution.iterations <= 10);
	const Errors e = errorsOf(solution.cameraFromReference, solution.landmarks, scene, about, s);
	CHECK(e.rotation < 0.002);
	CHECK(e.centre < 0.003);
	CHECK(e.landmark < 0.025);
	const double dimensions =
		2.0 * double(problem.landmarkObservations.size()) - 3.0 * double(scene.points.size()) - 6.0 * double(99);
	CHECK_THAT(solution.finalCost, WithinRel(0.5 * dimensions, 0.05));
}
