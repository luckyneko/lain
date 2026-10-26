#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::render
{
	// How a capture render samples one camera through its own model (ADR-0028). It rasterises an
	// intermediate image, pinhole faces at the camera's optical centre, then remaps it: each pixel is
	// the mean of s x s rays a ray table gives.
	struct CaptureSettings
	{
		// s: rays a side per pixel, at x + (i + 0.5) / s - 0.5 and likewise in y, where
		// texturedscene.h's CPU renderer puts them, so a pixel integrates its footprint.
		std::uint32_t samples = 4;
		// m: a step of one camera pixel, in any direction, spans at least m texels of the intermediate
		// image, so the remap's own filtering stays below the camera's resolution.
		double oversampling = 2;
		// How far, in pixels, an interpolated ray may project from the point it was meant for before a
		// ray table is refused (RayTable::interpolationError).
		double interpolationTolerance = 0.01;
		// A tile's largest side as drawn, in texels. 4096 is the least maxImageDimension2D every Vulkan
		// device has, so a capture is tiled the same way on every device. Smaller only to test tiling.
		std::uint32_t maxTileSide = 4096;
	};

	// Which way a face of the intermediate image looks, in lain's camera frame (X right, Y down, Z
	// forward). There is no back face: every camera model sees only what is in front of it (Z > 0).
	enum class FaceAxis
	{
		Front, // +Z
		Right, // +X
		Left,  // -X
		Down,  // +Y
		Up,	   // -Y
	};

	// A face's frame in the camera frame: a signed permutation, a proper rotation, whose columns are
	// the face's X (right), Y (down) and Z (forward). So a face is a pinhole of lain's own camera
	// convention, turned to look along its axis.
	math::Mat3d cameraFromFace(FaceAxis axis);

	// The axis along which camera-frame direction `d` is largest, ties going to the earlier axis in
	// FaceAxis's order: the face that sees `d`. The remap's shader applies the same rule.
	FaceAxis majorAxis(const math::Vec3d& d);

	// One pinhole face of the intermediate image. A direction lands on it at
	// focal * (x / z, y / z) + principalPoint, with (x, y, z) the direction in the face's frame, and its
	// texel (i, j) is centred at face coordinate (i, j), as lain's pixels are.
	struct CaptureFace
	{
		FaceAxis axis = FaceAxis::Front;
		double focal = 0;				 // texels per unit of the face's plane, the same both ways
		math::Vec2d principalPoint{0.0}; // where the face's axis lands
		// The texels drawn: columns first.x to last.x and rows first.y to last.y, inclusive.
		math::Vec2i first{0};
		math::Vec2i last{0};
		// The cosine of the widest angle off the face's axis of any ray it serves. A face's near plane
		// is measured along its own axis, so it is drawn with the capture's near times this.
		double nearScale = 1;
	};

	// A rectangle of one face, drawn as one raster pass. It draws texels first to last, inclusive,
	// both ways, at most CaptureSettings::maxTileSide a side. A sub-sample at face coordinate (u, v)
	// belongs to it when floor(u) and floor(v) lie in [first, last), so all four texels its bilinear
	// blend reads are drawn. A side on the face's edge is open and takes everything beyond, so the
	// tiles of a face partition its whole plane.
	struct CaptureTile
	{
		std::size_t face = 0; // into RayTable::faces()
		math::Vec2i first{0};
		math::Vec2i last{0};
		bool openLeft = false;
		bool openTop = false;
		bool openRight = false;
		bool openBottom = false;
		// The camera pixels [pixelFirst, pixelEnd) any of whose sub-samples it may take: what a remap
		// visits for it.
		math::Vec2i pixelFirst{0};
		math::Vec2i pixelEnd{0};

		// Whether a sub-sample at face coordinate `texel` belongs to this tile.
		bool accepts(const math::Vec2d& texel) const;
	};

	struct RayTableResult;

	// What a capture render needs of one camera model, built once on the CPU and reused for every
	// frame (ADR-0028): lain's own unprojection at every pixel corner, and the faces and tiles of an
	// intermediate image fine enough to remap from. A sub-sample's ray is its pixel's four corner
	// directions blended bilinearly. That is the one approximation of the model a capture makes, and
	// it is measured: interpolationError().
	class RayTable
	{
	public:
		// The model's ray table, or why not: the settings are not usable, a pixel corner has no ray, or
		// the corners interpolate the model worse than the settings' tolerance.
		static RayTableResult build(const camera::CameraModel& model, const CaptureSettings& settings = {});

		const camera::CameraModel& model() const { return m_model; }
		const CaptureSettings& settings() const { return m_settings; }

		// The unit ray through every pixel corner, (width + 1) x (height + 1) of them row by row: corner
		// (i, j) is at pixel coordinate (i - 0.5, j - 0.5). As floats, since that is what a remap reads.
		const std::vector<math::Vec3f>& corners() const { return m_corners; }

		// The ray through (u, v), unit length: the bilinear blend of the corner directions of the pixel
		// (u, v) lies in, as a remap blends them. (u, v) must lie inside the image.
		math::Vec3d direction(double u, double v) const;

		// The worst, over every pixel centre, of how far its interpolated ray projects from it through
		// the model, in pixels. A pixel's centre is where a bilinear blend is least exact.
		double interpolationError() const { return m_interpolationError; }

		const std::vector<CaptureFace>& faces() const { return m_faces; }
		const std::vector<CaptureTile>& tiles() const { return m_tiles; }

		// Whether a face is chosen by majorAxis, the cube's faces, or the one front face takes every
		// ray, whichever needed fewer texels.
		bool cube() const { return m_cube; }

		// The face a remap gives direction `d` to (an index into faces()), or none when that is a face
		// this table lacks, which no ray of the image is.
		std::optional<std::size_t> faceOf(const math::Vec3d& d) const;

		// Where direction `d` lands on face `face`, in its texel coordinates.
		math::Vec2d onFace(std::size_t face, const math::Vec3d& d) const;

		// The camera rows a band of the ray table holds at this width. A band's corners (one row more
		// than its pixels) and its pixels' results each fit 2^27 bytes, as 16 bytes apiece, which is
		// the least maxStorageBufferRange every Vulkan device has.
		static std::uint32_t bandRowsFor(std::uint32_t width);

	private:
		RayTable(camera::CameraModel model, const CaptureSettings& settings);

		camera::CameraModel m_model;
		CaptureSettings m_settings;
		std::vector<math::Vec3f> m_corners;
		double m_interpolationError = 0;
		std::vector<CaptureFace> m_faces;
		std::vector<CaptureTile> m_tiles;
		bool m_cube = false;
	};

	// A build's outcome: the ray table, or why not.
	struct RayTableResult
	{
		std::optional<RayTable> table;
		std::vector<std::string> problems; // empty exactly when there is a table
	};

	// Where a captured camera is, and what it draws: lain's camera frame placed in the world, as a
	// View's is, and its depth range. Everything farther than `nearPlane` and nearer than `farPlane`
	// along the camera's rays is drawn. Each face of the intermediate image measures depth along its own
	// axis, so it is drawn with its near plane pulled in by its nearScale, and may also draw a little
	// nearer than `nearPlane` toward its axis. Named as View's are, for the same reason (<windows.h>).
	struct CaptureView
	{
		math::RigidTransformd worldFromCamera;
		double nearPlane = 0.05;
		double farPlane = 100;
	};

	// Why `view` cannot be drawn: a pose that is not finite, or a depth range that is not
	// 0 < near < far. Empty when it can be.
	std::vector<std::string> validate(const CaptureView& view);
} // namespace lain::render
