#include "examples.h"

#include <lain/core/uri.h>
#include <lain/image/colorspace.h>
#include <lain/image/convert.h>
#include <lain/image/image.h>
#include <lain/image/pixelformat.h>
#include <lain/io/image/save.h>
#include <lain/io/video/save.h>
#include <lain/io/video/writer.h>
#include <lain/media/framespec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

// The data sets the media examples read. All of it is synthetic and made here, so its provenance
// needs no licence and it can be regenerated anywhere lain builds. The committed copy is what the
// examples use; this is rerun only to change it (`--data`).
namespace flowview::examples
{
	using namespace lain;

	static constexpr std::uint32_t kWidth = 160;
	static constexpr std::uint32_t kHeight = 90;
	static constexpr int kShotFrames = 24;

	struct Rgb
	{
		std::uint8_t r, g, b;
	};

	[[noreturn]] static void fail(const std::string& what)
	{
		throw std::runtime_error(what);
	}

	static image::Image blank()
	{
		return image::Image{kWidth, kHeight, image::PixelFormat::RGB8, image::ColorSpace::sRGB};
	}

	static void put(image::Image& image, int x, int y, Rgb c)
	{
		if (x < 0 || y < 0 || x >= static_cast<int>(image.width()) || y >= static_cast<int>(image.height()))
			return;
		std::uint8_t* px = image.data() + (static_cast<std::size_t>(y) * image.width() + static_cast<std::size_t>(x)) * 3;
		px[0] = c.r;
		px[1] = c.g;
		px[2] = c.b;
	}

	static void rect(image::Image& image, int x, int y, int w, int h, Rgb c)
	{
		for (int j = y; j < y + h; ++j)
		{
			for (int i = x; i < x + w; ++i)
				put(image, i, j, c);
		}
	}

	static std::uint8_t mix(int from, int to, float t)
	{
		return static_cast<std::uint8_t>(std::lround(from + (to - from) * std::clamp(t, 0.0f, 1.0f)));
	}

	// A digit in seven segments, in a 12x20 cell, so a frame's number can be read off the frame
	// itself: which is how "frame 7 is frame 7" is eyeballed through a clip, a seek or a decode.
	static void digit(image::Image& image, int x, int y, int value, Rgb c)
	{
		//                                     a  b  c  d  e  f  g
		static constexpr bool segments[10][7] = {{1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1}, {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1}, {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 0, 1, 1}};
		const bool* on = segments[value % 10];
		constexpr int w = 12;
		constexpr int h = 20;
		constexpr int t = 3;
		if (on[0])
			rect(image, x, y, w, t, c); // a: top
		if (on[1])
			rect(image, x + w - t, y, t, h / 2, c); // b: upper right
		if (on[2])
			rect(image, x + w - t, y + h / 2, t, h / 2, c); // c: lower right
		if (on[3])
			rect(image, x, y + h - t, w, t, c); // d: bottom
		if (on[4])
			rect(image, x, y + h / 2, t, h / 2, c); // e: lower left
		if (on[5])
			rect(image, x, y, t, h / 2, c); // f: upper left
		if (on[6])
			rect(image, x, y + (h - t) / 2, w, t, c); // g: middle
	}

	// The four stills: one extent and format, distinct enough to tell apart in a map's element
	// stepper and to make an average visibly an average.
	static image::Image still(int which)
	{
		image::Image image = blank();
		for (std::uint32_t y = 0; y < kHeight; ++y)
		{
			for (std::uint32_t x = 0; x < kWidth; ++x)
			{
				const float u = static_cast<float>(x) / (kWidth - 1);
				const float v = static_cast<float>(y) / (kHeight - 1);
				const int ix = static_cast<int>(x);
				const int iy = static_cast<int>(y);
				Rgb c{};
				switch (which)
				{
					case 0: // a: a warm disc on a dark ramp
					{
						const float dx = u - 0.5f;
						const float dy = (v - 0.5f) * kHeight / kWidth;
						const bool disc = dx * dx + dy * dy < 0.04f;
						c = disc ? Rgb{250, 180, 60} : Rgb{mix(20, 60, v), 20, mix(40, 90, v)};
						break;
					}
					case 1: // b: green diagonal stripes
						c = ((ix + iy) / 12) % 2 == 0 ? Rgb{40, 190, 90} : Rgb{10, 60, 30};
						break;
					case 2: // c: a blue checkerboard
						c = ((ix / 16) + (iy / 16)) % 2 == 0 ? Rgb{60, 120, 230} : Rgb{230, 235, 245};
						break;
					default: // d: magenta rings
					{
						const float dx = u - 0.5f;
						const float dy = (v - 0.5f) * kHeight / kWidth;
						const int ring = static_cast<int>(std::sqrt(dx * dx + dy * dy) * 40.0f);
						c = ring % 2 == 0 ? Rgb{200, 40, 160} : Rgb{50, 10, 45};
						break;
					}
				}
				put(image, ix, iy, c);
			}
		}
		return image;
	}

