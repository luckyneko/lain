#include "lain/rigplan/sensor.h"

#include <lain/core/sha256.h>
#include <lain/image/color.h>
#include <lain/image/imageview.h>
#include <lain/image/transfercurve.h>
#include <lain/task/task.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

// This file is compiled with floating-point contraction off (CMakeLists.txt): clang fuses a * b + c
// into one rounding on arm64 and not on x86-64, which would make a recording's codes differ by platform
// at a rounding boundary. Every expression below is then one IEEE operation at a time.

namespace lain::rigplan
{
	std::uint64_t noiseSeed(const NoiseKey& key)
	{
		// Versioned, so a change to how noise is drawn is a change of name rather than a silent re-roll
		// of every capture ever recorded. Each field on its own line, as unitDeviation's text is.
		std::string text = "lain-rigplan-noise/1\n";
		text += std::to_string(key.seed);
		text += '\n';
		text += key.camera.toString();
		text += '\n';
		text += std::to_string(key.frame);
		text += '\n';
		const core::Sha256Digest digest = core::sha256(text);
		std::uint64_t bits = 0;
		for (std::size_t i = 0; i < 8; ++i)
			bits = (bits << 8) | digest.bytes[i];
		return bits;
	}

	// SplitMix64's output `n` (1-based) from `seed`, computed directly: its state after n steps is
	// seed + n * gamma, mixed. So a draw needs no state carried from the draw before it.
	static std::uint64_t splitMix64(std::uint64_t seed, std::uint64_t n)
	{
		std::uint64_t z = seed + n * 0x9E3779B97F4A7C15ull;
		z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
		z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
		return z ^ (z >> 31);
	}

	double standardNormal(std::uint64_t seed, std::uint64_t sample)
	{
		// Twelve 32-bit uniforms, each (u + 0.5) / 2^32 so its mean is exactly 1/2, summed exactly: the
		// integer sum is below 12 * 2^32, a double holds it, and the scaling is by a power of two.
		std::uint64_t sum = 0;
		for (std::uint64_t j = 1; j <= 6; ++j)
		{
			const std::uint64_t bits = splitMix64(seed, 6 * sample + j);
			sum += bits >> 32;
			sum += bits & 0xFFFFFFFFull;
		}
		return (double(sum) + 6.0) * 0x1.0p-32 - 6.0;
	}

	Recorder::Recorder(const SensorModel& model)
		: m_model(model)
	{
		const std::uint32_t adcMax = (1u << model.adcBits) - 1u;
		const double outputMax = double((1u << model.output.bits) - 1u);
		const image::TransferCurve curve = image::transferCurve(model.output.curve);
		m_codes.resize(std::size_t(adcMax) + 1);
		for (std::uint32_t adc = 0; adc <= adcMax; ++adc)
		{
			const float encoded = curve.fromLinear(float(double(adc) / double(adcMax)));
			const double code = std::floor(double(encoded) * outputMax + 0.5);
			m_codes[adc] = std::uint16_t(std::clamp(code, 0.0, outputMax));
		}
	}

	std::optional<Recorder> Recorder::create(const SensorModel& model)
	{
		if (!sensorModelProblems(model).empty())
			return std::nullopt;
		return Recorder(model);
	}

	image::PixelFormat Recorder::pixelFormat() const
	{
		const bool deep = m_model.output.bits == 16;
		if (m_model.channels == SensorChannels::Grey)
			return deep ? image::PixelFormat::Gray16 : image::PixelFormat::Gray8;
		return deep ? image::PixelFormat::RGB16 : image::PixelFormat::RGB8;
	}

	image::Image Recorder::record(const image::Image& light, const NoiseKey& key, double noiseScale) const
	{
		if (!light.valid() || light.pixelFormat() != image::PixelFormat::RGBA32F || light.colorSpace() != image::ColorSpace::Linear)
			return {};

		const int width = light.width();
		const int height = light.height();
		const bool grey = m_model.channels == SensorChannels::Grey;
		const std::size_t channels = grey ? 1 : 3;
		const bool deep = m_model.output.bits == 16;
		image::Image out(width, height, pixelFormat(), m_model.output.curve);

		const std::uint64_t seed = noiseSeed(key);
		const double electronsPerUnit = m_model.gain * m_model.fullWellElectrons;
		const double readVariance = m_model.readNoiseElectrons * m_model.readNoiseElectrons;
		const double adcMax = double(m_codes.size() - 1);
		const double adcPerElectron = adcMax / m_model.fullWellElectrons;
		const bool noisy = noiseScale > 0;

		const image::ConstImageView<image::ColorRGBAf> view = light.as<image::ColorRGBAf>();
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(height),
					[&](std::ptrdiff_t y)
					{
						for (int x = 0; x < width; ++x)
						{
							const image::ColorRGBAf& pixel = view(x, int(y));
							for (std::size_t c = 0; c < channels; ++c)
							{
								double value = 0;
								if (grey)
								{
									// Rec709 luminance, as image::luminance weighs it, taken in double here.
									value = 0.2126 * double(pixel[0]);
									value = value + 0.7152 * double(pixel[1]);
									value = value + 0.0722 * double(pixel[2]);
								}
								else
								{
									value = double(pixel[int(c)]);
								}

								const std::size_t sample = (std::size_t(y) * std::size_t(width) + std::size_t(x)) * channels + c;
								std::uint16_t adc = 0;
								double electrons = value > 0 ? value * electronsPerUnit : 0.0;
								if (electrons == std::numeric_limits<double>::infinity())
								{
									adc = std::uint16_t(adcMax); // saturated, past anything noise could move
								}
								else
								{
									if (noisy)
									{
										const double sigma = std::sqrt(electrons + readVariance);
										const double z = standardNormal(seed, std::uint64_t(sample));
										electrons = electrons + noiseScale * sigma * z;
									}
									const double level = std::floor(electrons * adcPerElectron + 0.5);
									adc = std::uint16_t(std::clamp(level, 0.0, adcMax));
								}

								const std::uint16_t code = m_codes[adc];
								if (deep)
									reinterpret_cast<std::uint16_t*>(out.data())[sample] = code;
								else
									out.data()[sample] = std::uint8_t(code);
							}
						}
					});
		return out;
	}
} // namespace lain::rigplan
