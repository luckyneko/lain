// The Ceres refiner's free camera models (ADR-0017, M9 slice 4, sub-slice 5): a moving camera's
// frames see through ONE shared model, which the refinement estimates from their landmarks, or a held
// camera estimates its model from a board's corners. Every case uses exact tracks, so it asserts that
// the seam is wired, not that a scene is well posed: conditioning under noise is sub-slices 9 and
// 10's to measure. Every tolerance is a measurement, written beside its check.

#include "syntheticrig.h"

#include <lain/camera/ceres/register.h>
#include <lain/camera/comparison.h>
#include <lain/camera/refinement/refiner.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace lain;
using namespace lain::camera;
using namespace lain::camera::refinement;
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

	// Box-Muller over a seeded engine, as test_landmarks.cpp's: the standard's distributions are
	// implementation-defined.
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

	// Whether a pixel is inside `model`'s image, 5 px clear of every edge.
	bool inside(const CameraModel& model, const Projection<double>& p)
	{
		const ImageGeometry& image = model.image();
		return p.ok() && p.u >= 5 && p.v >= 5 && p.u <= double(image.width) - 6 && p.v <= double(image.height) - 6;
	}

	// A moving camera seen through `truth`: `frames` frames along an arc 1.2 m wide that dips and
	// turns to look at a drifting target, and `landmarks` points in a slab 3 m wide, 2 to 5 m away,
	// each seen by `views` consecutive frames from a random one, through `noise` px of Gaussian noise.
	// Frame 0 is the reference, so the reference frame is the scene's.
	struct MovingScene
	{
		std::vector<math::RigidTransformd> cameraFromReference; // the truth
		std::vector<math::Vec3d> points;						// the truth
		std::vector<LandmarkObservation> observations;
	};

	MovingScene movingScene(const CameraModel& truth, std::size_t frames, std::size_t landmarks, std::size_t views,
							double noise, std::uint64_t seed)
	{
		Random random(seed);
		MovingScene scene;
		for (std::size_t f = 0; f < frames; ++f)
		{
			const double t = double(f) / double(frames - 1) - 0.5;
			const math::Vec3d position{1.2 * t, 0.15 * std::sin(6.0 * t), 0.3 * t * t};
			const math::Vec3d target{0.6 * t, 0.1 * std::cos(4.0 * t), 3.5};
			scene.cameraFromReference.push_back(rig::lookingAt(position, target).inverse());
		}
		std::size_t attempts = 0;
		while (scene.points.size() < landmarks)
		{
			REQUIRE(++attempts < landmarks * 50);
			const math::Vec3d p{3.0 * (random.uniform() - 0.5), 2.2 * (random.uniform() - 0.5), 2.0 + 3.0 * random.uniform()};
			const std::size_t first = std::size_t(random.uniform() * double(frames - views + 1));
			std::vector<LandmarkObservation> seen;
			for (std::size_t k = 0; k < views; ++k)
			{
				const math::Vec3d q = scene.cameraFromReference[first + k].apply(p);
				const Projection<double> pixel = project(truth, q.x, q.y, q.z);
				if (!inside(truth, pixel))
					break;
				seen.push_back({std::uint32_t(first + k), std::uint32_t(scene.points.size()), {pixel.u + noise * random.gaussian(), pixel.v + noise * random.gaussian()}, std::nullopt});
			}
			if (seen.size() < views)
				continue;
			scene.points.push_back(p);
			scene.observations.insert(scene.observations.end(), seen.begin(), seen.end());
		}
		return scene;
	}

	// `truth`'s model with its focal lengths scaled by `focal`, its principal point moved by `shift`
	// pixels, and its coefficients neutral: where a targetless calibration starts.
	CameraModel startOf(const CameraModel& truth, double focal, double shift = 0)
	{
		CameraModelParameters p = truth.parameters();
		p.intrinsics.fx *= focal;
		p.intrinsics.fy *= focal;
		p.intrinsics.cx += shift;
		p.intrinsics.cy -= shift;
		p.distortion = neutral(modelOf(truth.distortion()));
		const ModelResult start = CameraModel::create(p);
		REQUIRE(start.model.has_value());
		return *start.model;
	}

	// The problem the probe started from: every frame but the reference turned 2 mrad about a random
	// axis and moved by 1 cm of Gaussian noise, every landmark coordinate 2% out, and every frame
	// seeing through `model`, freed by `freedom`. No robust loss, since nothing is an outlier.
	Problem sharedProblem(const MovingScene& scene, const CameraModel& model, const ModelFreedom& freedom, double sigma,
						  std::uint64_t seed)
	{
		Random random(seed);
		Problem problem;
		problem.models = {model};
		problem.modelOf.assign(scene.cameraFromReference.size(), 0);
		problem.modelFreedom = {freedom};
		for (std::size_t f = 0; f < scene.cameraFromReference.size(); ++f)
		{
			math::RigidTransformd t = scene.cameraFromReference[f];
			if (f > 0)
				t = math::RigidTransformd{math::angleAxis(0.002, random.direction()),
										  0.01 * math::Vec3d{random.gaussian(), random.gaussian(), random.gaussian()}} *
					t;
			problem.cameraFromReference.push_back(t);
		}
		for (const math::Vec3d& p : scene.points)
			problem.landmarks.push_back(p * math::Vec3d{1 + 0.02 * random.gaussian(), 1 + 0.02 * random.gaussian(),
														1 + 0.02 * random.gaussian()});
		problem.landmarkObservations = scene.observations;
		problem.noise.pixelSigma = sigma;
		problem.loss.family = LossFamily::None;
		return problem;
	}

	ModelFreedom freeing(bool focalLength, bool principalPoint, bool coefficients)
	{
		ModelFreedom out;
		out.focalLength = focalLength;
		out.principalPoint = principalPoint;
		out.coefficients = coefficients;
		return out;
	}

	// How far an estimate is from the truth: where it sends the truth's rays (camera::compareModels),
	// the RAY MAPPING, and its focal lengths' relative difference. The ray mapping is what a model is
	// for, and what the rational model's unidentifiable coefficients still pin. Every ray must reach
	// the estimate, since every estimate here is near the truth.
	//
	// Until 2026-10-26 these cases measured on a grid that stopped short of the right and bottom edges.
	// Reaching them moved the rational model's ray mapping from 2.8e-6 px to 3.1e-6, and nothing else
	// here by more than 6e-8 px.
	ModelComparison errorOf(const CameraModel& estimate, const CameraModel& truth)
	{
		const ModelComparison c = compareModels(truth, estimate);
		REQUIRE(c.compared());
		REQUIRE(c.unreached == 0);
		return c;
	}

	// The refined model, through CameraModel::create, which must accept it.
	CameraModel created(const Solution& solution, std::size_t model)
	{
		REQUIRE(solution.models.size() > model);
		const ModelResult result = CameraModel::create(solution.models[model]);
		REQUIRE(result.model.has_value());
		return *result.model;
	}

	bool sameParameters(const CameraModelParameters& a, const CameraModelParameters& b)
	{
		return a.image == b.image && a.intrinsics.fx == b.intrinsics.fx && a.intrinsics.fy == b.intrinsics.fy &&
			   a.intrinsics.cx == b.intrinsics.cx && a.intrinsics.cy == b.intrinsics.cy &&
			   a.distortion.index() == b.distortion.index() && coefficients(a.distortion) == coefficients(b.distortion);
	}

	bool sameSolution(const Solution& a, const Solution& b)
	{
		if (a.status != b.status || a.iterations != b.iterations || a.finalCost != b.finalCost ||
			a.cameraFromReference.size() != b.cameraFromReference.size() || a.landmarks != b.landmarks)
			return false;
		for (std::size_t c = 0; c < a.cameraFromReference.size(); ++c)
		{
			if (a.cameraFromReference[c].rotation() != b.cameraFromReference[c].rotation() ||
				a.cameraFromReference[c].translation() != b.cameraFromReference[c].translation())
				return false;
		}
		return true;
	}
} // namespace

