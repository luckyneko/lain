#include "lain/camera/cameramodel.h"

#include "lain/camera/projection.h"

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace lain::camera
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static constexpr double kInfinity = std::numeric_limits<double>::infinity();
	static constexpr double kHalfPi = 1.57079632679489661923;

	// Every coefficient of a distortion model, by name, for the finiteness check and its message.
	static std::vector<std::pair<const char*, double>> coefficients(const Distortion& distortion)
	{
		return std::visit(
			[](const auto& d) -> std::vector<std::pair<const char*, double>>
			{
				using D = std::decay_t<decltype(d)>;
				if constexpr (std::is_same_v<D, NoDistortion>)
				{
					(void)d;
					return {};
				}
				else if constexpr (std::is_same_v<D, RationalBrownConrady8>)
					return {{"k1", d.k1}, {"k2", d.k2}, {"p1", d.p1}, {"p2", d.p2}, {"k3", d.k3}, {"k4", d.k4}, {"k5", d.k5}, {"k6", d.k6}};
				else if constexpr (std::is_same_v<D, KannalaBrandt4>)
					return {{"k1", d.k1}, {"k2", d.k2}, {"k3", d.k3}, {"k4", d.k4}};
				else // the three five-coefficient Brown-Conrady models
					return {{"k1", d.k1}, {"k2", d.k2}, {"p1", d.p1}, {"p2", d.p2}, {"k3", d.k3}};
			},
			distortion);
	}

	// A model's radial mapping g(s) (distortion.h: radius for Brown-Conrady and rational, angle for
	// Kannala-Brandt) and whether it is still defined and increasing at s.
	struct RadialMapping
	{
		std::function<double(double)> value;
		std::function<bool(double)> increasing;
		double cap; // where the scan stops: no fold before it means none that matters
	};

	static RadialMapping brownConradyMapping(double k1, double k2, double k3)
	{
		return {[=](double r)
				{
					const double s = r * r;
					return r * (1 + s * (k1 + s * (k2 + s * k3)));
				},
				[=](double r)
				{
					const double s = r * r;
					return 1 + s * (3 * k1 + s * (5 * k2 + s * 7 * k3)) > 0;
				},
				1e3};
	}

	static RadialMapping mappingOf(const Distortion& distortion)
	{
		return std::visit(
			[](const auto& d) -> RadialMapping
			{
				using D = std::decay_t<decltype(d)>;
				if constexpr (std::is_same_v<D, NoDistortion>)
				{
					(void)d;
					return {[](double r)
							{ return r; }, [](double)
							{ return true; }, 1e3};
				}
				else if constexpr (std::is_same_v<D, RationalBrownConrady8>)
				{
					return {[=](double r)
							{
								const double s = r * r;
								return r * (1 + s * (d.k1 + s * (d.k2 + s * d.k3))) /
									   (1 + s * (d.k4 + s * (d.k5 + s * d.k6)));
							},
							[=](double r)
							{
								const double s = r * r;
								const double n = 1 + s * (d.k1 + s * (d.k2 + s * d.k3));
								const double m = 1 + s * (d.k4 + s * (d.k5 + s * d.k6));
								if (m <= 0)
									return false;
								const double dn = 2 * r * (d.k1 + s * (2 * d.k2 + s * 3 * d.k3));
								const double dm = 2 * r * (d.k4 + s * (2 * d.k5 + s * 3 * d.k6));
								return n / m + r * (dn * m - n * dm) / (m * m) > 0;
							},
							1e3};
				}
				else if constexpr (std::is_same_v<D, KannalaBrandt4>)
				{
					return {[=](double theta)
							{ return detail::kannalaBrandt(d, theta); },
							[=](double theta)
							{ return detail::kannalaBrandtSlope(d, theta) > 0; }, kHalfPi};
				}
				else // the three five-coefficient Brown-Conrady models share one radial part
					return brownConradyMapping(d.k1, d.k2, d.k3);
			},
			distortion);
	}

	// The first s > 0 where the mapping stops increasing, to 1e-12, or the cap when it never does
	// before it. Scanned rather than solved, because the rational model's condition is no neat
	// polynomial: finely to s = 4 (which covers any lens a pinhole model is used for), then in 0.1%
	// steps to the cap.
	static double foldOf(const RadialMapping& mapping)
	{
		double last = 0;
		double s = 1e-3;
		while (s < mapping.cap)
		{
			if (!mapping.increasing(s))
			{
				double lo = last, hi = s;
				while (hi - lo > 1e-12)
				{
					const double mid = 0.5 * (lo + hi);
					(mapping.increasing(mid) ? lo : hi) = mid;
				}
				return lo;
			}
			last = s;
			s = s < 4 ? s + 1e-3 : s * 1.001;
		}
		return mapping.cap;
	}

	static DistortionDomain domainOf(const Distortion& distortion)
	{
		const RadialMapping mapping = mappingOf(distortion);
		const double fold = foldOf(mapping);
		const bool angular = std::holds_alternative<KannalaBrandt4>(distortion);
		// A radius that never folds is unbounded; an angle stops at the side of the camera.
		if (!angular && fold >= mapping.cap)
			return {kInfinity, kInfinity};
		return {fold, mapping.value(fold)};
	}

	static std::string describe(double value)
	{
		return std::to_string(value);
	}

	// --- CameraModel ------------------------------------------------------------

	ModelResult CameraModel::create(const CameraModelParameters& parameters)
	{
		ModelResult result;
		const auto problem = [&result](ModelProblem kind, std::string detail)
		{ result.diagnostics.push_back({kind, std::move(detail)}); };

		const ImageGeometry& image = parameters.image;
		const Intrinsics& k = parameters.intrinsics;
		if (image.width == 0 || image.height == 0)
			problem(ModelProblem::EmptyImage,
					"image is " + std::to_string(image.width) + "x" + std::to_string(image.height));

		const std::pair<const char*, double> pinhole[] = {{"fx", k.fx}, {"fy", k.fy}, {"cx", k.cx}, {"cy", k.cy}};
		for (const auto& [name, value] : pinhole)
		{
			if (!std::isfinite(value))
				problem(ModelProblem::NonFiniteParameter, std::string(name) + " is " + describe(value));
		}
		for (const auto& [name, value] : coefficients(parameters.distortion))
		{
			if (!std::isfinite(value))
				problem(ModelProblem::NonFiniteParameter, std::string(name) + " is " + describe(value));
		}
		for (const auto& [name, value] : {std::pair<const char*, double>{"fx", k.fx}, {"fy", k.fy}})
		{
			if (std::isfinite(value) && value <= 0)
				problem(ModelProblem::NonPositiveFocalLength, std::string(name) + " is " + describe(value));
		}
		// The fold test needs finite numbers and an image to have corners, so it runs only on
		// parameters that passed everything above.
		if (!result.diagnostics.empty())
			return result;

		CameraModel model{parameters, domainOf(parameters.distortion)};

		// Every corner of the image must have a ray: a distortion that folds inside the image leaves
		// part of it with none, or with two, and no downstream use of the model survives that.
		const double right = double(image.width) - 0.5;
		const double bottom = double(image.height) - 0.5;
		const std::pair<double, double> corners[] = {{-0.5, -0.5}, {right, -0.5}, {-0.5, bottom}, {right, bottom}};
		for (const auto& [u, v] : corners)
		{
			if (!unproject(model, u, v).ok())
				problem(ModelProblem::FoldsInsideImage,
						"the distortion has no ray at image corner (" + describe(u) + ", " + describe(v) + ")");
		}
		if (!result.diagnostics.empty())
			return result;

		result.model = std::move(model);
		return result;
	}

	// --- free functions ---------------------------------------------------------

	std::string_view displayName(const Distortion& distortion)
	{
		return std::visit(
			[](const auto& d) -> std::string_view
			{
				using D = std::decay_t<decltype(d)>;
				(void)d;
				if constexpr (std::is_same_v<D, NoDistortion>)
					return "no distortion";
				else if constexpr (std::is_same_v<D, BrownConrady5>)
					return "Brown-Conrady 5";
				else if constexpr (std::is_same_v<D, InverseBrownConrady5>)
					return "Inverse Brown-Conrady 5";
				else if constexpr (std::is_same_v<D, ModifiedBrownConrady5>)
					return "Modified Brown-Conrady 5";
				else if constexpr (std::is_same_v<D, RationalBrownConrady8>)
					return "Rational Brown-Conrady 8";
				else
					return "Kannala-Brandt 4";
			},
			distortion);
	}

	Applicability applicability(const CameraModel& model, const ImageGeometry& footage)
	{
		return model.image() == footage ? Applicability::Unknown : Applicability::Incompatible;
	}

	bool contains(const CameraModel& model, double u, double v)
	{
		const ImageGeometry& image = model.image();
		return u >= -0.5 && v >= -0.5 && u < double(image.width) - 0.5 && v < double(image.height) - 0.5;
	}
} // namespace lain::camera
