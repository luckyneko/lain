#include "lain/rigplan/capture.h"
#include "outputs.h"

#include <lain/io/image/save.h>
#include <lain/string/format.h>

#include <algorithm>

namespace lain::rigplan
{
	// The side of the flat tile a camera's floor is measured on: enough pixels that a codec's fixed
	// overhead is small beside them, few enough to encode at once.
	static constexpr int kTileSide = 256;

	// A byte count for a person, in decimal units as a disk is sold.
	static std::string bytesText(std::uint64_t bytes)
	{
		const char* units[] = {"bytes", "kB", "MB", "GB", "TB"};
		double value = double(bytes);
		std::size_t unit = 0;
		while (value >= 1000 && unit + 1 < std::size(units))
		{
			value /= 1000;
			++unit;
		}
		return unit == 0 ? string::format("{} bytes", bytes) : string::format("{:.1f} {}", value, units[unit]);
	}

	std::string SizeEstimate::toString() const
	{
		if (!problems.empty())
			return "no estimate: " + problems.front();
		std::string text = string::format("{} camera{} x {} frame{}: between {} (every frame a flat picture) and {} (uncompressed) on disk",
										  cameras.size(), cameras.size() == 1 ? "" : "s", frames, frames == 1 ? "" : "s", bytesText(floor), bytesText(raw));
		std::vector<std::string> defeated;
		for (const CameraEstimate& camera : cameras)
		{
			if (camera.noiseDefeatsCompression)
				defeated.push_back(camera.camera);
		}
		if (!defeated.empty())
		{
			std::string names;
			for (std::size_t i = 0; i < std::min<std::size_t>(defeated.size(), 5); ++i)
				names += (i == 0 ? "" : ", ") + defeated[i];
			if (defeated.size() > 5)
				names += string::format(" and {} more", defeated.size() - 5);
			text += string::format("\nnoise defeats compression for {}: even a flat picture takes half its uncompressed size "
								   "or more, so its frames will be near {} however simple the scene",
								   names, defeated.size() == 1 ? "that" : "those");
		}
		return text;
	}

	SizeEstimate estimateSize(const RigPlan& source, const CaptureOptions& options)
	{
		SizeEstimate estimate;
		const RigPlan plan = capturedPlan(source, options);
		for (const PlanDiagnostic& diagnostic : check(plan))
			estimate.problems.push_back(diagnostic.detail);
		if (!estimate.problems.empty())
			return estimate;
		const std::vector<detail::OutputCamera> cameras = detail::outputCameras(plan, estimate.problems);
		if (!estimate.problems.empty())
			return estimate;

		const std::uint64_t frames = plan.timeline.frames;
		estimate.frames = plan.timeline.frames;
		for (const detail::OutputCamera& camera : cameras)
		{
			const SensorModel& model = camera.sensor->sensorModel;
			const std::uint64_t pixels = std::uint64_t(camera.sensor->image.width) * camera.sensor->image.height;
			io::image::ImageWriterOptions writing;
			writing.quality = model.output.quality;
			// Half of full scale, recorded with this capture's noise for this camera's first frame, and
			// encoded through its codec.
			const auto flatFrame = [&](int width, int height) -> std::optional<std::uint64_t>
			{
				const image::Image recorded = camera.recorder->record(detail::flatLight(width, height, float(0.5 / model.gain)),
																	  {plan.seed, camera.placed->id, 0}, plan.scales.noise);
				const std::optional<memory::Buffer> bytes = io::image::encode(camera.formatKey, recorded, writing);
				if (!bytes)
					return std::nullopt;
				return std::uint64_t(bytes->size());
			};
			// A tile and half of it, so what every file costs whatever its pixels (headers, a curve table)
			// is told apart from what each pixel costs: scaling the whole tile up by pixel count would
			// multiply a 16-bit TIFF's 128 kB transfer function by it too.
			const int width = std::min<int>(int(camera.sensor->image.width), kTileSide);
			const int height = std::min<int>(int(camera.sensor->image.height), kTileSide);
			const int half = std::max(1, height / 2);
			const std::optional<std::uint64_t> whole = flatFrame(width, height);
			const std::optional<std::uint64_t> part = flatFrame(width, half);
			if (!whole || !part)
			{
				estimate.problems.push_back(string::format("camera '{}': {} would not encode a flat picture", camera.placed->name, camera.formatKey));
				continue;
			}
			const std::uint64_t wholePixels = std::uint64_t(width) * std::uint64_t(height);
			const std::uint64_t partPixels = std::uint64_t(width) * std::uint64_t(half);
			const double perPixel =
				wholePixels > partPixels ? std::max(0.0, (double(*whole) - double(*part)) / double(wholePixels - partPixels)) : 0.0;
			CameraEstimate one;
			one.camera = camera.placed->name;
			one.overhead = std::uint64_t(std::max(0.0, double(*whole) - perPixel * double(wholePixels)));
			// A camera no larger than the tile was measured whole.
			const std::uint64_t frame = pixels <= wholePixels ? *whole : one.overhead + std::uint64_t(perPixel * double(pixels));
			const std::uint64_t bytesPerPixel = image::formatDescriptor(camera.recorder->pixelFormat()).bytesPerPixel();
			one.raw = frames * (pixels * bytesPerPixel + one.overhead);
			one.floor = frames * frame;
			one.noiseDefeatsCompression = 2 * perPixel >= double(bytesPerPixel);
			estimate.raw += one.raw;
			estimate.floor += one.floor;
			estimate.cameras.push_back(std::move(one));
		}
		if (!estimate.problems.empty())
			return SizeEstimate{{}, 0, 0, 0, std::move(estimate.problems)};
		return estimate;
	}
} // namespace lain::rigplan
