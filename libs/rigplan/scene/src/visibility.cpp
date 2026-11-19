#include "lain/rigplan/visibility.h"

#include <lain/camera/projection.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

namespace lain::rigplan
{
	static constexpr double kPi = 3.14159265358979323846;
	static constexpr std::array<render::FaceAxis, 5> kAxes = {render::FaceAxis::Front, render::FaceAxis::Right, render::FaceAxis::Left,
															  render::FaceAxis::Down, render::FaceAxis::Up};
	// How close to a face's region a border ray may come and still reach it, as a difference of unit
	// components. A face entered only between two of the border's rays, a pixel apart, is entered by
	// less than a pixel's angle, which is under this for any camera of more than 70 pixels a radian.
	static constexpr double kFaceSlack = 0.02;
	// A distance read back is a float: what it holds of the distance, beside the texel's angle.
	static constexpr double kFloatTolerance = 1e-5;

	// The XY rectangle `plan` places things in, without a room: its mounts, planes, boxes, props and
	// the keys of its paths. Empty when it places nothing.
	struct Bounds
	{
		math::Vec2d least{std::numeric_limits<double>::infinity()};
		math::Vec2d most{-std::numeric_limits<double>::infinity()};

		void add(const math::Vec2d& p, double reach = 0)
		{
			least = math::min(least, p - math::Vec2d{reach});
			most = math::max(most, p + math::Vec2d{reach});
		}
		bool empty() const { return !(least.x <= most.x && least.y <= most.y); }
	};

	static Bounds placed(const RigPlan& plan)
	{
		Bounds bounds;
		auto addPath = [&](const Path& path)
		{
			for (const PathKey& key : path.keys)
				bounds.add({key.position.x, key.position.y});
		};
		for (const Mount& mount : plan.mounts)
		{
			if (const auto* pole = std::get_if<Pole>(&mount.kind))
				bounds.add(pole->floor);
			else
				bounds.add({std::get<FixedPoint>(mount.kind).position.x, std::get<FixedPoint>(mount.kind).position.y});
		}
		// A plane or box may be turned, so it reaches as far as its largest half-extent either way.
		for (const Plane& plane : plan.planes)
			bounds.add({plane.centre.x, plane.centre.y}, std::max(plane.size.x, plane.size.y) / 2);
		for (const Box& box : plan.boxes)
			bounds.add({box.centre.x, box.centre.y}, std::hypot(box.size.x, box.size.y) / 2);
		for (const Prop& prop : plan.props)
		{
			bounds.add({prop.position.x, prop.position.y});
			if (prop.path)
				addPath(*prop.path);
		}
		for (const BoardMover& board : plan.boards)
			addPath(board.path);
		return bounds;
	}

	VisibilityGrid visibilityGrid(const RigPlan& plan, double height, double cell)
	{
		VisibilityGrid grid;
		grid.cell = cell;
		grid.height = height;
		Bounds bounds;
		if (plan.room)
		{
			const math::Vec2d half{plan.room->size.x / 2, plan.room->size.y / 2};
			bounds.add(plan.room->centre - half);
			bounds.add(plan.room->centre + half);
		}
		else
		{
			bounds = placed(plan);
		}
		if (bounds.empty() || !(cell > 0))
			return grid;
		const math::Vec2d extent = bounds.most - bounds.least;
		// A side a whole number of cells long is that many, not one more by rounding.
		grid.columns = std::max(1, int(std::ceil(extent.x / cell - 1e-9)));
		grid.rows = std::max(1, int(std::ceil(extent.y / cell - 1e-9)));
		const math::Vec2d middle = (bounds.least + bounds.most) / 2.0;
		grid.origin = middle - math::Vec2d{grid.columns * cell, grid.rows * cell} / 2.0;
		return grid;
	}

