#pragma once

#include <array>
#include <string_view>
#include <variant>
#include <vector>

namespace lain::camera
{
	// The outcome of projecting or unprojecting through a camera model or its distortion (CONTEXT.md,
	// "Projection status"). Failure is a status, never a NaN and never an exception; a failed
	// result's coordinates are zero and mean nothing.
	enum class ProjectionStatus
	{
		Ok,
		BehindCamera,  // the point is not in front of the camera (Z <= 0)
		OutsideDomain, // past where the model's distortion is defined (see DistortionDomain)
		NotConverged,  // the iterative inverse of the distortion did not settle
	};

	// DISTORTED normalised coordinates (xd, yd): where a distortion model projects a camera-frame
	// direction, before the intrinsics turn it into a pixel.
	template <typename T>
	struct DistortedPoint
	{
		ProjectionStatus status = ProjectionStatus::Ok;
		T x = T(0);
		T y = T(0);

		bool ok() const { return status == ProjectionStatus::Ok; }
	};

	// A ray: the UNIT direction, in the camera frame, of every point that projects to the pixel. Its
	// z is always positive when the status is Ok.
	template <typename T>
	struct Unprojection
	{
		ProjectionStatus status = ProjectionStatus::Ok;
		T x = T(0);
		T y = T(0);
		T z = T(0);

		bool ok() const { return status == ProjectionStatus::Ok; }
	};

	// Where a model's distortion is defined. `limit` bounds the argument of its radial mapping, and
	// `limitImage` is the mapping's value there, the bound on the other side:
	//   forward Brown-Conrady and rational: the ideal radius, and the distorted radius it maps to;
	//   inverse Brown-Conrady: the DISTORTED radius, and the ideal radius it maps to;
	//   Kannala-Brandt: the angle from the axis, and the distorted radius it maps to.
	// Infinite when the mapping never folds; for Kannala-Brandt never more than pi/2.
	struct DistortionDomain
	{
		double limit = 0;
		double limitImage = 0;
	};

	// The closed set of lens distortion models (ADR-0016). Each names an exact formula, a
	// coefficient order and a DIRECTION, not a coefficient count: BrownConrady5 and
	// InverseBrownConrady5 hold the same five numbers and are not interchangeable.
	//
	// Throughout, (x, y) are IDEAL normalised coordinates, a camera-frame point (X, Y, Z) divided by
	// Z, and (xd, yd) are DISTORTED normalised coordinates, which the intrinsics turn into pixels:
	// u = fx * xd + cx, v = fy * yd + cy. With r2 = x*x + y*y, the Brown-Conrady polynomial is
	//
	//   radial = 1 + k1*r2 + k2*r2^2 + k3*r2^3
	//   xd = x*radial + 2*p1*x*y + p2*(r2 + 2*x*x)
	//   yd = y*radial + p1*(r2 + 2*y*y) + 2*p2*x*y
	//
	// Every model is only defined where its radial mapping is still increasing: past the first fold
	// two different rays land on one pixel. A model computes that domain once (CameraModel::domain)
	// and projection reports OutsideDomain beyond it. Tangential terms are not part of the fold
	// test; they are small wherever a real lens is used.
	//
	// EVERY MODEL CARRIES ITS OWN ALGORITHM, so what a model is and what it does are read in one
	// place, and a new model that lacks any of this does not compile where the variant is visited:
	//
	//   kDisplayName        the model's name for a person, as the vocabulary spells it
	//   kCoefficientNames   its coefficients' names, in its coefficient order
	//   coefficients()      their values, in that order
	//   project(x, y, z)    a camera-frame direction to distorted normalised coordinates
	//   unproject(xd, yd)   distorted normalised coordinates to a unit ray
	//   domain()            where the model is defined; a scan, so CameraModel computes it once and
	//                       hands it back to project / unproject
	//
	// project and unproject are TOTAL (a point not in front of the camera is BehindCamera, never a
	// division by zero), and SCALAR-GENERIC, so an automatic-differentiation scalar (a Ceres Jet)
	// runs this same code: one projection implementation, which calibration, validation and later
	// refinement all use, so what a solver minimises is what a report measures. The coefficients
	// stay double; only the point is T. Which direction is closed form and which iterates is each
	// model's own business. A model's raw formula (distort, undistort, distortedAngle) is public
	// too, and checks nothing: it is the formula above made callable, and project / unproject are
	// its checked use.

