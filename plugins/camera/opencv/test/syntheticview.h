#pragma once

// A rendered board as a pinhole camera would see it, for the plugin's tests. Drawn 4x supersampled
// and averaged down: a single warp aliases the markers, and the aliasing shows up as a bias in every
// corner (opencv-prebuilt's probe measured 0.8% in a recovered focal length before this).

#include <lain/camera/board/rendering.h>
#include <lain/camera/board/specification.h>
#include <lain/camera/cameramodel.h>
#include <lain/image/image.h>
#include <lain/math/rigidtransform.h>

#include <opencv2/imgproc.hpp>

#include <cstring>

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
} // namespace lain::camera::testing
