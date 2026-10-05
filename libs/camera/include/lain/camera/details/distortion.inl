#pragma once

// The distortion models' template bodies (distortion.h). Scalar-generic: every function takes its
// point as T and converts each double coefficient to T before using it, so a float or an
// automatic-differentiation scalar runs exactly this code. Math functions are called unqualified
// after `using std::...`, so a scalar type's own sqrt / atan2 is found by argument-dependent lookup.
//
// Even a one-line member body is here rather than in its class: the members call the detail
// helpers below, and a qualified call in a template is looked up where the template is defined, so
// a body that names one has to follow it.

#include <cmath>

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

	template <typename T>
	DistortedPoint<T> failedPoint(ProjectionStatus status)
	{
		DistortedPoint<T> out;
		out.status = status;
		return out;
	}

	template <typename T>
	DistortedPoint<T> point(const T& x, const T& y)
	{
		DistortedPoint<T> out;
		out.x = x;
		out.y = y;
		return out;
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

	// Whether a camera-frame point is in front of the camera. Written negated by its callers'
	// `if (!inFront(z))`, so a NaN is refused too.
	template <typename T>
	bool inFront(const T& z)
	{
		return z > T(0);
	}

	// A FORWARD model's projection and unprojection, for a model whose distort() maps ideal to
	// distorted normalised coordinates: closed form one way, Newton's method the other.
	template <typename T, typename Model>
	DistortedPoint<T> projectForward(const Model& model, const T& x, const T& y, const T& z, const DistortionDomain& domain)
	{
		if (!inFront(z))
			return failedPoint<T>(ProjectionStatus::BehindCamera);
		const T xn = x / z;
		const T yn = y / z;
		const T limit = T(domain.limit);
		if (!(xn * xn + yn * yn < limit * limit))
			return failedPoint<T>(ProjectionStatus::OutsideDomain);
		DistortedPoint<T> out;
		model.distort(xn, yn, out.x, out.y);
		return out;
	}

	template <typename T, typename Model>
	Unprojection<T> unprojectForward(const Model& model, const T& xd, const T& yd, const DistortionDomain& domain)
	{
		const T limit = T(domain.limit);
		const T limitImage = T(domain.limitImage);
		if (!(xd * xd + yd * yd < limitImage * limitImage))
			return failedRay<T>(ProjectionStatus::OutsideDomain);
		T x = xd, y = yd;
		const auto toDistorted = [&model](const T& px, const T& py, T& ox, T& oy)
		{ model.distort(px, py, ox, oy); };
		const ProjectionStatus solved = solve(toDistorted, xd, yd, x, y);
		if (solved != ProjectionStatus::Ok)
			return failedRay<T>(solved);
		if (!(x * x + y * y < limit * limit))
			return failedRay<T>(ProjectionStatus::OutsideDomain);
		return ray(x, y);
	}
} // namespace lain::camera::detail

namespace lain::camera
{
	// --- NoDistortion -------------------------------------------------------------

	template <typename T>
	DistortedPoint<T> NoDistortion::project(const T& x, const T& y, const T& z, const DistortionDomain&) const
	{
		if (!detail::inFront(z))
			return detail::failedPoint<T>(ProjectionStatus::BehindCamera);
		return detail::point<T>(x / z, y / z);
	}

	template <typename T>
	Unprojection<T> NoDistortion::unproject(const T& xd, const T& yd, const DistortionDomain&) const
	{
		return detail::ray(xd, yd);
	}

	// --- BrownConrady5 ------------------------------------------------------------

	template <typename T>
	DistortedPoint<T> BrownConrady5::project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const
	{
		return detail::projectForward(*this, x, y, z, domain);
	}

	template <typename T>
	Unprojection<T> BrownConrady5::unproject(const T& xd, const T& yd, const DistortionDomain& domain) const
	{
		return detail::unprojectForward(*this, xd, yd, domain);
	}

	template <typename T>
	void BrownConrady5::distort(const T& x, const T& y, T& xd, T& yd) const
	{
		detail::brownConrady(k1, k2, p1, p2, k3, x, y, xd, yd);
	}

	// --- InverseBrownConrady5 -----------------------------------------------------

	// The polynomial runs distorted to ideal, so projection is the iterative side.
	template <typename T>
	DistortedPoint<T> InverseBrownConrady5::project(const T& x, const T& y, const T& z,
													const DistortionDomain& domain) const
	{
		if (!detail::inFront(z))
			return detail::failedPoint<T>(ProjectionStatus::BehindCamera);
		const T xn = x / z;
		const T yn = y / z;
		const T limit = T(domain.limit);
		const T limitImage = T(domain.limitImage);
		if (!(xn * xn + yn * yn < limitImage * limitImage))
			return detail::failedPoint<T>(ProjectionStatus::OutsideDomain);
		T xd = xn, yd = yn;
		const auto toIdeal = [this](const T& px, const T& py, T& ox, T& oy)
		{ undistort(px, py, ox, oy); };
		const ProjectionStatus solved = detail::solve(toIdeal, xn, yn, xd, yd);
		if (solved != ProjectionStatus::Ok)
			return detail::failedPoint<T>(solved);
		if (!(xd * xd + yd * yd < limit * limit))
			return detail::failedPoint<T>(ProjectionStatus::OutsideDomain);
		return detail::point(xd, yd);
	}