TEST_CASE("a shared free model is recovered for every distortion model", "[camera][ceres][freemodel]")
{
	ensureCeres();
	const int index = GENERATE(range(0, kModelCount));
	const CameraModel truth = cameraWith(everyModel(index));
	CAPTURE(std::string(displayName(truth.distortion())));

	// 24 frames, 120 landmarks each seen by 16 of them, exact. The model starts with its focal
	// lengths 5% long and its coefficients neutral, and its principal point held at the truth.
	const MovingScene scene = movingScene(truth, 24, 120, 16, 0.0, 1);
	const Problem problem = sharedProblem(scene, startOf(truth, 1.05), freeing(true, false, true), 1.0, 10);
	const Solution solution = refine(problem);
	INFO(solution.detail);
	REQUIRE(solution.status == Status::Converged);
	CHECK_THAT(solution.detail, EndsWith("linear solver SPARSE_SCHUR"));
	REQUIRE(solution.models.size() == 1);

	// Measured on linux-x86_64, the same in Debug and Release: the focal lengths within 7.5e-9 (no
	// distortion's, the worst) and the ray mapping within 3.1e-6 px (the rational model's, whose
	// coefficients are not identifiable but whose mapping is), in 12 to 26 iterations; 1.6 to 8.5 s
	// per model in Debug, 0.05 to 0.18 s in Release.
	const ModelComparison error = errorOf(created(solution, 0), truth);
	CAPTURE(error.focalDifference, error.worstPixels);
	CHECK(error.focalDifference < 1e-6);
	CHECK(error.worstPixels < 1e-4);
}

