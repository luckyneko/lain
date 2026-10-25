// The sensor model (sensor.h; CONTEXT.md, "Sensor model"; ADR-0029): exact noise, an exact recording
// of the light without it, and noise of the size a datasheet's electrons say.

#include <lain/core/sha256.h>
#include <lain/core/uuid.h>
#include <lain/image/color.h>
#include <lain/image/image.h>
#include <lain/image/imageview.h>
#include <lain/image/transfercurve.h>
#include <lain/rigplan/sensor.h>
#include <lain/rigplan/specification.h>
#include <lain/testing/threadpool.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using lain::rigplan::NoiseKey;
using lain::rigplan::OutputFormat;
using lain::rigplan::Recorder;
using lain::rigplan::SensorChannels;
using lain::rigplan::SensorModel;

static const lain::core::Uuid kCamera = *lain::core::Uuid::parse("01928f3e-6b2a-7c4d-8e5f-a1b2c3d4e5f6");
static const lain::core::Uuid kOtherCamera = *lain::core::Uuid::parse("01928f3e-6b2a-7c4d-8e5f-a1b2c3d4e5f7");

static Recorder recorderOf(const SensorModel& model)
{
	const auto recorder = Recorder::create(model);
	REQUIRE(recorder.has_value());
	return *recorder;
}

// A light of one value on every channel, linear, as a capture returns it.
static lain::image::Image flatLight(int width, int height, float value)
{
	lain::image::Image light(width, height, lain::image::PixelFormat::RGBA32F, lain::image::ColorSpace::Linear);
	auto view = light.as<lain::image::ColorRGBAf>();
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
			view(x, y) = lain::image::ColorRGBAf(value, value, value, 1.0f);
	}
	return light;
}

// Light that varies by pixel and channel, in values a float holds exactly, for the pinned recordings.
static lain::image::Image patternLight(int width, int height)
{
	lain::image::Image light(width, height, lain::image::PixelFormat::RGBA32F, lain::image::ColorSpace::Linear);
	auto view = light.as<lain::image::ColorRGBAf>();
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			float c[3];
			for (int k = 0; k < 3; ++k)
				c[k] = float((x * 7 + y * 13 + k * 29) % 97) / 64.0f; // up to 1.5: some of it saturates
			view(x, y) = lain::image::ColorRGBAf(c[0], c[1], c[2], 1.0f);
		}
	}
	return light;
}

static std::string digestOf(const lain::image::Image& image)
{
	return lain::core::sha256(image.data(), image.byteSize()).toString();
}

static std::uint32_t codeAt(const lain::image::Image& image, std::size_t sample)
{
	if (image.formatDescriptor().channelType == lain::image::ChannelType::U16)
		return reinterpret_cast<const std::uint16_t*>(image.data())[sample];
	return image.data()[sample];
}

// --- the noise --------------------------------------------------------------------------------

TEST_CASE("a camera-frame's noise is pinned outside lain", "[rigplan][sensor]")
{
	// Computed by Python's hashlib and integer arithmetic from the text and the SplitMix64 / Irwin-Hall
	// rule sensor.h states, so the draws are the same on every platform and any change to them is a
	// change of the versioned name.
	const std::uint64_t seed = lain::rigplan::noiseSeed(NoiseKey{7, kCamera, 3});
	REQUIRE(seed == 0x6fb29c25566225f6ull);
	REQUIRE(lain::rigplan::standardNormal(seed, 0) == -0x1.0fe770d3p+0);
	REQUIRE(lain::rigplan::standardNormal(seed, 1) == -0x1.69098e44p-1);
	REQUIRE(lain::rigplan::standardNormal(seed, 2) == 0x1.2ca9bffcp-2);
	REQUIRE(lain::rigplan::standardNormal(seed, 1000000) == 0x1.d57746ecp-1);
}

TEST_CASE("a noise seed belongs to one seed, camera and frame", "[rigplan][sensor]")
{
	const std::uint64_t base = lain::rigplan::noiseSeed(NoiseKey{7, kCamera, 3});
	REQUIRE(lain::rigplan::noiseSeed(NoiseKey{8, kCamera, 3}) != base);
	REQUIRE(lain::rigplan::noiseSeed(NoiseKey{7, kOtherCamera, 3}) != base);
	REQUIRE(lain::rigplan::noiseSeed(NoiseKey{7, kCamera, 4}) != base);
	REQUIRE(lain::rigplan::noiseSeed(NoiseKey{7, kCamera, 3}) == base);
}

