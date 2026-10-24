#include "lain/render/capture.h"

#include <lain/camera/projection.h>
#include <lain/string/format.h>
#include <lain/task/task.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace lain::render
{
	// Texels drawn beyond the sub-samples a face serves, on every side. A remap's float arithmetic may
	// land a sub-sample a little off where the CPU's double put it, and it must still find all four
	// texels of its bilinear blend drawn.
	static constexpr int kGuard = 2;
	// How far past the edge of a face's region a ray may still be given to it. The cells' own extent
	// covers every ray inside them; this covers the remap's float arithmetic on top. Like the guard
	// band, the open edge tiles and the texel either side of a tile's pixel rectangle, it is there for
	// the shader, whose float choices no CPU case can make: no ray of the test cameras reaches a face
	// none of its cell's corners belongs to, and for a pinhole none can.
	static constexpr double kWiden = 1e-5;
	// The front face alone serves no ray more than 80 degrees off its axis: past that, its plane is so
	// stretched that the cube's faces are always smaller.
	static const double kFrontOnlyCosine = std::cos(80.0 * 3.14159265358979323846 / 180.0);
	// The least a tile's side may be, so it holds the guard bands and something between them.
	static constexpr std::uint32_t kMinTileSide = 8;
	// A storage buffer's least guaranteed range (maxStorageBufferRange), and what one corner or one
	// pixel's result takes of it.
	static constexpr std::size_t kBufferBytes = std::size_t{1} << 27;
	static constexpr std::size_t kEntryBytes = 16;

	static constexpr std::array<FaceAxis, 5> kAxes = {FaceAxis::Front, FaceAxis::Right, FaceAxis::Left, FaceAxis::Down, FaceAxis::Up};

	math::Mat3d cameraFromFace(FaceAxis axis)
	{
		// GLM's constructor takes the columns in order: the face's X, then Y, then Z.
		switch (axis)
		{
			case FaceAxis::Front:
				return math::Mat3d{1, 0, 0, 0, 1, 0, 0, 0, 1};
			case FaceAxis::Right: // right is the camera's back, down is down
				return math::Mat3d{0, 0, -1, 0, 1, 0, 1, 0, 0};
			case FaceAxis::Left: // right is the camera's forward, down is down
				return math::Mat3d{0, 0, 1, 0, 1, 0, -1, 0, 0};
			case FaceAxis::Down: // right is right, down is the camera's back
				return math::Mat3d{1, 0, 0, 0, 0, -1, 0, 1, 0};
			case FaceAxis::Up: // right is right, down is the camera's forward
				return math::Mat3d{1, 0, 0, 0, 0, 1, 0, -1, 0};
		}
		return math::Mat3d{1.0};
	}

	// `d` in the frame of the face along `axis`: transpose(cameraFromFace(axis)) * d, which for a
	// signed permutation is only choosing and negating components. The shader does the same.
	static math::Vec3d inFace(FaceAxis axis, const math::Vec3d& d)
	{
		switch (axis)
		{
			case FaceAxis::Front:
				return d;
			case FaceAxis::Right:
				return {-d.z, d.y, d.x};
			case FaceAxis::Left:
				return {d.z, d.y, -d.x};
			case FaceAxis::Down:
				return {d.x, -d.z, d.y};
			case FaceAxis::Up:
				return {d.x, d.z, -d.y};
		}
		return d;
	}

	FaceAxis majorAxis(const math::Vec3d& d)
	{
		FaceAxis axis = FaceAxis::Front;
		double best = d.z;
		if (d.x > best)
		{
			axis = FaceAxis::Right;
			best = d.x;
		}
		if (-d.x > best)
		{
			axis = FaceAxis::Left;
			best = -d.x;
		}
		if (d.y > best)
		{
			axis = FaceAxis::Down;
			best = d.y;
		}
		if (-d.y > best)
			axis = FaceAxis::Up;
		return axis;
	}

	// How far `d` is inside the region of the face along `axis`: its component along that axis less
	// the largest along any other. Positive inside, negative outside.
	static double margin(FaceAxis axis, const math::Vec3d& d)
	{
		const double own = inFace(axis, d).z;
		double other = -std::numeric_limits<double>::infinity();
		for (FaceAxis a : kAxes)
		{
			if (a != axis)
				other = std::max(other, inFace(a, d).z);
		}
		return own - other;
	}

	std::vector<std::string> validate(const CaptureView& view)
	{
		std::vector<std::string> problems;
		const math::Quatd& q = view.worldFromCamera.rotation();
		const math::Vec3d& t = view.worldFromCamera.translation();
		if (!(std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(t.x) &&
			  std::isfinite(t.y) && std::isfinite(t.z)))
			problems.push_back("the capture's pose is not finite");
		if (!(std::isfinite(view.near) && std::isfinite(view.far) && view.near > 0 && view.near < view.far))
			problems.push_back("the capture's depth range is not 0 < near < far");
		return problems;
	}

	bool CaptureTile::accepts(const math::Vec2d& texel) const
	{
		const double u = std::floor(texel.x);
		const double v = std::floor(texel.y);
		return (openLeft || u >= first.x) && (openRight || u < last.x) && (openTop || v >= first.y) && (openBottom || v < last.y);
	}

	RayTable::RayTable(camera::CameraModel model, const CaptureSettings& settings)
		: m_model(std::move(model))
		, m_settings(settings)
	{
	}

	math::Vec3d RayTable::direction(double u, double v) const
	{
		const int w = int(m_model.image().width);
		const int h = int(m_model.image().height);
		const double a = u + 0.5;
		const double b = v + 0.5;
		const int i = std::clamp(int(std::floor(a)), 0, w - 1);
		const int j = std::clamp(int(std::floor(b)), 0, h - 1);
		const double t = a - i;
		const double s = b - j;
		const std::size_t row = std::size_t(w) + 1;
		const math::Vec3d c00{m_corners[std::size_t(j) * row + std::size_t(i)]};
		const math::Vec3d c10{m_corners[std::size_t(j) * row + std::size_t(i) + 1]};
		const math::Vec3d c01{m_corners[std::size_t(j + 1) * row + std::size_t(i)]};
		const math::Vec3d c11{m_corners[std::size_t(j + 1) * row + std::size_t(i) + 1]};
		const math::Vec3d top = c00 + t * (c10 - c00);
		const math::Vec3d bottom = c01 + t * (c11 - c01);
		return math::normalize(top + s * (bottom - top));
	}

	std::optional<std::size_t> RayTable::faceOf(const math::Vec3d& d) const
	{
		if (!m_cube)
			return std::size_t{0};
		const FaceAxis axis = majorAxis(d);
		for (std::size_t f = 0; f < m_faces.size(); ++f)
		{
			if (m_faces[f].axis == axis)
				return f;
		}
		return std::nullopt;
	}

	math::Vec2d RayTable::onFace(std::size_t face, const math::Vec3d& d) const
	{
		const CaptureFace& f = m_faces[face];
		const math::Vec3d p = inFace(f.axis, d);
		return f.focal * math::Vec2d{p.x / p.z, p.y / p.z} + f.principalPoint;
	}

	std::uint32_t RayTable::bandRowsFor(std::uint32_t width)
	{
		const std::size_t perRow = (std::size_t(width) + 1) * kEntryBytes;
		const std::size_t rows = kBufferBytes / perRow;
		return std::uint32_t(rows > 1 ? rows - 1 : 1);
	}

	// The four corners of pixel (x, y), and how far its rays spread.
	struct Cell
	{
		std::array<math::Vec3d, 4> corners; // (x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)
		double spread = 0;					// the largest distance between two of them
	};

	static Cell cellAt(const std::vector<math::Vec3f>& corners, std::size_t row, std::size_t x, std::size_t y)
	{
		Cell cell;
		cell.corners = {math::Vec3d{corners[y * row + x]}, math::Vec3d{corners[y * row + x + 1]},
						math::Vec3d{corners[(y + 1) * row + x]}, math::Vec3d{corners[(y + 1) * row + x + 1]}};
		for (std::size_t a = 0; a < 4; ++a)
		{
			for (std::size_t b = a + 1; b < 4; ++b)
				cell.spread = std::max(cell.spread, math::length(cell.corners[a] - cell.corners[b]));
		}
		return cell;
	}

	// Whether any ray in `cell` may be given to the face along `axis`. A ray inside the cell is within
	// `spread` of some corner, and a margin moves by at most twice the distance a direction does, so a
	// cell none of whose corners is within twice its spread (and kWiden) of the face holds no ray of it.
	static bool member(FaceAxis axis, const Cell& cell)
	{
		const double reach = -(2 * cell.spread + kWiden);
		for (const math::Vec3d& c : cell.corners)
		{
			if (margin(axis, c) >= reach)
				return true;
		}
		return false;
	}

	// What one face candidate needs of the cells it serves: the extent of their corners on its plane at
	// unit focal, the least stretch a pixel step takes there, and the widest angle off its axis.
	struct FaceStats
	{
		bool any = false;
		bool tooWide = false; // a corner too near the face's plane to project onto it
		math::Vec2d min{std::numeric_limits<double>::infinity()};
		math::Vec2d max{-std::numeric_limits<double>::infinity()};
		double stretch = std::numeric_limits<double>::infinity(); // the least singular value of a cell's Jacobian
		double cosine = 1;

		void merge(const FaceStats& other)
		{
			any = any || other.any;
			tooWide = tooWide || other.tooWide;
			min = math::min(min, other.min);
			max = math::max(max, other.max);
			stretch = std::min(stretch, other.stretch);
			cosine = std::min(cosine, other.cosine);
		}
	};

	// The least singular value of the 2x2 matrix with columns `a` and `b`: how little a unit step of
	// the camera's pixels can move on a face's plane.
	static double leastSingularValue(const math::Vec2d& a, const math::Vec2d& b)
	{
		const double sum = math::dot(a, a) + math::dot(b, b);
		const double det = a.x * b.y - a.y * b.x;
		const double largest = std::sqrt(0.5 * (sum + std::sqrt(std::max(sum * sum - 4 * det * det, 0.0))));
		return largest > 0 ? std::abs(det) / largest : 0;
	}

	// Adds `cell`, seen from the face along `axis`, to `stats`. `minCosine` is the narrowest a corner's
	// angle to the axis may be before the face cannot serve it.
	static void addCell(FaceStats& stats, FaceAxis axis, const Cell& cell, double minCosine)
	{
		std::array<math::Vec2d, 4> p;
		for (std::size_t k = 0; k < 4; ++k)
		{
			const math::Vec3d f = inFace(axis, cell.corners[k]);
			const double cosine = f.z / math::length(f);
			if (!(cosine > minCosine))
			{
				stats.tooWide = true;
				return;
			}
			p[k] = math::Vec2d{f.x / f.z, f.y / f.z};
			stats.min = math::min(stats.min, p[k]);
			stats.max = math::max(stats.max, p[k]);
			stats.cosine = std::min(stats.cosine, cosine);
		}
		const math::Vec2d du = 0.5 * ((p[1] - p[0]) + (p[3] - p[2]));
		const math::Vec2d dv = 0.5 * ((p[2] - p[0]) + (p[3] - p[1]));
		stats.stretch = std::min(stats.stretch, leastSingularValue(du, dv));
		stats.any = true;
	}

	// A face's texels at focal length `focal`: centred on its cells, so the shader's float coordinates
	// stay small, and wide enough that every ray's bilinear footprint, plus the guard, is drawn.
	static CaptureFace faceOver(FaceAxis axis, const FaceStats& stats, double focal)
	{
		CaptureFace face;
		face.axis = axis;
		face.focal = focal;
		face.principalPoint = -focal * 0.5 * (stats.min + stats.max);
		const math::Vec2d low = focal * stats.min + face.principalPoint;
		const math::Vec2d high = focal * stats.max + face.principalPoint;
		face.first = math::Vec2i{int(std::floor(low.x)) - kGuard, int(std::floor(low.y)) - kGuard};
		face.last = math::Vec2i{int(std::floor(high.x)) + 1 + kGuard, int(std::floor(high.y)) + 1 + kGuard};
		face.nearScale = stats.cosine;
		return face;
	}

	static double texelCount(const CaptureFace& face)
	{
		return double(face.last.x - face.first.x + 1) * double(face.last.y - face.first.y + 1);
	}

	// [first, last] cut into pieces at most `side` drawn texels across, each drawing one texel past its
	// interior: the boundaries, first to last.
	static std::vector<int> cuts(int first, int last, std::uint32_t side)
	{
		const int span = last - first;
		const int interior = int(side) - 1;
		const int pieces = std::max(1, (span + interior - 1) / interior);
		const int step = (span + pieces - 1) / pieces;
		std::vector<int> at;
		for (int k = 0; k < pieces; ++k)
			at.push_back(first + k * step);
		at.push_back(last);
		return at;
	}

	static std::string statusName(camera::ProjectionStatus status)
	{
		switch (status)
		{
			case camera::ProjectionStatus::Ok:
				return "ok";
			case camera::ProjectionStatus::BehindCamera:
				return "behind the camera";
			case camera::ProjectionStatus::OutsideDomain:
				return "outside the distortion's domain";
			case camera::ProjectionStatus::NotConverged:
				return "its inverse did not converge";
		}
		return "unknown";
	}

	RayTableResult RayTable::build(const camera::CameraModel& model, const CaptureSettings& settings)
	{
		std::vector<std::string> problems;
		if (settings.samples == 0)
			problems.push_back("a capture needs at least one ray a pixel");
		if (!(std::isfinite(settings.oversampling) && settings.oversampling >= 1))
			problems.push_back("a capture's intermediate image must be at least as fine as its camera (oversampling of 1 or more)");
		if (!(std::isfinite(settings.interpolationTolerance) && settings.interpolationTolerance > 0))
			problems.push_back("a capture's interpolation tolerance must be positive and finite");
		if (settings.maxTileSide < kMinTileSide)
			problems.push_back(string::format("a capture's tiles must be at least {} texels a side", kMinTileSide));
		const std::uint32_t width = model.image().width;
		const std::uint32_t height = model.image().height;
		if (2 * (std::size_t(width) + 1) * kEntryBytes > kBufferBytes)
			problems.push_back(string::format("a capture cannot remap an image {} pixels wide: two rows exceed a storage buffer", width));
		if (!problems.empty())
			return {std::nullopt, std::move(problems)};

		RayTable table(model, settings);
		const std::size_t row = std::size_t(width) + 1;
		const std::size_t rows = std::size_t(height) + 1;

		// lain's own unprojection at every pixel corner. A row records its first corner without a ray.
		table.m_corners.assign(row * rows, math::Vec3f{0.0f});
		std::vector<std::size_t> missing(rows, 0);
		std::vector<std::pair<std::size_t, camera::ProjectionStatus>> firstMissing(rows);
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(rows),
					[&](std::ptrdiff_t y)
					{
						for (std::size_t x = 0; x < row; ++x)
						{
							const camera::Unprojection<double> ray = camera::unproject(model, double(x) - 0.5, double(y) - 0.5);
							if (!ray.ok())
							{
								if (missing[std::size_t(y)]++ == 0)
									firstMissing[std::size_t(y)] = {x, ray.status};
								continue;
							}
							table.m_corners[std::size_t(y) * row + x] = math::Vec3f{float(ray.x), float(ray.y), float(ray.z)};
						}
					});
		std::size_t missingCount = 0;
		for (std::size_t y = 0; y < rows; ++y)
		{
			if (missing[y] > 0 && missingCount == 0)
			{
				problems.push_back(string::format("pixel corner ({}, {}) has no ray: {}", double(firstMissing[y].first) - 0.5,
												  double(y) - 0.5, statusName(firstMissing[y].second)));
			}
			missingCount += missing[y];
		}
		if (missingCount > 1)
			problems.back() += string::format(", and {} other corners have none", missingCount - 1);
		if (!problems.empty())
			return {std::nullopt, std::move(problems)};

		// How well the lattice stands for the model: each pixel centre's interpolated ray, projected back.
		std::vector<double> rowError(height, 0);
		std::vector<std::uint32_t> rowWorst(height, 0);
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(height),
					[&](std::ptrdiff_t y)
					{
						for (std::uint32_t x = 0; x < width; ++x)
						{
							const math::Vec3d d = table.direction(double(x), double(y));
							const camera::Projection<double> p = camera::project(model, d.x, d.y, d.z);
							const double error = p.ok() ? std::hypot(p.u - double(x), p.v - double(y)) : std::numeric_limits<double>::infinity();
							if (!(error <= rowError[std::size_t(y)]))
							{
								rowError[std::size_t(y)] = error;
								rowWorst[std::size_t(y)] = x;
							}
						}
					});
		std::size_t worstRow = 0;
		for (std::size_t y = 0; y < height; ++y)
		{
			if (!(rowError[y] <= rowError[worstRow]))
				worstRow = y;
		}
		table.m_interpolationError = rowError[worstRow];
		if (!(table.m_interpolationError <= settings.interpolationTolerance))
		{
			problems.push_back(string::format("the pixel-corner rays interpolate this model {:.3g} px from where they should be, at pixel "
											  "({}, {}), above the tolerance of {} px",
											  table.m_interpolationError, rowWorst[worstRow], worstRow, settings.interpolationTolerance));
			return {std::nullopt, std::move(problems)};
		}

		// Two candidate intermediate images: the cube's faces (indices 0 to 4, in kAxes' order), each
		// serving the cells whose rays may be given to it, and the front face alone (index 5) serving
		// every cell.
		constexpr std::size_t kFrontOnly = kAxes.size();
		std::vector<std::array<FaceStats, kAxes.size() + 1>> rowStats(height);
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(height),
					[&](std::ptrdiff_t y)
					{
						std::array<FaceStats, kAxes.size() + 1>& stats = rowStats[std::size_t(y)];
						for (std::size_t x = 0; x < width; ++x)
						{
							const Cell cell = cellAt(table.m_corners, row, x, std::size_t(y));
							for (std::size_t a = 0; a < kAxes.size(); ++a)
							{
								if (member(kAxes[a], cell))
									addCell(stats[a], kAxes[a], cell, 0.0);
							}
							addCell(stats[kFrontOnly], FaceAxis::Front, cell, kFrontOnlyCosine);
						}
					});
		std::array<FaceStats, kAxes.size() + 1> stats;
		for (const auto& r : rowStats)
		{
			for (std::size_t a = 0; a < stats.size(); ++a)
				stats[a].merge(r[a]);
		}

		const double m = settings.oversampling;
		std::vector<CaptureFace> cubeFaces;
		double cubeStretch = std::numeric_limits<double>::infinity();
		for (std::size_t a = 0; a < kAxes.size(); ++a)
		{
			if (stats[a].tooWide)
			{
				problems.push_back("a pixel spans too wide an angle for the capture's faces");
				return {std::nullopt, std::move(problems)};
			}
			if (stats[a].any)
				cubeStretch = std::min(cubeStretch, stats[a].stretch);
		}
		if (!(cubeStretch > 0 && std::isfinite(cubeStretch)))
		{
			problems.push_back("the camera's pixels do not span an area on the capture's faces");
			return {std::nullopt, std::move(problems)};
		}
		// One focal length for every face, so the texel density and the mip level a texture is sampled
		// at do not step where two faces meet.
		double cubeTexels = 0;
		for (std::size_t a = 0; a < kAxes.size(); ++a)
		{
			if (!stats[a].any)
				continue;
			cubeFaces.push_back(faceOver(kAxes[a], stats[a], m / cubeStretch));
			cubeTexels += texelCount(cubeFaces.back());
		}
		const FaceStats& front = stats[kFrontOnly];
		const bool frontUsable = front.any && !front.tooWide && front.stretch > 0 && std::isfinite(front.stretch);
		const CaptureFace frontFace = frontUsable ? faceOver(FaceAxis::Front, front, m / front.stretch) : CaptureFace{};
		table.m_cube = !(frontUsable && texelCount(frontFace) <= cubeTexels);
		table.m_faces = table.m_cube ? std::move(cubeFaces) : std::vector<CaptureFace>{frontFace};
		for (const CaptureFace& face : table.m_faces)
		{
			const double limit = double(1 << 30);
			if (!(std::abs(double(face.first.x)) < limit && std::abs(double(face.first.y)) < limit &&
				  std::abs(double(face.last.x)) < limit && std::abs(double(face.last.y)) < limit))
			{
				problems.push_back("the capture's intermediate image would be too large");
				return {std::nullopt, std::move(problems)};
			}
		}

		// Each face cut into tiles, in rows of tiles, a face after another.
		std::vector<std::pair<std::size_t, std::size_t>> faceTiles; // per face: its first tile and how many
		for (std::size_t f = 0; f < table.m_faces.size(); ++f)
		{
			const CaptureFace& face = table.m_faces[f];
			const std::vector<int> across = cuts(face.first.x, face.last.x, settings.maxTileSide);
			const std::vector<int> down = cuts(face.first.y, face.last.y, settings.maxTileSide);
			faceTiles.push_back({table.m_tiles.size(), (across.size() - 1) * (down.size() - 1)});
			for (std::size_t j = 0; j + 1 < down.size(); ++j)
			{
				for (std::size_t i = 0; i + 1 < across.size(); ++i)
				{
					CaptureTile tile;
					tile.face = f;
					tile.first = math::Vec2i{across[i], down[j]};
					tile.last = math::Vec2i{across[i + 1], down[j + 1]};
					tile.openLeft = i == 0;
					tile.openRight = i + 2 == across.size();
					tile.openTop = j == 0;
					tile.openBottom = j + 2 == down.size();
					table.m_tiles.push_back(tile);
				}
			}
		}

		// The camera pixels each tile may serve: those whose cell's corners, on the tile's face and a
		// texel either side, overlap what it accepts.
		struct PixelRect
		{
			math::Vec2i first{std::numeric_limits<int>::max()};
			math::Vec2i end{std::numeric_limits<int>::min()};
		};
		std::vector<std::vector<PixelRect>> rowRects(height, std::vector<PixelRect>(table.m_tiles.size()));
		task::range(std::ptrdiff_t{0}, std::ptrdiff_t(height),
					[&](std::ptrdiff_t y)
					{
						std::vector<PixelRect>& rects = rowRects[std::size_t(y)];
						for (std::size_t x = 0; x < width; ++x)
						{
							const Cell cell = cellAt(table.m_corners, row, x, std::size_t(y));
							for (std::size_t f = 0; f < table.m_faces.size(); ++f)
							{
								if (table.m_cube && !member(table.m_faces[f].axis, cell))
									continue;
								math::Vec2d low{std::numeric_limits<double>::infinity()};
								math::Vec2d high{-std::numeric_limits<double>::infinity()};
								for (const math::Vec3d& c : cell.corners)
								{
									const math::Vec2d t = table.onFace(f, c);
									low = math::min(low, t);
									high = math::max(high, t);
								}
								for (std::size_t t = faceTiles[f].first; t < faceTiles[f].first + faceTiles[f].second; ++t)
								{
									const CaptureTile& tile = table.m_tiles[t];
									const bool across = (tile.openLeft || high.x + 1 >= tile.first.x) && (tile.openRight || low.x - 1 < tile.last.x);
									const bool down = (tile.openTop || high.y + 1 >= tile.first.y) && (tile.openBottom || low.y - 1 < tile.last.y);
									if (across && down)
									{
										rects[t].first = math::min(rects[t].first, math::Vec2i{int(x), int(y)});
										rects[t].end = math::max(rects[t].end, math::Vec2i{int(x) + 1, int(y) + 1});
									}
								}
							}
						}
					});
		for (std::size_t t = 0; t < table.m_tiles.size(); ++t)
		{
			PixelRect rect;
			for (const std::vector<PixelRect>& rects : rowRects)
			{
				rect.first = math::min(rect.first, rects[t].first);
				rect.end = math::max(rect.end, rects[t].end);
			}
			if (rect.first.x < rect.end.x && rect.first.y < rect.end.y)
			{
				table.m_tiles[t].pixelFirst = rect.first;
				table.m_tiles[t].pixelEnd = rect.end;
			}
		}

		return {std::move(table), {}};
	}
} // namespace lain::render
