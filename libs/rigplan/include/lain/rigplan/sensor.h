#pragma once

#include "lain/rigplan/specification.h"

#include <lain/core/uuid.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::rigplan
{
	// The sensor model (CONTEXT.md, "Sensor model"; ADR-0029): how a sensor turns the light a capture
	// render produced into the codes it records. It runs in lain's own C++ after the light is read back,
	// and every step of it is exact IEEE arithmetic, so the same light records the same codes on every
	// machine whatever device rendered it. The light itself differs by device (ADR-0028); this does not.

	// Which camera and frame a recording's noise belongs to (CONTEXT.md, "Noise scale"). Keyed by the
	// camera's identity, never its name or its place in a list, as a draw is.
	struct NoiseKey
	{
		std::uint64_t seed = 0;
		core::Uuid camera;
		std::size_t frame = 0;
	};

	// The 64 bits every noise draw of one camera-frame grows from: the first eight bytes of SHA-256 over
	// a versioned canonical text of the key, as unitDeviation's are, so it is the same on every platform.
	std::uint64_t noiseSeed(const NoiseKey& key);

	// The standard normal draw for sample `sample` of the camera-frame whose noise seed is `seed`: the
	// sum of twelve uniform draws, less six (Irwin-Hall), each the half of one of SplitMix64's outputs
	// 6 * sample + 1 to 6 * sample + 6 from `seed`. Counter-based, so a draw depends on nothing but its
	// sample, and the sum is taken in integers, so the draw is exact: mean exactly 0, variance 1, within
	// six of 0. Exact rather than libm's Box-Muller, whose log and cos are not correctly rounded and so
	// differ by platform.
	double standardNormal(std::uint64_t seed, std::uint64_t sample);

	// A sensor model prepared to record: its output codes for every ADC code, found once.
	class Recorder
	{
	public:
		// The recorder, or none when the model has problems (sensorModelProblems says which).
		static std::optional<Recorder> create(const SensorModel& model);

		const SensorModel& sensorModel() const { return m_model; }

		// What a recording is: Gray8, Gray16, RGB8 or RGB16, its channels and output depth.
		image::PixelFormat pixelFormat() const;

		// The output code of each ADC code, in order: the code's linear fraction of full scale encoded
		// through the output curve (image::transferCurve) and rounded to the output depth. A finite table,
		// so the only arithmetic here that is not exact IEEE (the curve's pow) is a fixed set of values
		// that a test can pin on every platform.
		const std::vector<std::uint16_t>& outputCodes() const { return m_codes; }

		// What the sensor records of `light`, RGBA32F linear light as a capture returns it (its alpha,
		// coverage, is ignored), at `noiseScale` (0 to 1) of its noise. Per sample, with g the gain, W the
		// full well, R the read noise, b the ADC bits and z the sample's standardNormal:
		//   e = light * (g * W)                          electrons; 0 for light that is not positive
		//   e' = e + noiseScale * sqrt(e + R * R) * z     shot and read noise, in quadrature
		//   adc = floor(e' * ((2^b - 1) / W) + 0.5)       clamped to 0 .. 2^b - 1: the ADC saturates
		//   code = outputCodes()[adc]
		// A Grey sensor's sample is the Rec709 luminance of the pixel's light, a Rgb sensor's each colour.
		// Sample i of a frame is channel c of pixel (x, y) at i = (y * width + x) * channels + c. The
		// recording is tagged with the output curve and has no alpha. Rows run on the process pool, and a
		// draw depends only on its sample, so the codes are the same however many threads record them.
		// An invalid image when `light` is not valid RGBA32F linear light.
		image::Image record(const image::Image& light, const NoiseKey& key, double noiseScale) const;

	private:
		explicit Recorder(const SensorModel& model);

		SensorModel m_model;
		std::vector<std::uint16_t> m_codes;
	};
} // namespace lain::rigplan
