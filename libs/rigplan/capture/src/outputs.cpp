#include "outputs.h"

#include <lain/io/image/save.h>
#include <lain/meta/enums.h>
#include <lain/string/format.h>

#include <algorithm>

namespace lain::rigplan::detail
{
	std::string formatKey(OutputFormat format)
	{
		switch (format)
		{
			case OutputFormat::Png:
				return "png";
			case OutputFormat::Tiff:
				return "tiff";
			case OutputFormat::Jpeg:
				return "jpg";
		}
		return {};
	}

	image::Image flatLight(int width, int height, float level)
	{
		image::Image light(width, height, image::PixelFormat::RGBA32F, image::ColorSpace::Linear);
		float* texel = reinterpret_cast<float*>(light.data());
		for (std::size_t i = 0; i < light.pixelCount(); ++i, texel += 4)
		{
			std::fill(texel, texel + 3, level);
			texel[3] = 1.0f;
		}
		return light;
	}

	std::vector<OutputCamera> outputCameras(const RigPlan& plan, std::vector<std::string>& problems)
	{
		std::vector<OutputCamera> cameras;
		for (const PlacedCamera& placed : plan.cameras)
		{
			OutputCamera camera;
			camera.placed = &placed;
			camera.specification = findCamera(plan.specifications, placed.camera);
			camera.sensor = findSensor(plan.specifications, camera.specification->sensor);
			camera.lens = findLens(plan.specifications, camera.specification->lens);
			camera.recorder = Recorder::create(camera.sensor->sensorModel);
			camera.formatKey = formatKey(camera.sensor->sensorModel.output.format);
			// What the codec is asked about is a recording, so its pixel format and its tag are the ones
			// every frame will have.
			const image::Image sample = camera.recorder->record(flatLight(1, 1, 0.5f), NoiseKey{}, 0.0);
			if (!io::image::writerRegistry().contains(camera.formatKey))
				problems.push_back(string::format("camera '{}': this build has no {} writer", placed.name, camera.formatKey));
			else if (!io::image::canEncode(camera.formatKey, sample))
				problems.push_back(string::format("camera '{}': {} cannot hold what its sensor records ({} with a {} curve)", placed.name,
												  camera.formatKey, meta::enums::name(sample.pixelFormat()), meta::enums::name(sample.colorSpace())));
			cameras.push_back(std::move(camera));
		}
		return cameras;
	}
} // namespace lain::rigplan::detail