TEST_CASE("a held principal point comes back exactly where it started", "[camera][ceres][freemodel]")
{
	ensureCeres();
	// Brown-Conrady 5, started 1 px off the truth in each of cx and cy, with the focal length and
	// the coefficients free.
	const CameraModel truth = cameraWith(brownConrady());
	const MovingScene scene = movingScene(truth, 24, 120, 16, 0.0, 2);
	const CameraModel start = startOf(truth, 1.05, 1.0);

	SECTION("held: bit-equal to its start")
	{
		const Solution solution = refine(sharedProblem(scene, start, freeing(true, false, true), 1.0, 20));
		INFO(solution.detail);
		REQUIRE(solution.usable());
		REQUIRE(solution.models.size() == 1);
		CHECK(solution.models[0].intrinsics.cx == start.intrinsics().cx);
		CHECK(solution.models[0].intrinsics.cy == start.intrinsics().cy);
		// The rest moved, so the block was free.
		CHECK(solution.models[0].intrinsics.fx != start.intrinsics().fx);
	}
	SECTION("freed: it moves to the truth")
	{
		const Solution solution = refine(sharedProblem(scene, start, freeing(true, true, true), 1.0, 20));
		INFO(solution.detail);
		REQUIRE(solution.status == Status::Converged);
		const CameraModel estimate = created(solution, 0);
		CAPTURE(estimate.intrinsics().cx, estimate.intrinsics().cy);
		// Measured: 1.5e-7 px from the truth in each, in 17 iterations.
		CHECK(std::abs(estimate.intrinsics().cx - truth.intrinsics().cx) < 1e-5);
		CHECK(std::abs(estimate.intrinsics().cy - truth.intrinsics().cy) < 1e-5);
	}
}

TEST_CASE("a problem that frees no model solves as before", "[camera][ceres][freemodel]")
{
	ensureCeres();
	// A moving camera whose frames each hold a copy of the true model, started away from the truth.
	const CameraModel truth = cameraWith(brownConrady());
	const MovingScene scene = movingScene(truth, 12, 60, 8, 0.5, 3);
	Problem copies = sharedProblem(scene, truth, ModelFreedom{}, 0.5, 30);
	copies.models.assign(scene.cameraFromReference.size(), truth);
	copies.modelOf.clear();
	copies.modelFreedom.clear();
	const Solution reference = refine(copies);
	INFO(reference.detail);
	REQUIRE(reference.status == Status::Converged);
	REQUIRE(reference.models.size() == copies.models.size());
	for (const CameraModelParameters& p : reference.models)
		CHECK(sameParameters(p, truth.parameters()));

	SECTION("an explicit identity and nothing freed: bit-identical to the defaults")
	{
		Problem explicitly = copies;
		for (std::size_t c = 0; c < copies.models.size(); ++c)
			explicitly.modelOf.push_back(std::uint32_t(c));
		explicitly.modelFreedom.assign(copies.models.size(), ModelFreedom{});
		CHECK(sameSolution(refine(explicitly), reference));
	}
	SECTION("one shared held model: bit-identical to a copy per camera")
	{
		const Problem shared = sharedProblem(scene, truth, ModelFreedom{}, 0.5, 30);
		const Solution solution = refine(shared);
		CHECK(sameSolution(solution, reference));
		REQUIRE(solution.models.size() == 1);
		CHECK(sameParameters(solution.models[0], truth.parameters()));
	}
}