static double correlation(const std::vector<double>& a, const std::vector<double>& b)
{
	double ma = 0, mb = 0;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		ma += a[i];
		mb += b[i];
	}
	ma /= double(a.size());
	mb /= double(b.size());
	double sab = 0, saa = 0, sbb = 0;
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		sab += (a[i] - ma) * (b[i] - mb);
		saa += (a[i] - ma) * (a[i] - ma);
		sbb += (b[i] - mb) * (b[i] - mb);
	}
	return sab / std::sqrt(saa * sbb);
}

TEST_CASE("noise draws are a standard normal, independent of one another", "[rigplan][sensor]")
{
	const std::size_t n = 200000;
	const std::uint64_t seed = lain::rigplan::noiseSeed(NoiseKey{7, kCamera, 3});
	std::vector<double> z(n + 1);
	double mean = 0, peak = 0;
	for (std::size_t i = 0; i <= n; ++i)
	{
		z[i] = lain::rigplan::standardNormal(seed, i);
		mean += z[i];
		peak = std::max(peak, std::abs(z[i]));
	}
	mean /= double(n + 1);
	double variance = 0;
	for (double v : z)
		variance += (v - mean) * (v - mean);
	variance /= double(n);

	// A mean of 200001 draws has a standard deviation of 0.0022, and their variance one of 0.0032.
	REQUIRE(std::abs(mean) < 0.011);
	REQUIRE(std::abs(variance - 1.0) < 0.016);
	REQUIRE(peak <= 6.0); // Irwin-Hall's bound

	// Neighbouring samples, the same sample in the next frame, and in another camera, are unrelated: a
	// correlation of independent draws has a standard deviation of 1 / sqrt(n), 0.0022.
	const std::vector<double> here(z.begin(), z.end() - 1);
	const std::vector<double> next(z.begin() + 1, z.end());
	REQUIRE(std::abs(correlation(here, next)) < 0.011);

	const std::uint64_t nextFrame = lain::rigplan::noiseSeed(NoiseKey{7, kCamera, 4});
	const std::uint64_t otherCamera = lain::rigplan::noiseSeed(NoiseKey{7, kOtherCamera, 3});
	std::vector<double> inNextFrame(n), inOtherCamera(n);
	for (std::size_t i = 0; i < n; ++i)
	{
		inNextFrame[i] = lain::rigplan::standardNormal(nextFrame, i);
		inOtherCamera[i] = lain::rigplan::standardNormal(otherCamera, i);
	}
	REQUIRE(std::abs(correlation(here, inNextFrame)) < 0.011);
	REQUIRE(std::abs(correlation(here, inOtherCamera)) < 0.011);
}

// --- a recording ------------------------------------------------------------------------------

TEST_CASE("without noise a sensor records the light exactly", "[rigplan][sensor]")
{
	// A full well of 4096 electrons and a 12-bit ADC make every step below exact: light k / 4096 is k
	// electrons, and k electrons are ADC level floor(k * 4095 / 4096 + 0.5).
	SensorModel sensor;
	sensor.fullWellElectrons = 4096;
	sensor.output.bits = 16;
	sensor.output.curve = lain::image::ColorSpace::Linear;
	const Recorder recorder = recorderOf(sensor);

	const int width = 5000;
	lain::image::Image light(width, 1, lain::image::PixelFormat::RGBA32F, lain::image::ColorSpace::Linear);
	auto view = light.as<lain::image::ColorRGBAf>();
	for (int k = 0; k < width; ++k)
		view(k, 0) = lain::image::ColorRGBAf(float(k) / 4096.0f, -float(k), std::numeric_limits<float>::quiet_NaN(), 0.0f);

	const lain::image::Image recorded = recorder.record(light, NoiseKey{7, kCamera, 0}, 0.0);
	REQUIRE(recorded.pixelFormat() == lain::image::PixelFormat::RGB16);
	REQUIRE(recorded.colorSpace() == lain::image::ColorSpace::Linear);
	const std::vector<std::uint16_t>& codes = recorder.outputCodes();
	for (int k = 0; k < width; ++k)
	{
		const std::uint32_t adc = std::min<std::uint32_t>((std::uint32_t(k) * 4095u + 2048u) / 4096u, 4095u);
		REQUIRE(codeAt(recorded, std::size_t(k) * 3 + 0) == codes[adc]);
		REQUIRE(codeAt(recorded, std::size_t(k) * 3 + 1) == codes[0]); // negative light records nothing
		REQUIRE(codeAt(recorded, std::size_t(k) * 3 + 2) == codes[0]); // and so does light that is not a number
	}
}

