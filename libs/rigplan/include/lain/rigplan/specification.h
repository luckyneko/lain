#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/core/length.h>
#include <lain/math/types.h>

#include <string>
#include <string_view>
#include <vector>

namespace lain::rigplan
{
	// What a datasheet states about the parts a rig is built from (CONTEXT.md, "Rig design"). A
	// specification is nominal: it says what a part is meant to be, and its tolerances say how far a
	// real unit may stray. A realisation draws a unit; nothing here does.
	//
	// Units follow the part: lengths a datasheet quotes are core::Length (exact, so 1.4 um stays
	// 1.4 um), body geometry is metres as every position in a rig plan is, and a fractional
	// tolerance is a fraction (0.01 is +/-1%). Every tolerance is a symmetric bound, never negative.

	// The physical body a sensor sits in: what the viewport draws, and where the optical centre is.
	struct SensorBody
	{
		// The body's extent, metres, along the camera frame's axes (X right, Y down, Z forward).
		math::Vec3d size{0.1, 0.1, 0.1};
		// The optical centre relative to the mounting point, metres, in the camera frame: a placed
		// camera's pose on its mount places the mounting point, and the camera sits here from it. The
		// body is mounted square to the sensor, so the axes are the camera frame's.
		math::Vec3d opticalCentre{0.0, 0.0, 0.0};
	};

	// The body half of a camera (CONTEXT.md, "Sensor specification").
	struct SensorSpecification
	{
		std::string name; // unique among the sensors of one set of specifications
		camera::ImageGeometry image;
		core::Length pixelPitch; // square pixels
		// How far a unit's principal point may sit from the sensor's centre, on each axis: lens
		// decentring and sensor placement together.
		core::Length principalPointTolerance;
		SensorBody body;
	};

	// The optical half of a camera (CONTEXT.md, "Lens specification"). Its distortion is in normalised
	// image coordinates, so it means the same on any sensor.
	struct LensSpecification
	{
		std::string name; // unique among the lenses of one set of specifications
		core::Length focalLength;
		camera::Distortion distortion;
		// How far a unit's focal length may stray, as a fraction of it. Below 1: at 1 a unit could
		// have no focal length at all.
		double focalLengthTolerance = 0;
		// How far a unit's distortion may stray in strength, as a fraction: every coefficient scales
		// together, so a unit keeps the lens's shape. A lens with no distortion has none to vary.
		double distortionTolerance = 0;
	};

	// One sensor paired with one lens (CONTEXT.md, "Camera specification"), each named in the same set.
	struct CameraSpecification
	{
		std::string name; // unique among the cameras of one set of specifications
		std::string sensor;
		std::string lens;
	};

	// A set of specifications: what a rig plan holds its own copy of, and what a specification
	// catalogue is a document of.
	struct Specifications
	{
		std::vector<SensorSpecification> sensors;
		std::vector<LensSpecification> lenses;
		std::vector<CameraSpecification> cameras;
	};

	// The named specification, or null.
	const SensorSpecification* findSensor(const Specifications& specifications, std::string_view name);
	const LensSpecification* findLens(const Specifications& specifications, std::string_view name);
	const CameraSpecification* findCamera(const Specifications& specifications, std::string_view name);

	// The camera model a sensor and a lens make as a datasheet states them: fx = fy = focal length
	// over pixel pitch, the principal point at the sensor's centre ((width - 1) / 2, (height - 1) / 2,
	// since pixel (0, 0) is the centre of the top-left pixel), and the lens's distortion. It is
	// derived, never typed in. Checked by CameraModel::create, so a lens whose distortion folds inside
	// this sensor's image makes no model, and the diagnostics say why.
	camera::ModelResult nominalModel(const SensorSpecification& sensor, const LensSpecification& lens);

	// Why a set of specifications cannot be used.
	enum class SpecificationProblem
	{
		Unnamed,				// an empty name
		DuplicateName,			// two specifications of one kind share a name
		UnknownSensor,			// a camera names a sensor the set does not have
		UnknownLens,			// a camera names a lens the set does not have
		EmptyImage,				// a sensor with a zero width or height
		NonPositivePixelPitch,	// a sensor's pitch is zero or negative
		NonPositiveFocalLength, // a lens's focal length is zero or negative
		InvalidTolerance,		// a tolerance that is negative, not finite, or (for focal length) 1 or more
		InvalidNominalModel,	// a camera's sensor and lens make no camera model
	};

	struct SpecificationDiagnostic
	{
		SpecificationProblem problem;
		std::string detail; // which specification and what about it, for a person
	};

	// Every problem with `specifications`, in the order the set lists them: sensors, lenses, then
	// cameras. Empty when every specification can be used. A camera whose sensor or lens is unknown or
	// itself has a problem is not also reported for its nominal model.
	std::vector<SpecificationDiagnostic> check(const Specifications& specifications);
} // namespace lain::rigplan
