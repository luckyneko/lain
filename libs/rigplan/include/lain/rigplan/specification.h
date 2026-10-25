#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/core/length.h>
#include <lain/image/color.h>
#include <lain/image/colorspace.h>
#include <lain/math/types.h>

#include <cstdint>
#include <optional>
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

	// What a sensor records: every colour channel of the light, or its luminance alone.
	enum class SensorChannels
	{
		Rgb,
		Grey, // Rec709 luminance of the linear light, as a monochrome sensor sees it
	};

	// The file format a sensor's frames are written in, through io::image.
	enum class OutputFormat
	{
		Png,
		Tiff,
		Jpeg, // lossy, and recorded as lossy: the codec's loss is the only loss a capture accepts
	};

	// How a sensor's frames are stored: a format, a depth and the curve their codes are encoded with.
	struct SensorOutput
	{
		OutputFormat format = OutputFormat::Png;
		std::uint32_t bits = 8;							   // 8 or 16 per channel; JPEG holds 8
		image::ColorSpace curve = image::ColorSpace::sRGB; // Linear, sRGB or BT709
		std::optional<std::uint8_t> quality;			   // 1 to 100, JPEG only; unset is the codec's default
	};

	// How a sensor turns the light a capture render produced into the codes it records (CONTEXT.md,
	// "Sensor model"), as a datasheet states it. The gain is fixed: nothing here is metered from the
	// scene (ADR-0029). A Recorder in sensor.h is what records through it.
	struct SensorModel
	{
		// The fraction of full scale one unit of the renderer's linear light fills: 1 puts light 1 at
		// full scale.
		double gain = 1;
		double fullWellElectrons = 10000; // the charge at full scale, which sets the shot noise
		double readNoiseElectrons = 3;	  // the noise of reading a pixel, whatever its charge
		std::uint32_t adcBits = 12;		  // 1 to 16: the depth the charge is quantised at, linearly
		SensorChannels channels = SensorChannels::Rgb;
		SensorOutput output;
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
		SensorModel sensorModel;
	};

	// Why `model` is no usable sensor model: a gain or full well that is not positive and finite, a read
	// noise that is negative or not finite, ADC bits outside 1 to 16, an output that is not 8 or 16 bits,
	// a JPEG deeper than 8, a curve that is not Linear, sRGB or BT709, or a quality outside 1 to 100 or
	// given to a lossless format. One line per fault, each starting with what is wrong; empty when it is
	// usable. Whether the format's codec can state the curve is the capture writer's question, asked of
	// the codec itself.
	std::vector<std::string> sensorModelProblems(const SensorModel& model);

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

	enum class LightKind
	{
		Point, // shines every way from a point
		Spot,  // a point shining into a cone along its forward axis
	};

	// What a datasheet states about one kind of light (CONTEXT.md, "Light specification"). The first
	// cut states little more than a kind, an intensity and a colour; physical units, colour
	// temperature and area lights each arrive later as a field of this. No tolerances yet.
	struct LightSpecification
	{
		std::string name; // unique among the lights of one set of specifications
		LightKind kind = LightKind::Point;
		double intensity = 1;					   // linear, in the renderer's own scale until lights gain physical units
		image::ColorRGBf colour{1.0f, 1.0f, 1.0f}; // linear RGB
		// A spot's cone: full intensity inside the inner angle, falling to none at the outer one, each
		// measured from the forward axis. Unused by a point light.
		double innerConeDegrees = 20;
		double outerConeDegrees = 30;
	};

	// A set of specifications: what a rig plan holds its own copy of, and what a specification
	// catalogue is a document of.
	struct Specifications
	{
		std::vector<SensorSpecification> sensors;
		std::vector<LensSpecification> lenses;
		std::vector<CameraSpecification> cameras;
		std::vector<LightSpecification> lights;
	};

	// The named specification, or null.
	const SensorSpecification* findSensor(const Specifications& specifications, std::string_view name);
	const LensSpecification* findLens(const Specifications& specifications, std::string_view name);
	const CameraSpecification* findCamera(const Specifications& specifications, std::string_view name);
	const LightSpecification* findLight(const Specifications& specifications, std::string_view name);

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
		InvalidLight,			// a negative or non-finite intensity or colour, or a spot's cone out of order
		InvalidSensorModel,		// a sensor's sensor model is not usable (sensorModelProblems)
	};

	struct SpecificationDiagnostic
	{
		SpecificationProblem problem;
		std::string detail; // which specification and what about it, for a person
	};

	// Every problem with `specifications`, in the order the set lists them: sensors, lenses, cameras,
	// then lights. Empty when every specification can be used. A camera whose sensor or lens is unknown or
	// itself has a problem is not also reported for its nominal model.
	std::vector<SpecificationDiagnostic> check(const Specifications& specifications);
} // namespace lain::rigplan
