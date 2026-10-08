#pragma once

// A textured scene drawn in plain C++, for feature extraction to work on real pixels without a library
// of its own to draw them: flat rectangles carrying a seeded procedural texture, seen through a lain
// camera model, nearest surface first. Shared by the tests that need SIFT to find something (the
// OpenCV feature producer, targetless registration end to end), as syntheticfootage.h is by the ones
// that need a board.
//
// The texture is value noise over several octaves with soft blobs on top, so a detector finds stable
// extrema at several scales. Every ray is unprojected through lain's own model, so a distorted camera
// is drawn exactly, and each pixel averages s x s rays. A disc may move through the scene frame by
// frame, which is the moving content targetless registration must drop, and so may a printed board,
// which is what lets one rig be registered by both methods from the same footage.

#include "syntheticfootage.h"

#include <lain/camera/board/rendering.h>
#include <lain/camera/cameramodel.h>
#include <lain/camera/projection.h>
#include <lain/image/image.h>
#include <lain/math/rigidtransform.h>
#include <lain/media/framesource.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace lain::camera::synthetic
{
	// --- the texture ---------------------------------------------------------------------

	inline std::uint64_t mix(std::uint64_t x)
	{
		x += 0x9e3779b97f4a7c15ull;
		x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
		x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
		return x ^ (x >> 31);
	}

	// A value in [0, 1) for lattice point (i, j).
	inline double lattice(std::uint64_t seed, std::int64_t i, std::int64_t j)
	{
		const std::uint64_t key = mix(seed ^ mix(std::uint64_t(i) * 0x632be59bd9b4e019ull ^ std::uint64_t(j) * 0x8cb92ba72f3d8dd7ull));
		return double(key >> 11) * 0x1.0p-53;
	}

	inline double valueNoise(std::uint64_t seed, double x, double y)
	{
		const double fi = std::floor(x), fj = std::floor(y);
		const auto i = std::int64_t(fi), j = std::int64_t(fj);
		const double fx = x - fi, fy = y - fj;
		const double sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
		const double top = lattice(seed, i, j) * (1 - sx) + lattice(seed, i + 1, j) * sx;
		const double bottom = lattice(seed, i, j + 1) * (1 - sx) + lattice(seed, i + 1, j + 1) * sx;
		return top * (1 - sy) + bottom * sy;
	}

	struct Texture
	{
		std::uint64_t seed = 1;
		double cell = 0.04; // metres: the finest noise octave; blobs sit on a lattice five times coarser
	};

	// The texture's grey level, 0 to 255, at (u, v) metres on its surface.
	inline double textureAt(const Texture& t, double u, double v)
	{
		// Four octaves, the coarser ones stronger.
		double value = 0;
		double weight = 0;
		for (int o = 0; o < 4; ++o)
		{
			const double size = t.cell * double(1 << o);
			const double w = double(1 << o);
			value += w * valueNoise(t.seed + std::uint64_t(o), u / size, v / size);
			weight += w;
		}
		value /= weight;
		// A soft disc in each blob cell, bright or dark, inside its own cell.
		const double blob = 5 * t.cell;
		const double ci = std::floor(u / blob), cj = std::floor(v / blob);
		const auto i = std::int64_t(ci), j = std::int64_t(cj);
		const std::uint64_t s = t.seed ^ 0xb10b5ull;
		const double cx = (ci + 0.3 + 0.4 * lattice(s, i, j)) * blob;
		const double cy = (cj + 0.3 + 0.4 * lattice(s + 1, i, j)) * blob;
		const double radius = (0.08 + 0.12 * lattice(s + 2, i, j)) * blob;
		const double tone = lattice(s + 3, i, j) < 0.5 ? 0.08 : 0.92;
		const double d = std::hypot(u - cx, v - cy);
		const double edge = std::clamp((radius - d) / (0.25 * radius) + 0.5, 0.0, 1.0);
		value = value * (1 - edge) + tone * edge;
		return 255.0 * std::clamp(value, 0.0, 1.0);
	}

	// --- the scene -----------------------------------------------------------------------

	// A textured rectangle: `origin` is a corner, `u` and `v` unit axes along its sides, in the
	// reference frame, with the reference frame's +Y down.
	struct Plane
	{
		math::Vec3d origin{0.0};
		math::Vec3d u{1.0, 0.0, 0.0};
		math::Vec3d v{0.0, 1.0, 0.0};
		double width = 1;
		double height = 1;
		Texture texture;
	};

	// A textured disc facing -Z that moves `velocity` metres per frame from `start`.
	struct Occluder
	{
		math::Vec3d start{0.0};
		math::Vec3d velocity{0.0};
		double radius = 0.3;
		Texture texture{77, 0.03};

		math::Vec3d centre(std::size_t frame) const { return start + double(frame) * velocity; }
	};

	// A printed board moving through the scene: its raster on a flat sheet, in frame f at
	// `sheets[f]` (made by boardSurface), and nowhere in a frame past the last.
	struct BoardSurface
	{
		struct Sheet
		{
			math::Vec3d corner{0.0}; // where the raster's coordinate (-1, -1) lies, in the reference frame
			math::Vec3d u{1.0, 0.0, 0.0};
			math::Vec3d v{0.0, 1.0, 0.0};
		};

		std::shared_ptr<const board::Rendering> rendering;
		double perMetre = 1; // raster pixels per metre of board
		std::vector<Sheet> sheets;
	};

	// The board `rendering` printed with squares of `squareMetres`, at `referenceFromBoard[f]` in frame
	// f, in the board frame's convention (X right, Y down, Z into the board). The sheet is the
	// raster's bilinear footprint, from coordinate -1 to its size on each axis, so a board in front of
	// nothing draws exactly as syntheticfootage.h's view does; that half raster pixel beyond the edge
	// pixels is invisible in front of anything else.
	inline BoardSurface boardSurface(std::shared_ptr<const board::Rendering> rendering, double squareMetres,
									 const std::vector<math::RigidTransformd>& referenceFromBoard)
	{
		BoardSurface b;
		b.perMetre = double(rendering->request.pixelsPerSquare) / squareMetres;
		// Board metres -> raster pixel is x * perMetre + margin - 0.5 (syntheticfootage.h's view), so
		// raster coordinate -1 is board coordinate (-0.5 - margin) / perMetre.
		const double first = (-0.5 - double(rendering->request.marginPixels)) / b.perMetre;
		for (const math::RigidTransformd& pose : referenceFromBoard)
			b.sheets.push_back({pose.apply(math::Vec3d{first, first, 0.0}), pose.rotate(math::Vec3d{1.0, 0.0, 0.0}),
								pose.rotate(math::Vec3d{0.0, 1.0, 0.0})});
		b.rendering = std::move(rendering);
		return b;
	}

	struct Scene
	{
		std::vector<Plane> planes;
		std::optional<Occluder> occluder;
		std::optional<BoardSurface> board;
		double background = 128;
		double noise = 0;			 // grey levels: the standard deviation of each pixel's noise
		std::uint64_t noiseSeed = 1; // with the frame and a render's own seed, decides every pixel's noise
	};

	// The default scene: a back wall, a floor, and a box standing in front of the wall, so no two
	// surfaces are coplanar and a camera sees several depths. About 3 m in front of cameras on an arc
	// around the origin.
	inline Scene defaultScene(std::uint64_t seed = 1, double cell = 0.04)
	{
		Scene s;
		const auto texture = [&](std::uint64_t n)
		{ return Texture{seed * 101 + n, cell}; };
		s.planes.push_back({{-3.0, -1.6, 1.5}, {1, 0, 0}, {0, 1, 0}, 6.0, 2.4, texture(1)}); // back wall
		s.planes.push_back({{-3.0, 0.8, -3.0}, {1, 0, 0}, {0, 0, 1}, 6.0, 4.5, texture(2)}); // floor
		// The box: 0.8 m wide, 0.9 m tall, 0.6 m deep, standing on the floor.
		const double x0 = -0.6, x1 = 0.2, y0 = -0.1, y1 = 0.8, z0 = -0.2, z1 = 0.4;
		s.planes.push_back({{x0, y0, z0}, {1, 0, 0}, {0, 1, 0}, x1 - x0, y1 - y0, texture(3)});	 // front
		s.planes.push_back({{x0, y0, z1}, {0, 0, -1}, {0, 1, 0}, z1 - z0, y1 - y0, texture(4)}); // left
		s.planes.push_back({{x1, y0, z0}, {0, 0, 1}, {0, 1, 0}, z1 - z0, y1 - y0, texture(5)});	 // right
		s.planes.push_back({{x0, y0, z1}, {1, 0, 0}, {0, 0, -1}, x1 - x0, z1 - z0, texture(6)}); // top
		return s;
	}

	// A camera at `position` looking at `target`, with the reference frame's +Y as down.
	inline math::RigidTransformd lookingAt(const math::Vec3d& position, const math::Vec3d& target)
	{
		const math::Vec3d z = math::normalize(target - position);
		const math::Vec3d x = math::normalize(math::cross(math::Vec3d{0, 1, 0}, z));
		const math::Vec3d y = math::cross(z, x);
		return math::RigidTransformd{math::quat_cast(math::Mat3d{x, y, z}), position};
	}

	// A camera on an arc `radius` from `target`, `angle` radians round it and `rise` above it, looking
	// at it. The defaults stand round the middle of the default scene.
	inline math::RigidTransformd arcCamera(double angle, double radius = 3.2, const math::Vec3d& target = {-0.2, 0.2, 0.4},
										   double rise = 0.5)
	{
		return lookingAt(target + math::Vec3d{radius * std::sin(angle), -rise, -radius * std::cos(angle)}, target);
	}

	// What a ray hits first: the point in the reference frame, its grey level, and which surface
	// (an index into Scene::planes, kOccluderSurface or kBoardSurface).
	struct Hit
	{
		double distance = 0;
		math::Vec3d point{0.0};
		double grey = 0;
		int surface = 0;
	};

	constexpr int kOccluderSurface = -1;
	constexpr int kBoardSurface = -2;

	// Where a ray from `origin` along `direction` meets the rectangle at `corner` spanned by unit axes
	// `u` and `v`, `width` x `height`: its distance `t` and its coordinates (a, b) along the axes.
	// Plain arithmetic rather than GLM's calls: a Debug build spent nine tenths of a render in them.
	inline bool meetRectangle(const math::Vec3d& corner, const math::Vec3d& u, const math::Vec3d& v, double width,
							  double height, const math::Vec3d& origin, const math::Vec3d& direction, double& t, double& a,
							  double& b)
	{
		const double nx = u.y * v.z - u.z * v.y, ny = u.z * v.x - u.x * v.z, nz = u.x * v.y - u.y * v.x;
		const double along = direction.x * nx + direction.y * ny + direction.z * nz;
		if (std::abs(along) < 1e-12)
			return false;
		const double ox = corner.x - origin.x, oy = corner.y - origin.y, oz = corner.z - origin.z;
		t = (ox * nx + oy * ny + oz * nz) / along;
		// The hit, relative to the corner.
		const double rx = t * direction.x - ox, ry = t * direction.y - oy, rz = t * direction.z - oz;
		a = rx * u.x + ry * u.y + rz * u.z;
		b = rx * v.x + ry * v.y + rz * v.z;
		return a >= 0 && a <= width && b >= 0 && b <= height;
	}

	inline std::optional<Hit> cast(const Scene& scene, const math::Vec3d& origin, const math::Vec3d& direction,
								   std::size_t frame)
	{
		std::optional<Hit> best;
		const auto consider = [&](double t, const math::Vec3d& p, double grey, int surface)
		{
			if (t > 1e-9 && (!best || t < best->distance))
				best = Hit{t, p, grey, surface};
		};
		for (std::size_t k = 0; k < scene.planes.size(); ++k)
		{
			const Plane& plane = scene.planes[k];
			double t = 0, a = 0, b = 0;
			if (meetRectangle(plane.origin, plane.u, plane.v, plane.width, plane.height, origin, direction, t, a, b))
				consider(t, origin + t * direction, textureAt(plane.texture, a, b), int(k));
		}
		if (scene.board && frame < scene.board->sheets.size())
		{
			const BoardSurface& board = *scene.board;
			const BoardSurface::Sheet& sheet = board.sheets[frame];
			const image::Image& raster = board.rendering->raster;
			const double width = double(raster.width() + 1) / board.perMetre;
			const double height = double(raster.height() + 1) / board.perMetre;
			double t = 0, a = 0, b = 0;
			if (meetRectangle(sheet.corner, sheet.u, sheet.v, width, height, origin, direction, t, a, b))
				consider(t, origin + t * direction, sample(raster, a * board.perMetre - 1.0, b * board.perMetre - 1.0),
						 kBoardSurface);
		}
		if (scene.occluder && std::abs(direction.z) > 1e-12)
		{
			const Occluder& o = *scene.occluder;
			const math::Vec3d c = o.centre(frame);
			const double t = (c.z - origin.z) / direction.z;
			const math::Vec3d p = origin + t * direction;
			const double a = p.x - c.x, b = p.y - c.y;
			if (a * a + b * b <= o.radius * o.radius)
				consider(t, p, textureAt(o.texture, a + o.radius, b + o.radius), kOccluderSurface);
		}
		return best;
	}

	// What the centre of `pixel`'s ray hits in frame `frame`, seen by `model` at `referenceFromCamera`.
	inline std::optional<Hit> truthAt(const Scene& scene, const CameraModel& model,
									  const math::RigidTransformd& referenceFromCamera, const math::Vec2d& pixel,
									  std::size_t frame = 0)
	{
		const Unprojection<double> ray = unproject(model, pixel.x, pixel.y);
		if (!ray.ok())
			return std::nullopt;
		return cast(scene, referenceFromCamera.translation(), referenceFromCamera.rotate(math::Vec3d{ray.x, ray.y, ray.z}),
					frame);
	}

	// What `model` at `referenceFromCamera` sees of `scene` in frame `frame`, as 8-bit grey: each pixel
	// the mean of `samples` x `samples` rays, plus the scene's noise for this frame and `seed`.
	inline image::Image render(const Scene& scene, const CameraModel& model, const math::RigidTransformd& referenceFromCamera,
							   std::size_t frame = 0, int samples = 2, std::uint64_t seed = 0)
	{
		const int w = int(model.image().width), h = int(model.image().height);
		image::Image out{w, h, image::PixelFormat::Gray8, image::ColorSpace::sRGB};
		std::mt19937_64 engine(mix(scene.noiseSeed ^ mix(seed ^ mix(frame + 1))));
		const auto gaussian = [&engine]
		{
			// Box-Muller over mt19937_64, since std::normal_distribution is implementation-defined.
			const double u1 = (double(engine() >> 11) + 0.5) * 0x1.0p-53;
			const double u2 = double(engine() >> 11) * 0x1.0p-53;
			return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
		};
		const math::Vec3d origin = referenceFromCamera.translation();
		for (int y = 0; y < h; ++y)
		{
			for (int x = 0; x < w; ++x)
			{
				double sum = 0;
				for (int j = 0; j < samples; ++j)
				{
					for (int i = 0; i < samples; ++i)
					{
						const double px = x + (i + 0.5) / samples - 0.5;
						const double py = y + (j + 0.5) / samples - 0.5;
						const Unprojection<double> ray = unproject(model, px, py);
						double grey = scene.background;
						if (ray.ok())
						{
							if (const std::optional<Hit> hit =
									cast(scene, origin, referenceFromCamera.rotate(math::Vec3d{ray.x, ray.y, ray.z}), frame))
								grey = hit->grey;
						}
						sum += grey;
					}
				}
				double value = sum / double(samples * samples);
				if (scene.noise > 0)
					value += scene.noise * gaussian();
				out.data()[std::size_t(y) * std::size_t(w) + std::size_t(x)] =
					std::uint8_t(std::clamp(std::lround(value), 0L, 255L));
			}
		}
		return out;
	}

	// One camera's footage of a scene, drawn when a frame is asked for.
	class RenderedSource : public media::FrameSource
	{
	public:
		RenderedSource(std::string name, Scene scene, CameraModel model, math::RigidTransformd referenceFromCamera,
					   std::size_t frames, std::uint64_t seed = 0, int samples = 2)
			: FrameSource{core::Uri{"/rendered/" + name}, specOf(model), frames}
			, m_scene(std::move(scene))
			, m_model(std::move(model))
			, m_pose(referenceFromCamera)
			, m_seed(seed)
			, m_samples(samples)
		{
		}

		static media::FrameSpec specOf(const CameraModel& model)
		{
			media::FrameSpec s;
			s.extent = {int(model.image().width), int(model.image().height)};
			s.pixelFormat = image::PixelFormat::Gray8;
			s.colorSpace = image::ColorSpace::sRGB;
			return s;
		}

	protected:
		// Each frame is drawn once and kept, since a test that extracts the same footage twice would
		// otherwise pay for every render twice. FrameSource calls this under its own lock.
		image::Image decodeFrame(std::size_t ordinal) const override
		{
			auto found = m_drawn.find(ordinal);
			if (found == m_drawn.end())
				found = m_drawn.emplace(ordinal, render(m_scene, m_model, m_pose, ordinal, m_samples, m_seed)).first;
			return found->second;
		}

	private:
		Scene m_scene;
		CameraModel m_model;
		math::RigidTransformd m_pose;
		std::uint64_t m_seed;
		int m_samples;
		mutable std::map<std::size_t, image::Image> m_drawn;
	};
} // namespace lain::camera::synthetic
