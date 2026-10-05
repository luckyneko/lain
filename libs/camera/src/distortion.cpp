#include "lain/camera/distortion.h"

#include <limits>

namespace lain::camera
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static constexpr double kInfinity = std::numeric_limits<double>::infinity();
	static constexpr double kHalfPi = 1.57079632679489661923;

	// Where a radius scan stops: no fold before it means none that matters.
	static constexpr double kRadiusCap = 1e3;

	// The first s > 0 where `increasing(s)` stops holding, to 1e-12, or `cap` when it holds up to
	// there. Scanned rather than solved, because the rational model's condition is no neat
	// polynomial: finely to s = 4 (which covers any lens a pinhole model is used for), then in 0.1%
	// steps to the cap.
	template <typename Increasing>
	static double foldOf(const Increasing& increasing, double cap)
	{
		double last = 0;
		double s = 1e-3;
		while (s < cap)
		{
			if (!increasing(s))
			{
				double lo = last, hi = s;
				while (hi - lo > 1e-12)
				{
					const double mid = 0.5 * (lo + hi);
					(increasing(mid) ? lo : hi) = mid;
				}
				return lo;
			}
			last = s;
			s = s < 4 ? s + 1e-3 : s * 1.001;
		}
		return cap;
	}

	// The domain of a mapping on a radius, g(r) = value(r): unbounded when it never folds.
	template <typename Value, typename Increasing>
	static DistortionDomain radialDomain(const Value& value, const Increasing& increasing)
	{
		const double fold = foldOf(increasing, kRadiusCap);
		if (fold >= kRadiusCap)
			return {kInfinity, kInfinity};
		return {fold, value(fold)};
	}

	// The radial part the three five-coefficient Brown-Conrady models share, g(r) = r * radial.
	static DistortionDomain brownConradyDomain(double k1, double k2, double k3)
	{
		return radialDomain(
			[=](double r)
			{
				const double s = r * r;
				return r * (1 + s * (k1 + s * (k2 + s * k3)));
			},
			[=](double r)
			{
				const double s = r * r;
				return 1 + s * (3 * k1 + s * (5 * k2 + s * 7 * k3)) > 0;
			});
	}

	// --- each model's domain --------------------------------------------------------

	DistortionDomain NoDistortion::domain() const
	{
		return {kInfinity, kInfinity}; // the identity never folds
	}

	DistortionDomain BrownConrady5::domain() const
	{
		return brownConradyDomain(k1, k2, k3);
	}

	DistortionDomain InverseBrownConrady5::domain() const
	{
		return brownConradyDomain(k1, k2, k3); // on the DISTORTED radius, this model's argument
	}

	DistortionDomain ModifiedBrownConrady5::domain() const
	{
		return brownConradyDomain(k1, k2, k3);
	}

	DistortionDomain RationalBrownConrady8::domain() const
	{
		return radialDomain(
			[this](double r)
			{
				const double s = r * r;
				return r * (1 + s * (k1 + s * (k2 + s * k3))) / (1 + s * (k4 + s * (k5 + s * k6)));
			},
			[this](double r)
			{
				const double s = r * r;
				const double n = 1 + s * (k1 + s * (k2 + s * k3));
				const double m = 1 + s * (k4 + s * (k5 + s * k6));
				if (m <= 0)
					return false; // the denominator's zero ends the domain too
				const double dn = 2 * r * (k1 + s * (2 * k2 + s * 3 * k3));
				const double dm = 2 * r * (k4 + s * (2 * k5 + s * 3 * k6));
				return n / m + r * (dn * m - n * dm) / (m * m) > 0;
			});
	}

	DistortionDomain KannalaBrandt4::domain() const
	{
		// An angle, not a radius: it stops at the side of the camera, so this domain is never
		// unbounded and never more than pi/2.
		const double fold = foldOf([this](double theta)
								   { return distortedAngleSlope(theta) > 0; }, kHalfPi);
		return {fold, distortedAngle(fold)};
	}

	// --- free functions ---------------------------------------------------------

	Distortion neutral(DistortionModel model)
	{
		switch (model)
		{
			case DistortionModel::None:
				return NoDistortion{};
			case DistortionModel::BrownConrady5:
				return BrownConrady5{};
			case DistortionModel::InverseBrownConrady5:
				return InverseBrownConrady5{};
			case DistortionModel::ModifiedBrownConrady5:
				return ModifiedBrownConrady5{};
			case DistortionModel::RationalBrownConrady8:
				return RationalBrownConrady8{};
			case DistortionModel::KannalaBrandt4:
				return KannalaBrandt4{};
		}
		return NoDistortion{};
	}

	std::string_view displayName(const Distortion& distortion)
	{
		return std::visit([](const auto& d)
						  { return d.kDisplayName; }, distortion);
	}

	std::string_view displayName(DistortionModel model)
	{
		return displayName(neutral(model));
	}

	std::vector<std::string_view> coefficientNames(DistortionModel model)
	{
		return std::visit([](const auto& d)
						  { return std::vector<std::string_view>(d.kCoefficientNames.begin(), d.kCoefficientNames.end()); },
						  neutral(model));
	}

	std::vector<double> coefficients(const Distortion& distortion)
	{
		return std::visit(
			[](const auto& d)
			{
				const auto values = d.coefficients();
				return std::vector<double>(values.begin(), values.end());
			},
			distortion);
	}
} // namespace lain::camera