TEST_CASE("a free model that no camera sees through comes back as it started", "[camera][ceres][freemodel]")
{
	ensureCeres();
	const CameraModel truth = cameraWith(brownConrady());
	const MovingScene scene = movingScene(truth, 24, 120, 16, 0.0, 4);
	Problem problem = sharedProblem(scene, startOf(truth, 1.05), freeing(true, false, true), 1.0, 40);
	// A second model, free and seen through by no camera.
	const CameraModel unused = cameraWith(kannalaBrandt());
	problem.models.push_back(unused);
	problem.modelFreedom.push_back(freeing(true, true, true));

	const Solution solution = refine(problem);
	INFO(solution.detail);
	REQUIRE(solution.status == Status::Converged);
	REQUIRE(solution.models.size() == 2);
	CHECK(sameParameters(solution.models[1], unused.parameters()));
	// The one in use was refined: measured 1.2e-10 from the truth.
	CHECK(errorOf(created(solution, 0), truth).focalDifference < 1e-6);
}

TEST_CASE("a free model refines through a board's corners", "[camera][ceres][freemodel]")
{
	ensureCeres();
	// Board calibration as a bundle adjustment: one camera, held as the reference, seeing a board of
	// 9 x 6 corners 30 mm apart in twelve poses about 0.5 m away, tilted up to 0.4 rad and spread
	// over the image. The model starts with its focal lengths 5% long, its coefficients neutral and
	// its principal point held at the truth; each pose starts turned 10 mrad and moved 5 mm.
	const CameraModel truth = rig::model();
	std::vector<math::Vec3d> corners;
	for (int y = 0; y < 6; ++y)
	{
		for (int x = 0; x < 9; ++x)
			corners.push_back({0.03 * (x - 4), 0.03 * (y - 2.5), 0.0});
	}

	Random random(5);
	Problem problem;
	problem.models = {startOf(truth, 1.05)};
	problem.modelFreedom = {freeing(true, false, true)};
	problem.cameraFromReference = {math::RigidTransformd{}};
	problem.freeCameras = false;
	problem.noise.pixelSigma = 1.0;
	problem.loss.family = LossFamily::None;
	for (std::uint32_t pose = 0; pose < 12; ++pose)
	{
		const double a = kTau * double(pose) / 12.0;
		const math::Vec3d centre{0.13 * std::cos(a), 0.09 * std::sin(a), 0.5 + 0.05 * std::sin(2 * a)};
		const math::Quatd tilt = math::angleAxis(0.4, math::normalize(math::Vec3d{std::sin(a), -std::cos(a), 0.2}));
		const math::RigidTransformd referenceFromBoard{tilt, centre};
		for (const math::Vec3d& corner : corners)
		{
			const math::Vec3d q = referenceFromBoard.apply(corner);
			const Projection<double> pixel = project(truth, q.x, q.y, q.z);
			REQUIRE(inside(truth, pixel));
			problem.observations.push_back({0, pose, corner, {pixel.u, pixel.v}, std::nullopt});
		}
		problem.referenceFromBody.push_back(
			math::RigidTransformd{math::angleAxis(0.01, random.direction()), 0.005 * random.direction()} * referenceFromBoard);
	}

	const Solution solution = refine(problem);
	INFO(solution.detail);
	REQUIRE(solution.status == Status::Converged);
	CHECK_THAT(solution.detail, EndsWith("linear solver SPARSE_SCHUR"));
	// Measured: the focal lengths within 5.4e-9 and the ray mapping within 1.5e-6 px, in 7 iterations.
	const ModelComparison error = errorOf(created(solution, 0), truth);
	CAPTURE(error.focalDifference, error.worstPixels);
	CHECK(error.focalDifference < 1e-6);
	CHECK(error.worstPixels < 1e-4);
}

TEST_CASE("the refiner refuses a problem whose models do not match its cameras", "[camera][ceres][freemodel]")
{
	ensureCeres();
	const CameraModel truth = cameraWith(brownConrady());
	const MovingScene scene = movingScene(truth, 6, 20, 4, 0.0, 6);
	Problem problem = sharedProblem(scene, truth, ModelFreedom{}, 1.0, 60);
	REQUIRE(refine(problem).usable());

	std::string expected;
	SECTION("model indices of the wrong length")
	{
		problem.modelOf.pop_back();
		expected = "the problem's model indices do not match its cameras";
	}
	SECTION("a camera that sees through no model")
	{
		problem.modelOf[3] = 1;
		expected = "a camera sees through no model";
	}
	SECTION("freedoms of the wrong length")
	{
		problem.modelFreedom.push_back(ModelFreedom{});
		expected = "the problem's model freedoms do not match its models";
	}
	SECTION("no indices, and models and cameras of different counts")
	{
		problem.modelOf.clear();
		expected = "the problem's cameras do not match its models";
	}
	const Solution solution = refine(problem);
	CHECK(solution.status == Status::Failed);
	CHECK(solution.detail == expected);
}