	std::vector<VisibilityCamera> visibilityCameras(const NominalLayout& layout)
	{
		std::vector<VisibilityCamera> cameras;
		for (const NominalLayout::Camera& camera : layout.cameras)
		{
			if (camera.planFromCamera && camera.model)
				cameras.push_back({camera.id, camera.name, *camera.model, *camera.planFromCamera});
		}
		return cameras;
	}

	// `d`'s component along the axis the face `axis` looks along.
	static double along(render::FaceAxis axis, const math::Vec3d& d)
	{
		return math::dot(d, render::cameraFromFace(axis)[2]);
	}

	std::vector<render::CaptureFace> visibilityFaces(const camera::CameraModel& model, std::uint32_t size)
	{
		std::array<bool, 5> reached{};
		const camera::Projection<double> axis = camera::project(model, 0.0, 0.0, 1.0);
		reached[std::size_t(render::FaceAxis::Front)] = axis.ok() && camera::contains(model, axis.u, axis.v);

		auto border = [&](double u, double v)
		{
			const camera::Unprojection<double> ray = camera::unproject(model, u, v);
			if (!ray.ok())
				return;
			const math::Vec3d d = math::normalize(math::Vec3d{ray.x, ray.y, ray.z});
			double largest = -std::numeric_limits<double>::infinity();
			for (const render::FaceAxis a : kAxes)
				largest = std::max(largest, along(a, d));
			for (const render::FaceAxis a : kAxes)
			{
				if (along(a, d) >= largest - kFaceSlack)
					reached[std::size_t(a)] = true;
			}
		};
		// Every pixel along the image's outer edge, its corners included.
		const std::uint32_t w = model.image().width;
		const std::uint32_t h = model.image().height;
		for (std::uint32_t i = 0; i <= w; ++i)
		{
			border(i - 0.5, -0.5);
			border(i - 0.5, h - 0.5);
		}
		for (std::uint32_t j = 0; j <= h; ++j)
		{
			border(-0.5, j - 0.5);
			border(w - 0.5, j - 0.5);
		}

		std::vector<render::CaptureFace> faces;
		for (const render::FaceAxis a : kAxes)
		{
			if (!reached[std::size_t(a)])
				continue;
			render::CaptureFace face;
			face.axis = a;
			face.focal = size / 2.0; // 90 degrees across: tan 45 is one
			face.principalPoint = math::Vec2d{(size - 1) / 2.0};
			face.first = math::Vec2i{0};
			face.last = math::Vec2i{int(size) - 1};
			face.nearScale = 1 / std::sqrt(3.0); // its corner's ray is 54.7 degrees off its axis
			faces.push_back(face);
		}
		return faces;
	}

	render::View faceView(const render::CaptureFace& face, const math::RigidTransformd& planFromCamera)
	{
		const render::CaptureView range;
		render::View view;
		view.worldFromCamera = planFromCamera * math::RigidTransformd{math::quat_cast(render::cameraFromFace(face.axis)), math::Vec3d{0.0}};
		view.image = {std::uint32_t(face.last.x - face.first.x + 1), std::uint32_t(face.last.y - face.first.y + 1)};
		view.intrinsics = {face.focal, face.focal, face.principalPoint.x - face.first.x, face.principalPoint.y - face.first.y};
		view.nearPlane = range.nearPlane * face.nearScale;
		view.farPlane = range.farPlane;
		return view;
	}

