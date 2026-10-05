// One projection implementation (M9's review notes, ADR-0016): Ceres' automatic differentiation runs
// lain's own scalar-generic projection, camera::project<T>, rather than a copy of its formulas. This
// proves the claim the registration refiner will rest on, for every distortion model: the projection
// instantiates with ceres::Jet, and the derivatives it carries agree with finite differences of the
// double projection. The inverse Brown-Conrady model projects by Newton's method, so its derivatives
// come through an iteration, which is the case most worth checking.
//
// Then a small solve through Ceres itself, so the plugin is known to link and run the solver the
// refiner uses: Eigen's sparse Cholesky, one thread.

#include "testcamera.h"

#include <lain/camera/cameramodel.h>
#include <lain/camera/projection.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <ceres/autodiff_cost_function.h>
#include <ceres/jet.h>
#include <ceres/problem.h>
#include <ceres/solver.h>

#include <array>
#include <cmath>
#include <string>

using namespace lain::camera;
using Catch::Matchers::WithinAbs;

namespace
{
	using Jet = ceres::Jet<double, 3>;

	// d(u, v) / d(x, y, z) by central differences of the double projection.
	std::array<std::array<double, 3>, 2> numericJacobian(const CameraModel& model, double x, double y, double z)
	{
		std::array<std::array<double, 3>, 2> out{};
		const double point[3] = {x, y, z};
		for (int k = 0; k < 3; ++k)
		{
			const double h = 1e-6 * std::max(1.0, std::abs(point[k]));
			double plus[3] = {x, y, z}, minus[3] = {x, y, z};
			plus[k] += h;
			minus[k] -= h;
			const Projection<double> p = project(model, plus[0], plus[1], plus[2]);
			const Projection<double> m = project(model, minus[0], minus[1], minus[2]);
			REQUIRE(p.ok());
			REQUIRE(m.ok());
			out[0][std::size_t(k)] = (p.u - m.u) / (2 * h);
			out[1][std::size_t(k)] = (p.v - m.v) / (2 * h);
		}
		return out;
	}

	// The camera-frame point that projects to pixel (u, v) at depth `depth`, through lain's own
	// unprojection, so the test covers the whole image rather than only near the axis.
	std::array<double, 3> pointAt(const CameraModel& model, double u, double v, double depth)
	{
		const Unprojection<double> ray = unproject(model, u, v);
		REQUIRE(ray.ok());
		return {ray.x / ray.z * depth, ray.y / ray.z * depth, depth};
	}
} // namespace

TEST_CASE("camera::project differentiates through ceres::Jet for every distortion model", "[ceres]")
{
	const int index = GENERATE(range(0, testing::kModelCount));
	const CameraModel model = testing::cameraWith(testing::everyModel(index));
	CAPTURE(std::string(displayName(modelOf(model.distortion()))));

	// The centre, a point half way out, and one near a corner, at three depths.
	const std::array<std::array<double, 2>, 3> pixels = {{{320.5, 239.5}, {450.0, 120.0}, {40.0, 440.0}}};
	for (const auto& pixel : pixels)
	{
		for (const double depth : {0.4, 1.7, 6.0})
		{
			const std::array<double, 3> p = pointAt(model, pixel[0], pixel[1], depth);
			CAPTURE(pixel[0], pixel[1], depth);

			const Projection<double> plain = project(model, p[0], p[1], p[2]);
			const Projection<Jet> jet = project(model, Jet(p[0], 0), Jet(p[1], 1), Jet(p[2], 2));
			REQUIRE(plain.ok());
			REQUIRE(jet.ok());
			// The Jet carries the same value...
			CHECK_THAT(jet.u.a, WithinAbs(plain.u, 1e-9));
			CHECK_THAT(jet.v.a, WithinAbs(plain.v, 1e-9));
			// ...and derivatives agreeing with finite differences: within 1e-5 of the derivative's
			// scale, which is about f / z, and well above the differences' own truncation error.
			const std::array<std::array<double, 3>, 2> numeric = numericJacobian(model, p[0], p[1], p[2]);
			const double scale = 510.0 / depth;
			for (int k = 0; k < 3; ++k)
			{
				CHECK_THAT(jet.u.v[k], WithinAbs(numeric[0][std::size_t(k)], 1e-5 * scale));
				CHECK_THAT(jet.v.v[k], WithinAbs(numeric[1][std::size_t(k)], 1e-5 * scale));
			}
		}
	}
}

namespace
{
	// The pixel residual of a point seen by a camera translated by `offset`, for the small solve.
	struct PointResidual
	{
		const CameraModel* model;
		double offset[3];
		double u, v;

		template <typename T>
		bool operator()(const T* point, T* residual) const
		{
			const Projection<T> p =
				project(*model, point[0] - T(offset[0]), point[1] - T(offset[1]), point[2] - T(offset[2]));
			if (!p.ok())
				return false;
			residual[0] = p.u - T(u);
			residual[1] = p.v - T(v);
			return true;
		}
	};
} // namespace

TEST_CASE("a Ceres solve through camera::project recovers a point", "[ceres]")
{
	// Three translated copies of a distorted camera see one point; Ceres finds the point from their
	// pixels, starting a long way off, through the production projection and Eigen's sparse solver.
	const CameraModel model = testing::cameraWith(testing::brownConrady());
	const double truth[3] = {0.12, -0.08, 2.3};
	const double offsets[3][3] = {{0, 0, 0}, {0.3, 0, 0}, {0, 0.25, 0.1}};

	double point[3] = {0.0, 0.0, 1.0};
	ceres::Problem problem;
	for (const auto& offset : offsets)
	{
		const Projection<double> seen = project(model, truth[0] - offset[0], truth[1] - offset[1], truth[2] - offset[2]);
		REQUIRE(seen.ok());
		auto* cost = new ceres::AutoDiffCostFunction<PointResidual, 2, 3>(
			new PointResidual{&model, {offset[0], offset[1], offset[2]}, seen.u, seen.v});
		problem.AddResidualBlock(cost, nullptr, point);
	}

	ceres::Solver::Options options;
	options.linear_solver_type = ceres::SPARSE_NORMAL_CHOLESKY;
	options.sparse_linear_algebra_library_type = ceres::EIGEN_SPARSE;
	options.num_threads = 1;
	options.logging_type = ceres::SILENT;
	std::string why;
	REQUIRE(options.IsValid(&why));
	ceres::Solver::Summary summary;
	ceres::Solve(options, &problem, &summary);

	CAPTURE(summary.BriefReport());
	REQUIRE(summary.IsSolutionUsable());
	for (int k = 0; k < 3; ++k)
		CHECK_THAT(point[k], WithinAbs(truth[k], 1e-9));
}