	// Frame `n` (0-based) of the shot: a ramp, a block moving right, and the 1-based number of the
	// FILE burned in at the top left, since a still's number is its identity in a sequence.
	static image::Image shotFrame(int n)
	{
		image::Image image = blank();
		for (std::uint32_t y = 0; y < kHeight; ++y)
		{
			for (std::uint32_t x = 0; x < kWidth; ++x)
			{
				const float u = static_cast<float>(x) / (kWidth - 1);
				put(image, static_cast<int>(x), static_cast<int>(y), Rgb{mix(10, 30, u), mix(40, 120, u), mix(90, 140, u)});
			}
		}
		rect(image, 12 + n * 5, 50, 20, 20, Rgb{245, 245, 245});
		const int number = n + 1;
		digit(image, 6, 6, number / 10, Rgb{255, 210, 40});
		digit(image, 22, 6, number % 10, Rgb{255, 210, 40});
		return image;
	}

	static void save(const std::filesystem::path& file, const image::Image& image)
	{
		if (!io::image::save(core::Uri::fromPath(file), image))
			fail("could not write " + file.string());
	}

	static void folder(const std::filesystem::path& path)
	{
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
		std::filesystem::create_directories(path, ec);
		if (ec)
			fail("could not create " + path.string());
	}

	void writeData(const std::filesystem::path& examples)
	{
		const std::filesystem::path data = examples / "data";

		folder(data / "stills");
		for (int i = 0; i < 4; ++i)
			save(data / "stills" / (std::string(1, static_cast<char>('a' + i)) + ".png"), still(i));

		folder(data / "shot");
		for (int n = 0; n < kShotFrames; ++n)
		{
			std::string name = std::to_string(n + 1);
			name = "shot." + std::string(4 - name.size(), '0') + name + ".png";
			save(data / "shot" / name, shotFrame(n));
		}

		// Three readable stills and one file that only claims to be a PNG: the map's hole.
		folder(data / "holes");
		for (int i = 0; i < 3; ++i)
			save(data / "holes" / (std::string(1, static_cast<char>('a' + i)) + ".png"), still(i));
		std::ofstream(data / "holes" / "broken.png", std::ios::binary) << "this is not a PNG";

		// The same shot as video, for the video example. Encoded only when the build has a writer; a
		// build without one leaves the committed file alone rather than deleting it.
		media::FrameSpec spec;
		spec.extent = {static_cast<int>(kWidth), static_cast<int>(kHeight)};
		spec.pixelFormat = image::PixelFormat::RGB8;
		spec.colorSpace = image::ColorSpace::BT709;
		spec.rate = {24, 1};
		const core::Uri clip = core::Uri::fromPath(data / "shot.mp4");
		std::unique_ptr<io::video::VideoWriter> writer = io::video::openWriter(clip, spec);
		if (!writer)
			return;
		for (int n = 0; n < kShotFrames; ++n)
		{
			// Video is BT.709, so the frame is converted, not relabelled.
			if (!writer->encode(image::convert(shotFrame(n), image::ColorSpace::BT709)))
				fail("could not encode frame " + std::to_string(n) + " of shot.mp4");
		}
		if (!writer->finish())
			fail("could not finish shot.mp4");
	}
} // namespace flowview::examples