TEST_CASE("a sensor's output codes are its curve, and pinned", "[rigplan][sensor]")
{
	SensorModel sensor; // a 12-bit ADC written as 8-bit sRGB
	const Recorder recorder = recorderOf(sensor);
	const std::vector<std::uint16_t>& codes = recorder.outputCodes();
	REQUIRE(codes.size() == 4096);
	REQUIRE(codes.front() == 0);
	REQUIRE(codes.back() == 255);
	for (std::size_t i = 1; i < codes.size(); ++i)
		REQUIRE(codes[i] >= codes[i - 1]);
	// Mid-grey in linear light is code 188 in sRGB, as 4c found a half-covered pixel reads.
	REQUIRE(codes[2048] == 188);

	// Each table is a fixed set of values, so pinning its bytes is what proves the curve's pow agrees on
	// every platform: the one part of a recording that is not exact IEEE arithmetic.
	const auto digest = [](const SensorModel& r)
	{
		const Recorder recorder = recorderOf(r); // held: its table is the recorder's own
		const std::vector<std::uint16_t>& table = recorder.outputCodes();
		return lain::core::sha256(table.data(), table.size() * sizeof(std::uint16_t)).toString();
	};
	CHECK(digest(sensor) == "232e1b3a6fd859b73b3df3631bb71a8a7b8fd470f8509275da34b087bcc1c33a");

	SensorModel linear16;
	linear16.output.bits = 16;
	linear16.output.curve = lain::image::ColorSpace::Linear;
	CHECK(digest(linear16) == "50aec0dfe1c8ae5a9a23a5cb59f11cd57c378e12993589d8251d8a1d6327ca41");

	SensorModel rec709;
	rec709.adcBits = 10;
	rec709.output.curve = lain::image::ColorSpace::BT709;
	CHECK(digest(rec709) == "6745919096f9c0a30823272ca59c5b560982ee74ad531933b1f008d936257722");

	SensorModel srgb16;
	srgb16.adcBits = 16;
	srgb16.output.bits = 16;
	CHECK(digest(srgb16) == "b1d82bc4a32b08e128299f585c5d0e4f202a25168368b9969605c6956f9e1493");
}

TEST_CASE("shot and read noise add in quadrature, at their scale", "[rigplan][sensor]")
{
	// A small full well and a fine ADC, so the noise is many codes wide and quantisation is negligible:
	// a 16-bit ADC level is 1000 / 65535 electrons, written as itself (16-bit linear).
	SensorModel sensor;
	sensor.fullWellElectrons = 1000;
	sensor.readNoiseElectrons = 5;
	sensor.adcBits = 16;
	sensor.output.bits = 16;
	sensor.output.curve = lain::image::ColorSpace::Linear;
	const Recorder recorder = recorderOf(sensor);

	for (const double level : {0.05, 0.3, 0.8})
	{
		for (const double scale : {1.0, 0.5})
		{
			const lain::image::Image recorded = recorder.record(flatLight(256, 256, float(level)), NoiseKey{3, kCamera, 1}, scale);
			const std::size_t samples = 256 * 256 * 3;
			double mean = 0;
			std::vector<double> electrons(samples);
			for (std::size_t i = 0; i < samples; ++i)
			{
				electrons[i] = double(codeAt(recorded, i)) * 1000.0 / 65535.0;
				mean += electrons[i];
			}
			mean /= double(samples);
			double variance = 0;
			for (double e : electrons)
				variance += (e - mean) * (e - mean);
			variance /= double(samples - 1);

			const double expected = level * 1000;
			const double expectedVariance = scale * scale * (expected + 25.0);
			INFO("level " << level << ", scale " << scale << ": mean " << mean << ", variance " << variance);
			// Within five standard errors of the mean.
			REQUIRE(std::abs(mean - expected) < 5.0 * std::sqrt(expectedVariance / double(samples)));
			// 196608 samples measure a variance to 0.3%.
			REQUIRE(std::abs(variance / expectedVariance - 1.0) < 0.02);
		}
	}
}

