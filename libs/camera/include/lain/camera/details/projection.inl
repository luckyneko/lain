#pragma once

// The projection kernels (projection.h). Scalar-generic: every function takes its point as T and
// converts each double coefficient to T before using it, so a float or an automatic-differentiation
// scalar runs exactly this code. Math functions are called unqualified after `using std::...`, so a
// scalar type's own sqrt / atan2 is found by argument-dependent lookup.

#include <cmath>
#include <type_traits>
#include <variant>

namespace lain::camera::detail
{
	// How close an iterative inverse must come, and the step its finite-difference Jacobian takes, in
	// normalised units. Anything that is not float is assumed to carry double precision.
	template <typename T>
	struct Convergence
	{
		static constexpr double tolerance = 1e-12;
		static constexpr double step = 1e-7;
	};
	template <>
	struct Convergence<float>
	{
		static constexpr double tolerance = 1e-5;
		static constexpr double step = 1e-3;
	};

	constexpr int kMaxIterations = 50;

	// The Brown-Conrady polynomial (distortion.h), which maps ideal to distorted for the forward model
	// and distorted to ideal for the inverse one.
	template <typename T>
	void brownConrady(double k1, double k2, double p1, double p2, double k3, const T& x, const T& y, T& outX, T& outY)
	{
		const T r2 = x * x + y * y;
		const T radial = T(1) + r2 * (T(k1) + r2 * (T(k2) + r2 * T(k3)));
		outX = x * radial + T(2 * p1) * x * y + T(p2) * (r2 + T(2) * x * x);
		outY = y * radial + T(p1) * (r2 + T(2) * y * y) + T(2 * p2) * x * y;
	}

	template <typename T>
	void distort(const BrownConrady5& d, const T& x, const T& y, T& xd, T& yd)
	{
		brownConrady(d.k1, d.k2, d.p1, d.p2, d.k3, x, y, xd, yd);
	}

	template <typename T>
	void distort(const ModifiedBrownConrady5& d, const T& x, const T& y, T& xd, T& yd)
	{
		const T r2 = x * x + y * y;
		const T radial = T(1) + r2 * (T(d.k1) + r2 * (T(d.k2) + r2 * T(d.k3)));
		const T xr = x * radial;
		const T yr = y * radial;
		xd = xr + T(2 * d.p1) * xr * yr + T(d.p2) * (r2 + T(2) * xr * xr);
		yd = yr + T(2 * d.p2) * xr * yr + T(d.p1) * (r2 + T(2) * yr * yr);
	}

	template <typename T>
	void distort(const RationalBrownConrady8& d, const T& x, const T& y, T& xd, T& yd)
	{
		const T r2 = x * x + y * y;
		const T numerator = T(1) + r2 * (T(d.k1) + r2 * (T(d.k2) + r2 * T(d.k3)));
		const T denominator = T(1) + r2 * (T(d.k4) + r2 * (T(d.k5) + r2 * T(d.k6)));
		const T radial = numerator / denominator;
		xd = x * radial + T(2 * d.p1) * x * y + T(d.p2) * (r2 + T(2) * x * x);
		yd = y * radial + T(d.p1) * (r2 + T(2) * y * y) + T(2 * d.p2) * x * y;
	}

	// Kannala-Brandt's distorted angle for an angle from the axis, and its derivative.
	template <typename T>
	T kannalaBrandt(const KannalaBrandt4& d, const T& theta)
	{
		const T t2 = theta * theta;
		return theta * (T(1) + t2 * (T(d.k1) + t2 * (T(d.k2) + t2 * (T(d.k3) + t2 * T(d.k4)))));
	}
	template <typename T>
	T kannalaBrandtSlope(const KannalaBrandt4& d, const T& theta)
	{
		const T t2 = theta * theta;
		return T(1) + t2 * (T(3 * d.k1) + t2 * (T(5 * d.k2) + t2 * (T(7 * d.k3) + t2 * T(9 * d.k4))));
	}

