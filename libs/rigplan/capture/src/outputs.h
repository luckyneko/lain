#pragma once

#include <lain/image/image.h>
#include <lain/rigplan/rigplan.h>
#include <lain/rigplan/sensor.h>
#include <lain/rigplan/specification.h>

#include <optional>
#include <string>
#include <vector>

namespace lain::rigplan::detail
{
	// A camera of a rig plan with what recording and writing its frames needs.
	struct OutputCamera
	{
		const PlacedCamera* placed = nullptr;
		const CameraSpecification* specification = nullptr;
		const SensorSpecification* sensor = nullptr;
		const LensSpecification* lens = nullptr;
		std::optional<Recorder> recorder;
		std::string formatKey; // the io::image writer's key, which is also the frames' extension
	};

	// The io::image format key a sensor's output is written with: "png", "tiff" or "jpg".
	std::string formatKey(OutputFormat format);

	// RGBA32F linear light, every pixel `level` in every colour, coverage 1.
	image::Image flatLight(int width, int height, float level);

	// Every camera of `plan`, which must pass check(), in its order, each with its recorder and format;
	// or, added to `problems`, why one cannot be written: no writer for its format is registered, or the
	// writer cannot hold what its sensor records (a curve the format cannot state, a depth it cannot
	// hold), asked of the codec itself on a recording of the camera's own format and tag.
	std::vector<OutputCamera> outputCameras(const RigPlan& plan, std::vector<std::string>& problems);
} // namespace lain::rigplan::detail