TEST_CASE("each channel draws its own noise", "[rigplan][sensor]")
{
	SensorModel sensor;
	sensor.fullWellElectrons = 1000;
	sensor.adcBits = 16;
	sensor.output.bits = 16;
	sensor.output.curve = lain::image::ColorSpace::Linear;
	const lain::image::Image recorded = recorderOf(sensor).record(flatLight(256, 256, 0.5f), NoiseKey{3, kCamera, 1}, 1.0);
	std::vector<double> red, green;
	for (std::size_t p = 0; p < 256 * 256; ++p)
	{
		red.push_back(double(codeAt(recorded, p * 3 + 0)));
		green.push_back(double(codeAt(recorded, p * 3 + 1)));
	}
	REQUIRE(std::abs(correlation(red, green)) < 0.02);
}

TEST_CASE("a sensor saturates at its full well", "[rigplan][sensor]")
{
	SensorModel sensor; // 10000 electrons: noise at twice full scale is some 140 electrons
	const Recorder recorder = recorderOf(sensor);
	for (const float value : {2.0f, 1.0e30f, std::numeric_limits<float>::infinity()})
	{
		const lain::image::Image recorded = recorder.record(flatLight(64, 64, value), NoiseKey{3, kCamera, 1}, 1.0);
		for (std::size_t i = 0; i < 64 * 64 * 3; ++i)
			REQUIRE(codeAt(recorded, i) == 255);
	}
}

TEST_CASE("a grey sensor records luminance", "[rigplan][sensor]")
{
	SensorModel sensor;
	sensor.channels = SensorChannels::Grey;
	sensor.fullWellElectrons = 4096;
	const Recorder recorder = recorderOf(sensor);
	REQUIRE(recorder.pixelFormat() == lain::image::PixelFormat::Gray8);

	lain::image::Image light(3, 1, lain::image::PixelFormat::RGBA32F, lain::image::ColorSpace::Linear);
	auto view = light.as<lain::image::ColorRGBAf>();
	view(0, 0) = lain::image::ColorRGBAf(1.0f, 0.0f, 0.0f, 1.0f);
	view(1, 0) = lain::image::ColorRGBAf(0.0f, 1.0f, 0.0f, 1.0f);
	view(2, 0) = lain::image::ColorRGBAf(0.0f, 0.0f, 1.0f, 1.0f);
	const lain::image::Image recorded = recorder.record(light, NoiseKey{3, kCamera, 1}, 0.0);
	REQUIRE(recorded.colorSpace() == lain::image::ColorSpace::sRGB);
	const std::vector<std::uint16_t>& codes = recorder.outputCodes();
	REQUIRE(codeAt(recorded, 0) == codes[std::size_t(std::floor(0.2126 * 4095 + 0.5))]);
	REQUIRE(codeAt(recorded, 1) == codes[std::size_t(std::floor(0.7152 * 4095 + 0.5))]);
	REQUIRE(codeAt(recorded, 2) == codes[std::size_t(std::floor(0.0722 * 4095 + 0.5))]);
}

TEST_CASE("a recording is the same however many threads make it", "[rigplan][sensor]")
{
	const Recorder recorder = recorderOf(SensorModel{});
	const lain::image::Image light = patternLight(333, 97);
	const lain::image::Image alone = recorder.record(light, NoiseKey{7, kCamera, 3}, 1.0);
	lain::image::Image pooled;
	{
		lain::testing::ThreadPool pool(4);
		pooled = recorder.record(light, NoiseKey{7, kCamera, 3}, 1.0);
	}
	REQUIRE(alone.byteSize() == pooled.byteSize());
	REQUIRE(std::memcmp(alone.data(), pooled.data(), alone.byteSize()) == 0);
}

