#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/camera/projection.h>
#include <lain/image/color.h>
#include <lain/image/image.h>
#include <lain/mesh/mesh.h>
#include <lain/render/scene.h>
#include <lain/render/view.h>

#include <array>
#include <memory>

namespace lain::render::testing
{
	// A square two metres across in its mesh's XY plane, facing -Z (normals included unless asked
	// not to be), with one material. Its texture coordinates are laid out so a camera at rest, which
	// looks along +Z with Y down, sees its picture upright: v = 1 along the square's -Y edge.
	// `uvScale` repeats the picture that many times across.
	inline std::shared_ptr<const mesh::Mesh> square(bool normals = true, float uvScale = 1, math::Vec3f normal = {0.0f, 0.0f, -1.0f})
	{
		mesh::Mesh mesh;
		mesh.positions = {{-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}};
		if (normals)
			mesh.normals = {normal, normal, normal, normal};
		mesh.uvs = {{0.0f, uvScale}, {uvScale, uvScale}, {uvScale, 0.0f}, {0.0f, 0.0f}};
		mesh.indices = {0, 1, 2, 0, 2, 3};
		mesh.parts = {{0, 0, 6}};
		mesh.materials = {mesh::Material{}};
		return std::make_shared<const mesh::Mesh>(std::move(mesh));
	}

	// The four corners of square(), in its mesh's frame.
	inline std::array<math::Vec3d, 4> squareCorners()
	{
		return {math::Vec3d{-1.0, -1.0, 0.0}, math::Vec3d{1.0, -1.0, 0.0}, math::Vec3d{1.0, 1.0, 0.0}, math::Vec3d{-1.0, 1.0, 0.0}};
	}

	// A camera at rest at the world's origin: the world's axes are the camera frame's.
	inline View view(std::uint32_t width, std::uint32_t height, double f, double cx, double cy)
	{
		View v;
		v.image = {width, height};
		v.intrinsics = {f, f, cx, cy};
		v.nearPlane = 0.1;
		v.farPlane = 20;
		return v;
	}

	// The undistorted model of the same pinhole, to project through lain's own camera geometry.
	inline camera::CameraModel pinholeOf(const View& v)
	{
		return *camera::CameraModel::create({v.image, v.intrinsics, camera::NoDistortion{}}).model;
	}

	inline image::ColorRGBAf pixel(const image::Image& picture, int x, int y)
	{
		const float* p = reinterpret_cast<const float*>(picture.data()) + 4 * (std::size_t(y) * picture.width() + x);
		return image::ColorRGBAf{p[0], p[1], p[2], p[3]};
	}
} // namespace lain::render::testing
