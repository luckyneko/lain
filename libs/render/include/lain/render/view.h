#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>

#include <string>
#include <vector>

namespace lain::render
{
	// A pinhole looking at a scene: lain's camera frame (X right, Y down, Z forward) placed in the
	// world, lain's intrinsics with pixel (0, 0) at the centre of the top-left pixel, and the depth
	// range drawn. Never distorted: distortion belongs to a capture render, which remaps a raster
	// through lain's own camera model (ADR-0028).
	struct View
	{
		math::RigidTransformd worldFromCamera;
		camera::ImageGeometry image;
		camera::Intrinsics intrinsics;
		// Metres in front of the camera: nothing nearer than the near plane is drawn, nothing farther
		// than the far one. Not `near` and `far`, which <windows.h> defines as empty macros, so a
		// consumer including it first (CLI11 does) could not compile this header.
		double nearPlane = 0.05;
		double farPlane = 100;
	};

	// Why `view` cannot be drawn: an empty image, a focal length that is not positive and finite, a
	// principal point or pose that is not finite, or a depth range that is not 0 < near < far. Empty
	// when it can be.
	std::vector<std::string> validate(const View& view);

	// The camera frame into Vulkan's clip space for this view: x right and y down, as the camera's, a
	// pixel's centre where lain's projection puts it, and depth 0 at `near` and 1 at `far`. So a point
	// at pixel (u, v) of lain's projection lands at framebuffer coordinate (u + 0.5, v + 0.5), where
	// the rasteriser samples that pixel.
	math::Mat4d clipFromCamera(const View& view);
} // namespace lain::render
