#pragma once

#include <lain/camera/cameramodel.h>
#include <lain/core/uuid.h>
#include <lain/image/image.h>
#include <lain/math/rigidtransform.h>
#include <lain/math/types.h>
#include <lain/render/capture.h>
#include <lain/render/view.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lain::rigplan
{
	// The visibility map (CONTEXT.md, "Visibility map"): how many cameras see each cell of a plane of a
	// rig plan, unoccluded, from its nominal poses, over what never moves (staticScene). Each camera
	// sees the static scene as distances on the faces of a cube at its optical centre, drawn by
	// render::Renderer::renderDistance through faceView. A cell is seen when lain's own projection
	// puts it inside the camera's image, and the distance along the cell's ray reaches it. Drawing is
	// the caller's, a face at a time; counting needs no device.

	inline constexpr double kVisibilityCell = 0.05;			   // metres a side
	inline constexpr std::uint32_t kVisibilityFaceSize = 1024; // texels a side: 2 mrad at a face's centre
	// A cell whose ray meets the plane further than this from its normal is not counted: there, a cell's
	// distance changes faster along the plane than anything drawn holds it (tan 85 degrees is 11.4).
	inline constexpr double kGrazingDegrees = 85;

	// The plane a visibility map counts over: a rectangle at a height above the floor, in square cells,
	// column by column along the plan's +X and row by row along its +Y.
	struct VisibilityGrid
	{
		math::Vec2d origin{0.0, 0.0};  // the plan-frame (x, y) of cell (0, 0)'s corner nearest the origin
		double cell = kVisibilityCell; // metres a side
		int columns = 0;
		int rows = 0;
		double height = 0; // metres above the floor

		std::size_t cells() const { return std::size_t(columns) * std::size_t(rows); }
		// Cell (column, row)'s centre in the plan frame.
		math::Vec3d centre(int column, int row) const
		{
			return {origin.x + (column + 0.5) * cell, origin.y + (row + 0.5) * cell, height};
		}
	};

	// The grid at `height` over `plan`'s room's floor; without a room, over the floor's rectangle that
	// holds what the plan places (its mounts, planes, boxes, props, and the keys of its paths). Whole
	// cells, centred on that rectangle, at least one each way. A plan that places nothing has no cells.
	VisibilityGrid visibilityGrid(const RigPlan& plan, double height = 0, double cell = kVisibilityCell);

	// A camera the map counts: where it is meant to be, and its nominal model.
	struct VisibilityCamera
	{
		core::Uuid id;
		std::string name;
		camera::CameraModel model;
		math::RigidTransformd planFromCamera; // camera-frame coordinates (X right, Y down, Z forward)
	};

	// Every camera of `layout` with a pose and a model, in its order. One without either sees nothing,
	// and check() says why.
	std::vector<VisibilityCamera> visibilityCameras(const NominalLayout& layout);

	// The faces of a camera's cube its image reaches, in render::FaceAxis's order: each `size` texels a
	// side and 90 degrees across, its texels centred on a pixel's way (render::CaptureFace). A face is
	// reached when a ray of the image's border falls on it, or, for the front face, when the camera's
	// axis lies inside the image, which is the one face an image can hold without its border reaching
	// it. So a narrow lens needs only its front face, and a fisheye all five.
	std::vector<render::CaptureFace> visibilityFaces(const camera::CameraModel& model, std::uint32_t size = kVisibilityFaceSize);

	// How `face` of a camera at `planFromCamera` is drawn: a pinhole at its optical centre, turned to
	// the face's axis (render::cameraFromFace), drawing what a capture would along any ray it serves
	// (render::CaptureView's depth range, the near plane pulled in by the face's nearScale).
	render::View faceView(const render::CaptureFace& face, const math::RigidTransformd& planFromCamera);

	// One face of a camera's cube and its distances: render::Renderer::renderDistance of its faceView.
	struct FaceDistances
	{
		render::CaptureFace face;
		image::Image distances; // Gray32F, the face's size
	};

	// The cells of `grid` that `camera` sees, a bit a cell, row by row, over `faces` (one per face
	// visibilityFaces gives it; a missing or misshapen one sees nothing). A cell is seen when:
	//   * it lies within a capture's depth range of the camera, and its ray meets the plane within
	//     kGrazingDegrees of the normal;
	//   * lain's projection puts it inside the camera's image (camera::project, camera::contains);
	//   * the distance at the texel nearest its ray, on the face its ray falls on, reaches it. A texel is
	//     never blended with its neighbours: at a silhouette, two distances averaged reach neither.
	// "Reaches" has a tolerance: the nearest texel's ray is within one texel's angle of the cell's, and
	// along a plane at slope s to the ray a distance moves by that angle times s, so a cell lying on a
	// drawn floor would otherwise hide behind the floor itself.
	std::vector<bool> seenBy(const VisibilityGrid& grid, const VisibilityCamera& camera, const std::vector<FaceDistances>& faces);

	// How many cameras see each cell of a grid, and which.
	class VisibilityMap
	{
	public:
		VisibilityMap() = default;
		explicit VisibilityMap(VisibilityGrid grid);

		const VisibilityGrid& grid() const { return m_grid; }

		// Counts `camera` as seeing the cells `seen` holds (seenBy's, for this map's grid), replacing
		// what it was counted as seeing before.
		void add(const core::Uuid& camera, std::vector<bool> seen);

		// How many cameras see cell (column, row), and which, in the order they were first counted.
		std::size_t count(int column, int row) const { return m_counts[index(column, row)]; }
		std::vector<core::Uuid> camerasAt(int column, int row) const;

		// Every camera counted, in that order.
		const std::vector<core::Uuid>& cameras() const { return m_cameras; }

	private:
		std::size_t index(int column, int row) const { return std::size_t(row) * std::size_t(m_grid.columns) + std::size_t(column); }

		VisibilityGrid m_grid;
		std::vector<core::Uuid> m_cameras;
		std::vector<std::vector<bool>> m_seen; // per camera, in m_cameras' order
		std::vector<std::uint32_t> m_counts;
	};
} // namespace lain::rigplan