	// Newton's method for (x, y) with f(x, y) == (targetX, targetY), starting from (x, y). The Jacobian
	// is a central finite difference: the residual is computed exactly, so the answer is exact to the
	// tolerance even though the steps are approximate.
	template <typename T, typename F>
	ProjectionStatus solve(const F& f, const T& targetX, const T& targetY, T& x, T& y)
	{
		using std::abs;
		const T tolerance = T(Convergence<T>::tolerance);
		const T h = T(Convergence<T>::step);
		for (int iteration = 0;; ++iteration)
		{
			T fx = T(0), fy = T(0);
			f(x, y, fx, fy);
			const T ex = fx - targetX;
			const T ey = fy - targetY;
			if (abs(ex) + abs(ey) <= tolerance)
				return ProjectionStatus::Ok;
			if (iteration == kMaxIterations)
				return ProjectionStatus::NotConverged;

			T xPlusX = T(0), xPlusY = T(0), xMinusX = T(0), xMinusY = T(0);
			T yPlusX = T(0), yPlusY = T(0), yMinusX = T(0), yMinusY = T(0);
			f(x + h, y, xPlusX, xPlusY);
			f(x - h, y, xMinusX, xMinusY);
			f(x, y + h, yPlusX, yPlusY);
			f(x, y - h, yMinusX, yMinusY);
			const T a = (xPlusX - xMinusX) / (T(2) * h); // d fx / dx
			const T c = (xPlusY - xMinusY) / (T(2) * h); // d fy / dx
			const T b = (yPlusX - yMinusX) / (T(2) * h); // d fx / dy
			const T d = (yPlusY - yMinusY) / (T(2) * h); // d fy / dy
			const T determinant = a * d - b * c;
			if (abs(determinant) <= tolerance)
				return ProjectionStatus::NotConverged; // at a fold: no local inverse
			x -= (d * ex - b * ey) / determinant;
			y -= (a * ey - c * ex) / determinant;
		}
	}

	// The unit ray through ideal normalised coordinates (x, y).
	template <typename T>
	Unprojection<T> ray(const T& x, const T& y)
	{
		using std::sqrt;
		const T length = sqrt(x * x + y * y + T(1));
		Unprojection<T> out;
		out.x = x / length;
		out.y = y / length;
		out.z = T(1) / length;
		return out;
	}

	template <typename T>
	Unprojection<T> failedRay(ProjectionStatus status)
	{
		Unprojection<T> out;
		out.status = status;
		return out;
	}

	// The models whose distortion is a forward polynomial on normalised coordinates.
	template <typename D>
	constexpr bool isForwardPolynomial = std::is_same_v<D, BrownConrady5> || std::is_same_v<D, ModifiedBrownConrady5> ||
										 std::is_same_v<D, RationalBrownConrady8>;
} // namespace lain::camera::detail

namespace lain::camera
{
	template <typename T>
	Projection<T> project(const CameraModel& model, T x, T y, T z)
	{
		using std::atan2;
		using std::sqrt;

		Projection<T> out;
		if (!(z > T(0))) // written negated so a NaN is refused too
		{
			out.status = ProjectionStatus::BehindCamera;
			return out;
		}
		const DistortionDomain& domain = model.domain();
		const T limit = T(domain.limit);
		const T limitImage = T(domain.limitImage);
		T xd = T(0), yd = T(0);

		const ProjectionStatus status = std::visit(
			[&](const auto& d) -> ProjectionStatus
			{
				using D = std::decay_t<decltype(d)>;
				if constexpr (std::is_same_v<D, NoDistortion>)
				{
					xd = x / z;
					yd = y / z;
					return ProjectionStatus::Ok;
				}
				else if constexpr (detail::isForwardPolynomial<D>)
				{
					const T xn = x / z;
					const T yn = y / z;
					if (!(xn * xn + yn * yn < limit * limit))
						return ProjectionStatus::OutsideDomain;
					detail::distort(d, xn, yn, xd, yd);
					return ProjectionStatus::Ok;
				}
				else if constexpr (std::is_same_v<D, InverseBrownConrady5>)
				{
					const T xn = x / z;
					const T yn = y / z;
					if (!(xn * xn + yn * yn < limitImage * limitImage))
						return ProjectionStatus::OutsideDomain;
					xd = xn;
					yd = yn;
					const auto toIdeal = [&d](const T& px, const T& py, T& ox, T& oy)
					{ detail::brownConrady(d.k1, d.k2, d.p1, d.p2, d.k3, px, py, ox, oy); };
					const ProjectionStatus solved = detail::solve(toIdeal, xn, yn, xd, yd);
					if (solved != ProjectionStatus::Ok)
						return solved;
					return xd * xd + yd * yd < limit * limit ? ProjectionStatus::Ok : ProjectionStatus::OutsideDomain;
				}
				else // KannalaBrandt4
				{
					const T rho = sqrt(x * x + y * y);
					const T theta = atan2(rho, z);
					if (!(theta < limit))
						return ProjectionStatus::OutsideDomain;
					if (rho * T(1e12) <= z)
					{
						// On the axis, where thetad / rho has its limit 1 / z; dividing would lose
						// the derivative an automatic-differentiation scalar carries.
						xd = x / z;
						yd = y / z;
						return ProjectionStatus::Ok;
					}
					const T thetad = detail::kannalaBrandt(d, theta);
					xd = thetad * x / rho;
					yd = thetad * y / rho;
					return ProjectionStatus::Ok;
				}
			},
			model.distortion());

		if (status != ProjectionStatus::Ok)
		{
			out.status = status;
			return out;
		}
		const Intrinsics& k = model.intrinsics();
		out.u = T(k.fx) * xd + T(k.cx);
		out.v = T(k.fy) * yd + T(k.cy);
		return out;
	}