TEST_CASE("a whole recording is pinned", "[rigplan][sensor]")
{
	// The cross-platform claim: one light, recorded with its noise, gives these bytes on every machine.
	// The light is exact floats, every step after it exact IEEE arithmetic (sensor.cpp is compiled
	// without contraction), and the curve a table the test above pins.
	const lain::image::Image light = patternLight(64, 48);
	const lain::image::Image rgb = recorderOf(SensorModel{}).record(light, NoiseKey{7, kCamera, 3}, 1.0);
	REQUIRE(rgb.pixelFormat() == lain::image::PixelFormat::RGB8);
	CHECK(digestOf(rgb) == "c48a29f1c752c182959e67f909608bbe8ac54ee3c7179c9b2f6946e60f5b9ce7");

	SensorModel grey;
	grey.channels = SensorChannels::Grey;
	grey.fullWellElectrons = 2500;
	grey.readNoiseElectrons = 7.5;
	grey.gain = 0.8;
	grey.output.bits = 16;
	grey.output.curve = lain::image::ColorSpace::Linear;
	const lain::image::Image deep = recorderOf(grey).record(light, NoiseKey{7, kCamera, 3}, 0.75);
	REQUIRE(deep.pixelFormat() == lain::image::PixelFormat::Gray16);
	CHECK(digestOf(deep) == "df0879cf71bbcbbdf220ade3a8bdce7184a89d2782424816cee9d95e37c32720");
}

TEST_CASE("a recording refuses light it cannot read", "[rigplan][sensor]")
{
	const Recorder recorder = recorderOf(SensorModel{});
	REQUIRE_FALSE(recorder.record(lain::image::Image{}, NoiseKey{}, 0.0).valid());
	REQUIRE_FALSE(recorder.record(lain::image::Image(4, 4, lain::image::PixelFormat::RGBA8, lain::image::ColorSpace::Linear), NoiseKey{}, 0.0).valid());
	REQUIRE_FALSE(recorder.record(lain::image::Image(4, 4, lain::image::PixelFormat::RGBA32F, lain::image::ColorSpace::sRGB), NoiseKey{}, 0.0).valid());
}

// --- a sensor model's problems ----------------------------------------------------------------

TEST_CASE("a sensor model that cannot record is refused", "[rigplan][sensor]")
{
	REQUIRE(lain::rigplan::sensorModelProblems(SensorModel{}).empty());

	const auto refused = [](SensorModel r)
	{
		REQUIRE_FALSE(Recorder::create(r).has_value());
		return lain::rigplan::sensorModelProblems(r).size();
	};
	SensorModel r;
	r.gain = 0;
	REQUIRE(refused(r) == 1);
	r = {};
	r.fullWellElectrons = std::numeric_limits<double>::infinity();
	REQUIRE(refused(r) == 1);
	r = {};
	r.readNoiseElectrons = -1;
	REQUIRE(refused(r) == 1);
	r = {};
	r.adcBits = 0;
	REQUIRE(refused(r) == 1);
	r.adcBits = 17;
	REQUIRE(refused(r) == 1);
	r = {};
	r.output.bits = 12;
	REQUIRE(refused(r) == 1);
	r = {};
	r.output.format = OutputFormat::Jpeg;
	r.output.bits = 16;
	REQUIRE(refused(r) == 1);
	r = {};
	r.output.curve = lain::image::ColorSpace::Unspecified;
	REQUIRE(refused(r) == 1);
	r = {};
	r.output.quality = 90; // on a PNG, which has none
	REQUIRE(refused(r) == 1);
	r.output.format = OutputFormat::Jpeg;
	REQUIRE(lain::rigplan::sensorModelProblems(r).empty());
	r.output.quality = 0;
	REQUIRE(refused(r) == 1);

	// And check() names the sensor.
	lain::rigplan::Specifications specifications;
	lain::rigplan::SensorSpecification sensor;
	sensor.name = "mono";
	sensor.image = {64, 48};
	sensor.pixelPitch = lain::core::Length::from<lain::core::Length::Micrometres>(3.0);
	sensor.sensorModel.adcBits = 20;
	specifications.sensors.push_back(sensor);
	const auto diagnostics = lain::rigplan::check(specifications);
	REQUIRE(diagnostics.size() == 1);
	REQUIRE(diagnostics[0].problem == lain::rigplan::SpecificationProblem::InvalidSensorModel);
	REQUIRE(diagnostics[0].detail.find("'mono'") != std::string::npos);
}
