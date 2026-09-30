#pragma once

#include "lain/camera/distortion.h"

#include <lain/math/axisconvention.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::camera
{
	// Every camera interface speaks the camera frame: X right, Y down, Z forward (CONTEXT.md, "Camera
	// frame"). Another convention is converted at the boundary with math::basisChange, never stored
	// on a camera value.
	constexpr math::AxisConvention kCameraFrame = math::AxisConvention::XRightYDownZForward();

	// An image rectangle in pixels. Pixel (0, 0) is the CENTRE of the top-left pixel, x right and y
	// down, as OpenCV and librealsense have it, so the rectangle spans [-0.5, width - 0.5) by
	// [-0.5, height - 0.5).
	struct ImageGeometry
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		bool operator==(const ImageGeometry& rhs) const { return width == rhs.width && height == rhs.height; }
		bool operator!=(const ImageGeometry& rhs) const { return !(*this == rhs); }
	};

	// The pinhole part of a camera: focal lengths and principal point, in pixels.
	struct Intrinsics
	{
		double fx = 0;
		double fy = 0;
		double cx = 0;
		double cy = 0;
	};

	// A camera model's parameters with NOTHING checked: what an importer or a solver has before it
	// asks CameraModel::create whether they make a camera. Never used to project anything.
	struct CameraModelParameters
	{
		ImageGeometry image;
		Intrinsics intrinsics;
		Distortion distortion;
	};

	// Why parameters are not a camera model.
	enum class ModelProblem
	{
		EmptyImage,				// a zero width or height
		NonPositiveFocalLength, // fx or fy <= 0
		NonFiniteParameter,		// a NaN or an infinity anywhere
		FoldsInsideImage,		// the distortion folds before an image corner, so the corner has no ray
	};

	struct ModelDiagnostic
	{
		ModelProblem problem;
		std::string detail; // which parameter or corner, for a person
	};

	// Where a model's distortion is defined (see distortion.h). `limit` bounds the argument of its
	// radial mapping, and `limitImage` is the mapping's value there, the bound on the other side:
	//   forward Brown-Conrady and rational: the ideal radius, and the distorted radius it maps to;
	//   inverse Brown-Conrady: the DISTORTED radius, and the ideal radius it maps to;
	//   Kannala-Brandt: the angle from the axis, and the distorted radius it maps to.
	// Infinite when the mapping never folds; for Kannala-Brandt never more than pi/2.
	struct DistortionDomain
	{
		double limit = 0;
		double limitImage = 0;
	};

	struct ModelResult;

	// A calibrated camera: image geometry, intrinsics and distortion (CONTEXT.md, "Camera model").
	// IMMUTABLE and VALID: create() is the only way to make one and refuses anything malformed, so
	// every model that exists can project, and a solver's unchecked candidate cannot pass for one.
	class CameraModel
	{
	public:
		// A model from these parameters, or the diagnostics saying why not. Checks, in order: a
		// non-empty image, finite parameters, positive focal lengths, and a distortion that reaches
		// all four image corners without folding.
		static ModelResult create(const CameraModelParameters& parameters);

		const CameraModelParameters& parameters() const { return m_parameters; }
		const ImageGeometry& image() const { return m_parameters.image; }
		const Intrinsics& intrinsics() const { return m_parameters.intrinsics; }
		const Distortion& distortion() const { return m_parameters.distortion; }
		const DistortionDomain& domain() const { return m_domain; }

		// One line for a person: the distortion model, the geometry, then every parameter by name.
		std::string toString() const;

	private:
		CameraModel(const CameraModelParameters& parameters, const DistortionDomain& domain)
			: m_parameters(parameters)
			, m_domain(domain)
		{
		}

		CameraModelParameters m_parameters;
		DistortionDomain m_domain;
	};

	// The outcome of CameraModel::create: a model, or every reason there is none.
	struct ModelResult
	{
		std::optional<CameraModel> model;
		std::vector<ModelDiagnostic> diagnostics; // empty exactly when `model` is set
	};

	// Whether a model may be reused for footage of this geometry (CONTEXT.md, "Camera-model
	// applicability"). A different geometry is Incompatible. A matching one is Unknown, not
	// Compatible: nothing here yet records the lens setting, crop or camera identity that would
	// make it so, and Unknown obliges a caller to choose a policy instead of inheriting trust.
	enum class Applicability
	{
		Compatible,
		Incompatible,
		Unknown,
	};
	Applicability applicability(const CameraModel& model, const ImageGeometry& footage);
} // namespace lain::camera