	template <typename T>
	Unprojection<T> InverseBrownConrady5::unproject(const T& xd, const T& yd, const DistortionDomain& domain) const
	{
		const T limit = T(domain.limit);
		if (!(xd * xd + yd * yd < limit * limit))
			return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
		T x = T(0), y = T(0);
		undistort(xd, yd, x, y);
		return detail::ray(x, y);
	}

	template <typename T>
	void InverseBrownConrady5::undistort(const T& xd, const T& yd, T& x, T& y) const
	{
		detail::brownConrady(k1, k2, p1, p2, k3, xd, yd, x, y);
	}

	// --- ModifiedBrownConrady5 ----------------------------------------------------

	template <typename T>
	DistortedPoint<T> ModifiedBrownConrady5::project(const T& x, const T& y, const T& z,
													 const DistortionDomain& domain) const
	{
		return detail::projectForward(*this, x, y, z, domain);
	}

	template <typename T>
	Unprojection<T> ModifiedBrownConrady5::unproject(const T& xd, const T& yd, const DistortionDomain& domain) const
	{
		return detail::unprojectForward(*this, xd, yd, domain);
	}

	template <typename T>
	void ModifiedBrownConrady5::distort(const T& x, const T& y, T& xd, T& yd) const
	{
		const T r2 = x * x + y * y;
		const T radial = T(1) + r2 * (T(k1) + r2 * (T(k2) + r2 * T(k3)));
		const T xr = x * radial;
		const T yr = y * radial;
		xd = xr + T(2 * p1) * xr * yr + T(p2) * (r2 + T(2) * xr * xr);
		yd = yr + T(2 * p2) * xr * yr + T(p1) * (r2 + T(2) * yr * yr);
	}

	// --- RationalBrownConrady8 ----------------------------------------------------

	template <typename T>
	DistortedPoint<T> RationalBrownConrady8::project(const T& x, const T& y, const T& z,
													 const DistortionDomain& domain) const
	{
		return detail::projectForward(*this, x, y, z, domain);
	}

	template <typename T>
	Unprojection<T> RationalBrownConrady8::unproject(const T& xd, const T& yd, const DistortionDomain& domain) const
	{
		return detail::unprojectForward(*this, xd, yd, domain);
	}

	template <typename T>
	void RationalBrownConrady8::distort(const T& x, const T& y, T& xd, T& yd) const
	{
		const T r2 = x * x + y * y;
		const T numerator = T(1) + r2 * (T(k1) + r2 * (T(k2) + r2 * T(k3)));
		const T denominator = T(1) + r2 * (T(k4) + r2 * (T(k5) + r2 * T(k6)));
		const T radial = numerator / denominator;
		xd = x * radial + T(2 * p1) * x * y + T(p2) * (r2 + T(2) * x * x);
		yd = y * radial + T(p1) * (r2 + T(2) * y * y) + T(2 * p2) * x * y;
	}

	// --- KannalaBrandt4 -----------------------------------------------------------

	template <typename T>
	DistortedPoint<T> KannalaBrandt4::project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const
	{
		using std::atan2;
		using std::sqrt;

		if (!detail::inFront(z))
			return detail::failedPoint<T>(ProjectionStatus::BehindCamera);
		const T rho = sqrt(x * x + y * y);
		const T theta = atan2(rho, z);
		if (!(theta < T(domain.limit)))
			return detail::failedPoint<T>(ProjectionStatus::OutsideDomain);
		// On the axis, where thetad / rho has its limit 1 / z; dividing would lose the derivative an
		// automatic-differentiation scalar carries.
		if (rho * T(1e12) <= z)
			return detail::point<T>(x / z, y / z);
		const T thetad = distortedAngle(theta);
		return detail::point<T>(thetad * x / rho, thetad * y / rho);
	}

	template <typename T>
	Unprojection<T> KannalaBrandt4::unproject(const T& xd, const T& yd, const DistortionDomain& domain) const
	{
		using std::abs;
		using std::cos;
		using std::sin;
		using std::sqrt;

		const T thetad = sqrt(xd * xd + yd * yd);
		if (!(thetad < T(domain.limitImage)))
			return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
		if (thetad <= T(1e-12))
			return detail::ray(xd, yd); // on the axis: theta ~ thetad ~ r
		// The distorted angle is increasing on the domain, so Newton from thetad settles.
		T theta = thetad;
		const T tolerance = T(detail::Convergence<T>::tolerance);
		for (int iteration = 0;; ++iteration)
		{
			const T error = distortedAngle(theta) - thetad;
			if (abs(error) <= tolerance)
				break;
			if (iteration == detail::kMaxIterations)
				return detail::failedRay<T>(ProjectionStatus::NotConverged);
			theta -= error / distortedAngleSlope(theta);
		}
		if (!(theta >= T(0) && theta < T(domain.limit)))
			return detail::failedRay<T>(ProjectionStatus::OutsideDomain);
		const T s = sin(theta);
		Unprojection<T> out;
		out.x = s * xd / thetad;
		out.y = s * yd / thetad;
		out.z = cos(theta);
		return out;
	}

	template <typename T>
	T KannalaBrandt4::distortedAngle(const T& theta) const
	{
		const T t2 = theta * theta;
		return theta * (T(1) + t2 * (T(k1) + t2 * (T(k2) + t2 * (T(k3) + t2 * T(k4)))));
	}

	template <typename T>
	T KannalaBrandt4::distortedAngleSlope(const T& theta) const
	{
		const T t2 = theta * theta;
		return T(1) + t2 * (T(3 * k1) + t2 * (T(5 * k2) + t2 * (T(7 * k3) + t2 * T(9 * k4))));
	}
} // namespace lain::camera