	// No distortion: xd = x, yd = y.
	struct NoDistortion
	{
		static constexpr std::string_view kDisplayName = "no distortion";
		static constexpr std::array<std::string_view, 0> kCoefficientNames = {};
		std::array<double, 0> coefficients() const { return {}; }

		template <typename T>
		DistortedPoint<T> project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const;
		template <typename T>
		Unprojection<T> unproject(const T& xd, const T& yd, const DistortionDomain& domain) const;
		DistortionDomain domain() const;
	};

	// Brown-Conrady, FORWARD: the polynomial above maps ideal to distorted, so projection is closed
	// form and unprojection iterates. Coefficient order k1, k2, p1, p2, k3 (OpenCV's default model).
	struct BrownConrady5
	{
		double k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0;

		static constexpr std::string_view kDisplayName = "Brown-Conrady 5";
		static constexpr std::array<std::string_view, 5> kCoefficientNames = {"k1", "k2", "p1", "p2", "k3"};
		std::array<double, 5> coefficients() const { return {k1, k2, p1, p2, k3}; }

		template <typename T>
		DistortedPoint<T> project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const;
		template <typename T>
		Unprojection<T> unproject(const T& xd, const T& yd, const DistortionDomain& domain) const;
		DistortionDomain domain() const;

		// Ideal (x, y) to distorted (xd, yd): the polynomial.
		template <typename T>
		void distort(const T& x, const T& y, T& xd, T& yd) const;
	};

	// Brown-Conrady, INVERSE: the same polynomial with the roles swapped. It maps DISTORTED to ideal,
	// (x, y) = poly(xd, yd), so unprojection is closed form and projection iterates. Coefficient
	// order k1, k2, p1, p2, k3 (librealsense's RS2_DISTORTION_INVERSE_BROWN_CONRADY).
	struct InverseBrownConrady5
	{
		double k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0;

		static constexpr std::string_view kDisplayName = "Inverse Brown-Conrady 5";
		static constexpr std::array<std::string_view, 5> kCoefficientNames = {"k1", "k2", "p1", "p2", "k3"};
		std::array<double, 5> coefficients() const { return {k1, k2, p1, p2, k3}; }

		template <typename T>
		DistortedPoint<T> project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const;
		template <typename T>
		Unprojection<T> unproject(const T& xd, const T& yd, const DistortionDomain& domain) const;
		DistortionDomain domain() const;

		// Distorted (xd, yd) to ideal (x, y): the polynomial, which is this model's direction.
		template <typename T>
		void undistort(const T& xd, const T& yd, T& x, T& y) const;
	};

	// Brown-Conrady, FORWARD, with the tangential terms applied to the radially distorted point
	// (librealsense's RS2_DISTORTION_MODIFIED_BROWN_CONRADY). With r2 from the IDEAL point:
	//
	//   x' = x*radial, y' = y*radial
	//   xd = x' + 2*p1*x'*y' + p2*(r2 + 2*x'*x')
	//   yd = y' + 2*p2*x'*y' + p1*(r2 + 2*y'*y')
	//
	// Coefficient order k1, k2, p1, p2, k3. Representable and applicable; no backend estimates it.
	struct ModifiedBrownConrady5
	{
		double k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0;

		static constexpr std::string_view kDisplayName = "Modified Brown-Conrady 5";
		static constexpr std::array<std::string_view, 5> kCoefficientNames = {"k1", "k2", "p1", "p2", "k3"};
		std::array<double, 5> coefficients() const { return {k1, k2, p1, p2, k3}; }

		template <typename T>
		DistortedPoint<T> project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const;
		template <typename T>
		Unprojection<T> unproject(const T& xd, const T& yd, const DistortionDomain& domain) const;
		DistortionDomain domain() const;

		// Ideal (x, y) to distorted (xd, yd): the formula above.
		template <typename T>
		void distort(const T& x, const T& y, T& xd, T& yd) const;
	};

