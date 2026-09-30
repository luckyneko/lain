#pragma once

#include <string_view>
#include <variant>

namespace lain::camera
{
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
	// two different rays land on one pixel. A model computes that DOMAIN once (CameraModel::domain)
	// and projection reports OutsideDomain beyond it. Tangential terms are not part of the fold
	// test; they are small wherever a real lens is used.

	// No distortion: xd = x, yd = y.
	struct NoDistortion
	{
	};

	// Brown-Conrady, FORWARD: the polynomial above maps ideal to distorted, so projection is closed
	// form and unprojection iterates. Coefficient order k1, k2, p1, p2, k3 (OpenCV's default model).
	struct BrownConrady5
	{
		double k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0;
	};

	// Brown-Conrady, INVERSE: the same polynomial with the roles swapped. It maps DISTORTED to ideal,
	// (x, y) = poly(xd, yd), so unprojection is closed form and projection iterates. Coefficient
	// order k1, k2, p1, p2, k3 (librealsense's RS2_DISTORTION_INVERSE_BROWN_CONRADY).
	struct InverseBrownConrady5
	{
		double k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0;
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

	// The model's name for a person ("Brown-Conrady 5"), as the vocabulary spells it.
	std::string_view displayName(const Distortion& distortion);
	std::string_view displayName(DistortionModel model);
} // namespace lain::camera
