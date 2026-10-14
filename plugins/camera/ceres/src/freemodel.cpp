#include "freemodel.h"

#include <lain/camera/projection.h>

#include <ceres/autodiff_cost_function.h>

#include <array>
#include <type_traits>
#include <variant>

namespace lain::camera::ceres
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// A free model's projection: its parameter block, laid out [fx, fy, cx, cy, coefficients...] in
	// the parameter overload's order, through the STARTING model's domain (ADR-0017).
	template <typename Model, typename T>
	static auto freeProjection(const T* const parameters, const DistortionDomain& domain)
	{
		const std::array<T, 4> intrinsics = {parameters[0], parameters[1], parameters[2], parameters[3]};
		typename Model::template Coefficients<T> coefficients;
		for (std::size_t i = 0; i < coefficients.size(); ++i)
			coefficients[i] = parameters[4 + i];
		return [intrinsics, coefficients, &domain](const T& x, const T& y, const T& z)
		{ return project<Model>(intrinsics, coefficients, domain, x, y, z); };
	}

	// A corner, and a landmark, seen through a free model, whose parameters are a block of their own.
	template <typename Model>
	struct FreeCornerResidual
	{
		DistortionDomain domain;
		math::Vec3d point;
		math::Vec2d pixel;
		Whitening whitening;

		template <typename T>
		bool operator()(const T* const camera, const T* const body, const T* const model, T* residual) const
		{
			T inReference[3];
			bodyToReference(body, point, inReference);
			return observe(freeProjection<Model>(model, domain), camera, inReference, pixel, whitening, residual);
		}
	};

	template <typename Model>
	struct FreePointResidual
	{
		DistortionDomain domain;
		math::Vec2d pixel;
		Whitening whitening;

		template <typename T>
		bool operator()(const T* const camera, const T* const point, const T* const model, T* residual) const
		{
			return observe(freeProjection<Model>(model, domain), camera, point, pixel, whitening, residual);
		}
	};

	// A free model's parameter count: four intrinsics, then its coefficients.
	template <typename Model>
	constexpr int kFreeParameters = 4 + int(Model::kCoefficientNames.size());

	// --- freemodel.h ---------------------------------------------------------------

	::ceres::CostFunction* freeCornerCost(const CameraModel& start, const math::Vec3d& point,
										  const math::Vec2d& pixel, const Whitening& w)
	{
		return std::visit(
			[&](const auto& d) -> ::ceres::CostFunction*
			{
				using Model = std::decay_t<decltype(d)>;
				return new ::ceres::AutoDiffCostFunction<FreeCornerResidual<Model>, 2, 6, 6, kFreeParameters<Model>>(
					new FreeCornerResidual<Model>{start.domain(), point, pixel, w});
			},
			start.distortion());
	}

	::ceres::CostFunction* freePointCost(const CameraModel& start, const math::Vec2d& pixel, const Whitening& w)
	{
		return std::visit(
			[&](const auto& d) -> ::ceres::CostFunction*
			{
				using Model = std::decay_t<decltype(d)>;
				return new ::ceres::AutoDiffCostFunction<FreePointResidual<Model>, 2, 6, 3, kFreeParameters<Model>>(
					new FreePointResidual<Model>{start.domain(), pixel, w});
			},
			start.distortion());
	}

	std::vector<double> blockOf(const CameraModel& model)
	{
		const Intrinsics& k = model.intrinsics();
		std::vector<double> out = {k.fx, k.fy, k.cx, k.cy};
		for (const double c : coefficients(model.distortion()))
			out.push_back(c);
		return out;
	}

	CameraModelParameters parametersOf(const CameraModel& start, const std::vector<double>& block)
	{
		CameraModelParameters out = start.parameters();
		out.intrinsics = {block[0], block[1], block[2], block[3]};
		out.distortion = std::visit(
			[&](const auto& d) -> Distortion
			{
				using Model = std::decay_t<decltype(d)>;
				typename Model::template Coefficients<double> c{};
				for (std::size_t i = 0; i < c.size(); ++i)
					c[i] = block[4 + i];
				return Model::fromCoefficients(c);
			},
			start.distortion());
		return out;
	}

	std::vector<int> heldFields(const refinement::ModelFreedom& freedom, std::size_t blockSize)
	{
		std::vector<int> out;
		if (!freedom.focalLength)
			out.insert(out.end(), {0, 1});
		if (!freedom.principalPoint)
			out.insert(out.end(), {2, 3});
		if (!freedom.coefficients)
		{
			for (std::size_t i = 4; i < blockSize; ++i)
				out.push_back(int(i));
		}
		return out;
	}
} // namespace lain::camera::ceres