	// Rational Brown-Conrady, FORWARD (OpenCV's CALIB_RATIONAL_MODEL): the radial factor becomes
	//
	//   radial = (1 + k1*r2 + k2*r2^2 + k3*r2^3) / (1 + k4*r2 + k5*r2^2 + k6*r2^3)
	//
	// with the tangential terms unchanged. Coefficient order k1, k2, p1, p2, k3, k4, k5, k6. The
	// domain also ends where the denominator reaches zero.
	struct RationalBrownConrady8
	{
		double k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0, k4 = 0, k5 = 0, k6 = 0;

		static constexpr std::string_view kDisplayName = "Rational Brown-Conrady 8";
		static constexpr std::array<std::string_view, 8> kCoefficientNames = {"k1", "k2", "p1", "p2",
																			  "k3", "k4", "k5", "k6"};
		std::array<double, 8> coefficients() const { return {k1, k2, p1, p2, k3, k4, k5, k6}; }

		template <typename T>
		DistortedPoint<T> project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const;
		template <typename T>
		Unprojection<T> unproject(const T& xd, const T& yd, const DistortionDomain& domain) const;
		DistortionDomain domain() const;

		// Ideal (x, y) to distorted (xd, yd): the formula above.
		template <typename T>
		void distort(const T& x, const T& y, T& xd, T& yd) const;
	};

	// Kannala-Brandt, FORWARD (OpenCV's fisheye model), on the angle from the optical axis rather
	// than on r. For a camera-frame point with rho = sqrt(X*X + Y*Y):
	//
	//   theta  = atan2(rho, Z)
	//   thetad = theta * (1 + k1*theta^2 + k2*theta^4 + k3*theta^6 + k4*theta^8)
	//   xd = thetad * X / rho, yd = thetad * Y / rho      (both 0 on the axis)
	//
	// Coefficient order k1, k2, k3, k4. Like every model here it needs Z > 0, so it covers fields of
	// view under 180 degrees, which is what OpenCV's estimator assumes too.
	struct KannalaBrandt4
	{
		double k1 = 0, k2 = 0, k3 = 0, k4 = 0;

		static constexpr std::string_view kDisplayName = "Kannala-Brandt 4";
		static constexpr std::array<std::string_view, 4> kCoefficientNames = {"k1", "k2", "k3", "k4"};
		std::array<double, 4> coefficients() const { return {k1, k2, k3, k4}; }

		template <typename T>
		DistortedPoint<T> project(const T& x, const T& y, const T& z, const DistortionDomain& domain) const;
		template <typename T>
		Unprojection<T> unproject(const T& xd, const T& yd, const DistortionDomain& domain) const;
		DistortionDomain domain() const;

		// The distorted angle thetad for an angle theta from the axis, and its derivative.
		template <typename T>
		T distortedAngle(const T& theta) const;
		template <typename T>
		T distortedAngleSlope(const T& theta) const;
	};

	using Distortion = std::variant<NoDistortion, BrownConrady5, InverseBrownConrady5, ModifiedBrownConrady5,
									RationalBrownConrady8, KannalaBrandt4>;

	// A distortion model named without its coefficients: what a calibration request asks to
	// estimate. The order is the variant's, so modelOf is the variant's index.
	enum class DistortionModel
	{
		None,
		BrownConrady5,
		InverseBrownConrady5,
		ModifiedBrownConrady5,
		RationalBrownConrady8,
		KannalaBrandt4,
	};
	static_assert(std::variant_size_v<Distortion> == 6, "every Distortion alternative needs a DistortionModel");

	inline DistortionModel modelOf(const Distortion& distortion)
	{
		return DistortionModel(distortion.index());
	}

	// The model with every coefficient zero: a starting point that seeds nothing.
	Distortion neutral(DistortionModel model);

	// The model's name for a person ("Brown-Conrady 5"): its kDisplayName.
	std::string_view displayName(const Distortion& distortion);
	std::string_view displayName(DistortionModel model);

	// A model's coefficient names and values, in its coefficient order: its kCoefficientNames and
	// coefficients(), for code that holds the variant rather than the model.
	std::vector<std::string_view> coefficientNames(DistortionModel model);
	std::vector<double> coefficients(const Distortion& distortion);
} // namespace lain::camera

#include "lain/camera/details/distortion.inl"
