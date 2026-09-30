#pragma once

// A rendered board as a pinhole camera would see it, for the plugin's tests. Drawn 4x supersampled
// and averaged down: a single warp aliases the markers, and the aliasing shows up as a bias in every
// corner (opencv-prebuilt's probe measured 0.8% in a recovered focal length before this).

#include <lain/camera/board/rendering.h>
#include <lain/camera/board/specification.h>
#include <lain/camera/cameramodel.h>
#include <lain/camera/projection.h>
#include <lain/image/image.h>
#include <lain/math/rigidtransform.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lain::camera::testing
{
	// A 960x720 pinhole camera with distinct intrinsics.
	inline CameraModelParameters pinhole()
	{
		CameraModelParameters p;
		p.image = {960, 720};
		p.intrinsics = {900.0, 905.0, 479.5, 359.5};
		p.distortion = NoDistortion{};
		return p;
	}

	// The board's centre `distance` in front of the camera, turned by `yaw` about Y and `pitch` about
	// X (radians), so it is seen at an angle rather than squarely.
	inline math::RigidTransformd cameraFromBoard(const board::Specification& spec, double distance, double yaw,
												 double pitch)
	{
		const board::PatternParameters& p = spec.pattern().parameters();
		const double square = spec.instance().squareLength.value.metres();
		const math::Vec3d centre{p.squaresX * square / 2, p.squaresY * square / 2, 0.0};
		const math::Quatd turn = math::angleAxis(yaw, math::Vec3d{0, 1, 0}) * math::angleAxis(pitch, math::Vec3d{1, 0, 0});
		return math::RigidTransformd{turn, math::Vec3d{0, 0, distance} - turn * centre};
	}

	// What the camera sees: the board on mid grey, as 8-bit grey.
	inline image::Image view(const board::Rendering& rendering, const board::Specification& spec,
							 const CameraModelParameters& camera, const math::RigidTransformd& cameraFromBoard)
	{
		const double square = spec.instance().squareLength.value.metres();
		const double pps = rendering.request.pixelsPerSquare;
		const double margin = rendering.request.marginPixels;

		// Raster pixel -> board metres. The board's top-left corner is raster coordinate margin - 0.5,
		// since pixel (0, 0) is the centre of the first pixel.
		const double k = square / pps;
		const cv::Matx33d rasterToBoard(k, 0, -(margin - 0.5) * k, 0, k, -(margin - 0.5) * k, 0, 0, 1);
		// Board plane (Z = 0) -> camera pixel: K [r1 r2 t].
		const math::Mat3d r = math::mat3_cast(cameraFromBoard.rotation());
		const math::Vec3d t = cameraFromBoard.translation();
		const Intrinsics& in = camera.intrinsics;
		const cv::Matx33d planeToCamera(r[0][0], r[1][0], t.x, r[0][1], r[1][1], t.y, r[0][2], r[1][2], t.z);
		const cv::Matx33d intrinsic(in.fx, 0, in.cx, 0, in.fy, in.cy, 0, 0, 1);
		// Native pixel -> supersampled pixel: centre u maps to 4u + 1.5.
		const int s = 4;
		const cv::Matx33d supersample(s, 0, (s - 1) / 2.0, 0, s, (s - 1) / 2.0, 0, 0, 1);
		const cv::Matx33d h = supersample * intrinsic * planeToCamera * rasterToBoard;

		const image::Image& raster = rendering.raster;
		const cv::Mat source(raster.height(), raster.width(), CV_8UC1, const_cast<std::uint8_t*>(raster.data()));
		const int w = int(camera.image.width), hgt = int(camera.image.height);
		cv::Mat big, small;
		cv::warpPerspective(source, big, h, cv::Size(w * s, hgt * s), cv::INTER_LINEAR, cv::BORDER_CONSTANT, 128);
		cv::resize(big, small, cv::Size(w, hgt), 0, 0, cv::INTER_AREA);

		image::Image out{w, hgt, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		for (int row = 0; row < hgt; ++row)
			std::memcpy(out.data() + std::size_t(row) * std::size_t(w), small.ptr<std::uint8_t>(row), std::size_t(w));
		return out;
	}

	// The board at pose `i` of `count`: a sweep across the image that tilts it every way, the variety
	// view selection looks for.
	inline math::RigidTransformd sweepPose(const board::Specification& spec, std::size_t i, std::size_t count,
										   double distance = 0.45)
	{
		const double t = double(i) / double(count);
		const double tau = 6.283185307179586;
		const board::PatternParameters& p = spec.pattern().parameters();
		const double square = spec.instance().squareLength.value.metres();
		const math::Vec3d centre{p.squaresX * square / 2, p.squaresY * square / 2, 0.0};
		const math::Quatd turn = math::angleAxis(0.5 * std::sin(tau * t), math::Vec3d{0, 1, 0}) *
								 math::angleAxis(0.4 * std::cos(2 * tau * t), math::Vec3d{1, 0, 0});
		const math::Vec3d offset{0.13 * std::cos(3 * tau * t), 0.09 * std::sin(3 * tau * t), distance};
		return math::RigidTransformd{turn, offset - turn * centre};
	}

	// What a camera WITH lens distortion sees: the board drawn as a pinhole image on a canvas larger
	// than the frame (a distorted frame's corners look further out than a pinhole's), then remapped
	// through the camera's distortion, supersampled 2x and averaged down. The remap is lain's own
	// unprojection, evaluated on a coarse grid and interpolated, since distortion is smooth and a
	// per-pixel inverse at this resolution is too slow for a debug build.
	inline image::Image distortedView(const board::Rendering& rendering, const board::Specification& spec,
									  const CameraModel& camera, const math::RigidTransformd& cameraFromBoard)
	{
		constexpr int s = 2;
		const int w = int(camera.image().width), h = int(camera.image().height);
		const int sw = w * s, sh = h * s;
		const int marginX = sw / 3, marginY = sh / 3; // the canvas extends a third of the frame each way
		const Intrinsics& in = camera.intrinsics();

		// The pinhole canvas: raster pixel -> board metres -> camera -> supersampled canvas pixel.
		const double square = spec.instance().squareLength.value.metres();
		const double k = square / rendering.request.pixelsPerSquare;
		const double m = rendering.request.marginPixels;
		const cv::Matx33d rasterToBoard(k, 0, -(m - 0.5) * k, 0, k, -(m - 0.5) * k, 0, 0, 1);
		const math::Mat3d r = math::mat3_cast(cameraFromBoard.rotation());
		const math::Vec3d t = cameraFromBoard.translation();
		const cv::Matx33d planeToCamera(r[0][0], r[1][0], t.x, r[0][1], r[1][1], t.y, r[0][2], r[1][2], t.z);
		const double half = (s - 1) / 2.0; // native centre u sits at supersampled s*u + half
		const cv::Matx33d toCanvas(s * in.fx, 0, s * in.cx + half + marginX, 0, s * in.fy, s * in.cy + half + marginY, 0,
								   0, 1);
		const image::Image& raster = rendering.raster;
		const cv::Mat source(raster.height(), raster.width(), CV_8UC1, const_cast<std::uint8_t*>(raster.data()));
		cv::Mat canvas;
		cv::warpPerspective(source, canvas, toCanvas * planeToCamera * rasterToBoard,
							cv::Size(sw + 2 * marginX, sh + 2 * marginY), cv::INTER_LINEAR, cv::BORDER_CONSTANT, 128);

		// Where each supersampled frame pixel looks on the canvas, on a grid every `step` pixels.
		constexpr int step = 8;
		const int gw = sw / step + 2, gh = sh / step + 2;
		std::vector<float> gx(std::size_t(gw * gh)), gy(std::size_t(gw * gh));
		for (int j = 0; j < gh; ++j)
		{
			for (int i = 0; i < gw; ++i)
			{
				const double u = (i * step - half) / s, v = (j * step - half) / s;
				const Unprojection<double> ray = unproject(camera, u, v);
				const double pu = ray.ok() ? in.fx * ray.x / ray.z + in.cx : -1e4;
				const double pv = ray.ok() ? in.fy * ray.y / ray.z + in.cy : -1e4;
				gx[std::size_t(j * gw + i)] = float(s * pu + half + marginX);
				gy[std::size_t(j * gw + i)] = float(s * pv + half + marginY);
			}
		}
		cv::Mat mapX(sh, sw, CV_32FC1), mapY(sh, sw, CV_32FC1);
		for (int y = 0; y < sh; ++y)
		{
			const int j = y / step;
			const float fy = float(y - j * step) / step;
			for (int x = 0; x < sw; ++x)
			{
				const int i = x / step;
				const float fx = float(x - i * step) / step;
				const auto lerp = [&](const std::vector<float>& g)
				{
					const float a = g[std::size_t(j * gw + i)], b = g[std::size_t(j * gw + i + 1)];
					const float c = g[std::size_t((j + 1) * gw + i)], d = g[std::size_t((j + 1) * gw + i + 1)];
					return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
				};
				mapX.at<float>(y, x) = lerp(gx);
				mapY.at<float>(y, x) = lerp(gy);
			}
		}
		cv::Mat big, small;
		cv::remap(canvas, big, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT, 128);
		cv::resize(big, small, cv::Size(w, h), 0, 0, cv::INTER_AREA);

		image::Image out{w, h, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		for (int row = 0; row < h; ++row)
			std::memcpy(out.data() + std::size_t(row) * std::size_t(w), small.ptr<std::uint8_t>(row), std::size_t(w));
		return out;
	}
} // namespace lain::camera::testing