	template <typename T>
	Unprojection<T> unproject(const CameraModel& model, T u, T v)
	{
		using std::abs;
		using std::cos;
		using std::sin;
		using std::sqrt;

		const Intrinsics& k = model.intrinsics();
		const T xd = (u - T(k.cx)) / T(k.fx);
		const T yd = (v - T(k.cy)) / T(k.fy);
		const DistortionDomain& domain = model.domain();
		const T limit = T(domain.limit);
		const T limitImage = T(domain.limitImage);

		return std::visit(
			[&](const auto& d) -> Unprojection<T>
			{
				using D = std::decay_t<decltype(d)>;
				if constexpr (std::is_same_v<D, NoDistortion>)
				{
					return detail::ray(xd, yd);
				}
				else if constexpr (detail::isForwardPolynomial<D>)
				{
					if (!(xd * xd + yd * yd < limitImage * limitImage))
						return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
					T x = xd, y = yd;
					const auto toDistorted = [&d](const T& px, const T& py, T& ox, T& oy)
					{ detail::distort(d, px, py, ox, oy); };
					const ProjectionStatus solved = detail::solve(toDistorted, xd, yd, x, y);
					if (solved != ProjectionStatus::Ok)
						return detail::failedRay<T>(solved);
					if (!(x * x + y * y < limit * limit))
						return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
					return detail::ray(x, y);
				}
				else if constexpr (std::is_same_v<D, InverseBrownConrady5>)
				{
					if (!(xd * xd + yd * yd < limit * limit))
						return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
					T x = T(0), y = T(0);
					detail::brownConrady(d.k1, d.k2, d.p1, d.p2, d.k3, xd, yd, x, y);
					return detail::ray(x, y);
				}
				else // KannalaBrandt4
				{
					const T thetad = sqrt(xd * xd + yd * yd);
					if (!(thetad < limitImage))
						return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
					if (thetad <= T(1e-12))
						return detail::ray(xd, yd); // on the axis: theta ~ thetad ~ r
					// The distorted angle is increasing on the domain, so Newton from thetad settles.
					T theta = thetad;
					const T tolerance = T(detail::Convergence<T>::tolerance);
					for (int iteration = 0;; ++iteration)
					{
						const T error = detail::kannalaBrandt(d, theta) - thetad;
						if (abs(error) <= tolerance)
							break;
						if (iteration == detail::kMaxIterations)
							return detail::failedRay<T>(ProjectionStatus::NotConverged);
						theta -= error / detail::kannalaBrandtSlope(d, theta);
					}
					if (!(theta >= T(0) && theta < limit))
						return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
					const T s = sin(theta);
					Unprojection<T> out;
					out.x = s * xd / thetad;
					out.y = s * yd / thetad;
					out.z = cos(theta);
					return out;
				}
			},
			model.distortion());
	}
} // namespace lain::camera
