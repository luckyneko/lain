#pragma once

#include "lain/camera/cameramodel.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace lain::camera
{
	// Which physical device frames came from, as far as it is known. Every field may be empty: a
	// webcam reports a name and nothing else, and the record says what was known rather than inventing
	// the rest.
	struct DeviceDescription
	{
		std::string make;	  // "Intel"
		std::string model;	  // "RealSense D455"
		std::string identity; // a serial number or other durable per-unit identity
	};

	// The stream as the device delivered it, before anything lain did to the frames.
	struct StreamDescription
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::string format;					   // the device's own name for it: "RGB8", "YUYV"
		std::optional<double> framesPerSecond; // unset when unknown
	};

	// A camera model someone other than lain produced, and where it came from. The parameters are
	// UNCHECKED, as a manufacturer states them: what they are worth is decided where they are used,
	// through CameraModel::create and then calibration's imported-model policy, never here.
	struct ImportedModel
	{
		std::string source; // "RealSense factory calibration (rs2_intrinsics, SDK 2.55.1)"
		CameraModelParameters parameters;
	};

	// What is known about where a set of frames came from (CONTEXT.md, "Capture record"): the device,
	// the stream, free-form properties such as SDK and firmware versions, and an optional imported
	// manufacturer model. Generic by design: a second camera is a second record, never a second schema,
	// so nothing here names a device family.
	struct CaptureRecord
	{
		DeviceDescription device;
		StreamDescription stream;
		std::map<std::string, std::string> properties; // "sdk" -> "2.55.1", "firmware" -> "5.16.0.1"
		std::optional<ImportedModel> importedModel;
	};
} // namespace lain::camera