TEST_CASE("the Ceres refiner can estimate every distortion model", "[camera][ceres][freemodel]")
{
	ensureCeres();
	for (int m = 0; m < kModelCount; ++m)
	{
		const DistortionModel model = modelOf(everyModel(m));
		INFO(displayName(model));
		CHECK(canEstimate(model));
	}
}

TEST_CASE("a free model's solution does not depend on where the heap put its block", "[camera][ceres][freemodel]")
{
	ensureCeres();
	// Two moving cameras' frames, each half seeing through a model of its own, both freed, through
	// 0.5 px of noise, so the solve's last bits are not an exact answer's. A free model's block is an
	// allocation of its own, so where it lands against the cameras' array, and against the other
	// model's, is the heap's business: here the heap is left in a different state before each solve,
	// on a thread of its own as a calibration's resamples are. Every solution must be the first's,
	// bit for bit; a resample's answer may not depend on which worker ran it.
	const CameraModel truth = cameraWith(brownConrady());
	const MovingScene scene = movingScene(truth, 12, 60, 8, 0.5, 11);
	Problem problem = sharedProblem(scene, startOf(truth, 1.05), freeing(true, false, true), 0.5, 110);
	problem.models.push_back(problem.models[0]);
	problem.modelFreedom.push_back(problem.modelFreedom[0]);
	for (std::size_t f = scene.cameraFromReference.size() / 2; f < scene.cameraFromReference.size(); ++f)
		problem.modelOf[f] = 1;

	const Solution first = refine(problem);
	INFO(first.detail);
	REQUIRE(first.status == Status::Converged);
	REQUIRE(first.models.size() == 2);

	for (std::size_t layout = 0; layout < 8; ++layout)
	{
		CAPTURE(layout);
		Solution solution;
		std::thread worker(
			[&]
			{
				// Blocks of every small size, every other one freed, so later allocations land in
				// holes whose order differs from one layout to the next.
				std::vector<std::vector<double>> spacers;
				for (std::size_t i = 0; i < 7 * layout + 3; ++i)
					spacers.emplace_back(1 + (5 * i + layout) % 20, 0.0);
				for (std::size_t i = layout % 2; i < spacers.size(); i += 2)
					std::vector<double>().swap(spacers[i]);
				solution = refine(problem);
			});
		worker.join();
		CHECK(sameSolution(solution, first));
		REQUIRE(solution.models.size() == 2);
		CHECK(sameParameters(solution.models[0], first.models[0]));
		CHECK(sameParameters(solution.models[1], first.models[1]));
	}
}

TEST_CASE("a moving camera of 60 frames and 20000 landmarks refines its shared model", "[camera][ceres][freemodel][scale]")
{
#ifndef NDEBUG
	SKIP("runs in Release builds, where it takes 6.4 s");
#endif
	ensureCeres();
	// Brown-Conrady 5, every landmark seen by 8 frames through 0.5 px of noise, the focal lengths and
	// coefficients free from the probe's start. What it pins is the cost; how well tracks this short
	// condition a model is sub-slices 9 and 10's to measure.
	const CameraModel truth = cameraWith(brownConrady());
	const MovingScene scene = movingScene(truth, 60, 20000, 8, 0.5, 7);
	const Problem problem = sharedProblem(scene, startOf(truth, 1.05), freeing(true, false, true), 0.5, 70);
	const Solution solution = refine(problem);
	INFO(solution.detail);
	REQUIRE(solution.status == Status::Converged);
	const ModelComparison error = errorOf(created(solution, 0), truth);
	CAPTURE(error.focalDifference, error.worstPixels, solution.iterations, solution.elapsed.seconds());
	// Measured on linux-x86_64, Release, one thread: 6.4 s over 160,000 residuals, 13 iterations, 162
	// MB at peak for the whole case. The final cost is 1.002 of half the residual dimensions less the
	// unknowns (every landmark, every frame but the reference, and the model's seven free
	// parameters), which is where whitened noise leaves a least-squares minimum. The focal lengths
	// came out 0.81% long and the worst ray 4.3 px out, which is the probe's finding: 8-view tracks
	// condition a model poorly under noise.
	const double dimensions = 2.0 * double(problem.landmarkObservations.size()) - 3.0 * double(problem.landmarks.size()) -
							  6.0 * double(problem.cameraFromReference.size() - 1) - 7.0;
	CHECK_THAT(solution.finalCost, WithinRel(0.5 * dimensions, 0.01));
	CHECK(error.focalDifference < 0.02);
}