	std::vector<bool> seenBy(const VisibilityGrid& grid, const VisibilityCamera& camera, const std::vector<FaceDistances>& faces)
	{
		std::vector<bool> seen(grid.cells(), false);

		// Each face by its axis, where its distances are the face's size.
		std::array<const FaceDistances*, 5> byAxis{};
		for (const FaceDistances& face : faces)
		{
			const int width = face.face.last.x - face.face.first.x + 1;
			const int height = face.face.last.y - face.face.first.y + 1;
			const image::Image& d = face.distances;
			if (d.valid() && d.pixelFormat() == image::PixelFormat::Gray32F && d.width() == width && d.height() == height)
				byAxis[std::size_t(face.face.axis)] = &face;
		}

		const render::CaptureView range;
		const double grazing = std::cos(kGrazingDegrees * kPi / 180);
		const math::RigidTransformd cameraFromPlan = camera.planFromCamera.inverse();
		const math::Vec3d eye = camera.planFromCamera.translation();
		for (int row = 0; row < grid.rows; ++row)
		{
			for (int column = 0; column < grid.columns; ++column)
			{
				const math::Vec3d toCell = grid.centre(column, row) - eye;
				const double r = math::length(toCell);
				if (!(r >= range.nearPlane && r <= range.farPlane))
					continue;
				const double cosine = std::abs(toCell.z) / r; // the plane's normal is the plan's +Z
				if (cosine < grazing)
					continue;
				const math::Vec3d d = cameraFromPlan.rotate(toCell) / r;
				const camera::Projection<double> pixel = camera::project(camera.model, d.x, d.y, d.z);
				if (!pixel.ok() || !camera::contains(camera.model, pixel.u, pixel.v))
					continue;
				const FaceDistances* face = byAxis[std::size_t(render::majorAxis(d))];
				if (!face)
					continue;

				const render::CaptureFace& f = face->face;
				const math::Vec2d texel = render::onFace(f, d);
				const int width = f.last.x - f.first.x + 1;
				const int height = f.last.y - f.first.y + 1;
				const int i = std::clamp(int(std::floor(texel.x + 0.5)) - f.first.x, 0, width - 1);
				const int j = std::clamp(int(std::floor(texel.y + 0.5)) - f.first.y, 0, height - 1);
				const float stored = reinterpret_cast<const float*>(face->distances.data())[std::size_t(j) * std::size_t(width) + std::size_t(i)];

				// The nearest texel's ray is within half its diagonal of the cell's, and what the rasteriser
				// drew within a sixteenth of a texel (Vulkan's least precision): under one texel's angle,
				// which is at most 1 / focal, at the face's centre. Along the plane at slope s to the ray, a
				// distance moves by that angle times s.
				const double slope = std::sqrt(std::max(1 - cosine * cosine, 0.0)) / cosine;
				const double tolerance = r * (slope / f.focal + kFloatTolerance);
				if (double(stored) >= r - tolerance)
					seen[std::size_t(row) * std::size_t(grid.columns) + std::size_t(column)] = true;
			}
		}
		return seen;
	}

	VisibilityMap::VisibilityMap(VisibilityGrid grid)
		: m_grid(std::move(grid))
		, m_counts(m_grid.cells(), 0)
	{
	}

	void VisibilityMap::add(const core::Uuid& camera, std::vector<bool> seen)
	{
		assert(seen.size() == m_grid.cells() && "a camera's cells are seenBy's, for this map's grid");
		const auto at = std::find(m_cameras.begin(), m_cameras.end(), camera);
		const std::size_t which = std::size_t(at - m_cameras.begin());
		if (at == m_cameras.end())
		{
			m_cameras.push_back(camera);
			m_seen.emplace_back(m_grid.cells(), false);
		}
		std::vector<bool>& held = m_seen[which];
		for (std::size_t c = 0; c < m_counts.size(); ++c)
		{
			m_counts[c] = m_counts[c] - (held[c] ? 1u : 0u) + (seen[c] ? 1u : 0u);
		}
		held = std::move(seen);
	}

	std::vector<core::Uuid> VisibilityMap::camerasAt(int column, int row) const
	{
		std::vector<core::Uuid> cameras;
		const std::size_t c = index(column, row);
		for (std::size_t k = 0; k < m_cameras.size(); ++k)
		{
			if (m_seen[k][c])
				cameras.push_back(m_cameras[k]);
		}
		return cameras;
	}
} // namespace lain::rigplan
