#pragma once

// Synthetic footage of a rendered board, drawn in plain C++ with no OpenCV: a known pinhole camera
// looking at the production render along a sweep of poses. Shared by the tests that need real frames
// on disk without a library of their own to draw them (flowview's cli vertical, the fixture harness).
//
// Each pixel is the mean of 3x3 rays, each met with the board plane and sampled bilinearly from the
// raster, because aliased markers bias every corner: nearest sampling put a recovered fx 0.33% out.

#include <lain/camera/board/rendering.h>
#include <lain/camera/board/specification.h>
#include <lain/image/image.h>
#include <lain/math/rigidtransform.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lain::camera::synthetic
{
	// The camera the footage is seen through.
	struct Pinhole
	{
		int width = 640;
		int height = 480;
		double fx = 600.0, fy = 602.0, cx = 321.4, cy = 238.6;
	};

	// The board at pose `i` of `count`: a sweep across the image that tilts it every way. `phase`
	// shifts the sweep, so two sessions of one camera see the board from different poses.
	inline math::RigidTransformd sweepPose(const board::Specification& spec, std::size_t i, std::size_t count,
										   double phase = 0.0, double distance = 0.42)
	{
		const double t = double(i) / double(count) + phase;
		const double tau = 6.283185307179586;
		const board::PatternParameters& p = spec.pattern().parameters();
		const double square = spec.instance().squareLength.value.metres();
		const math::Vec3d centre{p.squaresX * square / 2, p.squaresY * square / 2, 0.0};
		const math::Quatd turn = math::angleAxis(0.5 * std::sin(tau * t), math::Vec3d{0, 1, 0}) *
								 math::angleAxis(0.4 * std::cos(2 * tau * t), math::Vec3d{1, 0, 0});
		const math::Vec3d offset{0.13 * std::cos(3 * tau * t), 0.09 * std::sin(3 * tau * t), distance};
		return math::RigidTransformd{turn, offset - turn * centre};
	}

	// The raster at (x, y), bilinearly, on mid grey beyond its edges.
	inline double sample(const image::Image& raster, double x, double y)
	{
		const auto at = [&raster](long px, long py) -> double
		{
			if (px < 0 || py < 0 || px >= raster.width() || py >= raster.height())
				return 128;
			return raster.data()[std::size_t(py) * std::size_t(raster.width()) + std::size_t(px)];
		};
		const long x0 = long(std::floor(x)), y0 = long(std::floor(y));
		const double fx = x - double(x0), fy = y - double(y0);
		return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
			   (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
	}

	// What `camera` sees of the rendered board at `pose`, on mid grey, as 8-bit grey.
	inline image::Image view(const board::Rendering& rendering, const board::Specification& spec, const Pinhole& camera,
							 const math::RigidTransformd& pose)
	{
		const image::Image& raster = rendering.raster;
		// Board metres -> raster pixel. The board's top-left corner is raster coordinate margin - 0.5,
		// since pixel (0, 0) is the centre of the first pixel.
		const double perMetre = rendering.request.pixelsPerSquare / spec.instance().squareLength.value.metres();
		const double origin = double(rendering.request.marginPixels) - 0.5;

		// A ray d (camera frame) meets the plane where b = R^T (lambda d - t) has b.z = 0.
		const math::Mat3d r = math::mat3_cast(pose.rotation());
		const math::Vec3d t = pose.translation();
		const math::Vec3d c{math::dot(r[0], t), math::dot(r[1], t), math::dot(r[2], t)}; // R^T t

		constexpr int s = 3;
		image::Image frame{camera.width, camera.height, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		for (int v = 0; v < camera.height; ++v)
		{
			for (int u = 0; u < camera.width; ++u)
			{
				double sum = 0;
				for (int j = 0; j < s; ++j)
				{
					for (int i = 0; i < s; ++i)
					{
						const math::Vec3d d{(u + (i + 0.5) / s - 0.5 - camera.cx) / camera.fx,
											(v + (j + 0.5) / s - 0.5 - camera.cy) / camera.fy, 1.0};
						const math::Vec3d q{math::dot(r[0], d), math::dot(r[1], d), math::dot(r[2], d)}; // R^T d
						double value = 128;
						if (q.z != 0 && c.z / q.z > 0)
						{
							const double lambda = c.z / q.z;
							value = sample(raster, (lambda * q.x - c.x) * perMetre + origin,
										   (lambda * q.y - c.y) * perMetre + origin);
						}
						sum += value;
					}
				}
				frame.data()[std::size_t(v) * std::size_t(camera.width) + std::size_t(u)] =
					std::uint8_t(std::lround(sum / (s * s)));
			}
		}
		return frame;
	}
} // namespace lain::camera::synthetic
