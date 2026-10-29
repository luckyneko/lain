#include "lain/rigplan/scene.h"

#include <lain/camera/board/rendering.h>
#include <lain/image/procedural.h>
#include <lain/io/image/load.h>
#include <lain/io/mesh/load.h>
#include <lain/log/log.h>
#include <lain/render/renderer.h> // storedTexture: each texture made in the form the device keeps
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <utility>
#include <variant>

namespace lain::rigplan
{
	// One flat rectangle of the plan: centred on `centre`, seen from the side `facing` points to,
	// `width` metres across and `height` along `up`. Across is up x facing, so it reads left to right
	// from that side.
	struct Surface
	{
		SceneOwner object;	  // what it belongs to
		std::string owner;	  // in a problem's text: "the room's floor", "plane 'partition'"
		std::string identity; // what its procedural pattern is mixed with, stable for the surface's life
		math::Vec3d centre;
		math::Vec3d facing;
		math::Vec3d up;
		double width;
		double height;
		const Material* material;
	};

	// Every flat surface of `plan`, in one order: the room's, its planes', its boxes'. One walk serves
	// both what is loaded and what is drawn, so the two cannot disagree about which surfaces exist.
	static std::vector<Surface> surfacesOf(const RigPlan& plan)
	{
		const math::Vec3d X{1.0, 0.0, 0.0}, Y{0.0, 1.0, 0.0}, Z{0.0, 0.0, 1.0};
		std::vector<Surface> surfaces;
		if (plan.room)
		{
			const Room& room = *plan.room;
			const double cx = room.centre.x, cy = room.centre.y;
			const double sx = room.size.x, sy = room.size.y, sz = room.size.z;
			auto add = [&](const char* name, const RoomSurface& s, math::Vec3d centre, math::Vec3d facing, math::Vec3d up, double w, double h)
			{
				if (s.present)
					surfaces.push_back({{SceneObject::Room, core::Uuid{}}, string::format("the room's {}", name), string::format("room/{}", name), centre, facing, up, w, h, &s.material});
			};
			// Each faces in. A floor reads with north at its top seen from above, a ceiling seen from below.
			add("floor", room.floor, {cx, cy, 0.0}, Z, Y, sx, sy);
			add("ceiling", room.ceiling, {cx, cy, sz}, -Z, Y, sx, sy);
			add("north wall", room.north, {cx, cy + sy / 2, sz / 2}, -Y, Z, sx, sz);
			add("south wall", room.south, {cx, cy - sy / 2, sz / 2}, Y, Z, sx, sz);
			add("east wall", room.east, {cx + sx / 2, cy, sz / 2}, -X, Z, sy, sz);
			add("west wall", room.west, {cx - sx / 2, cy, sz / 2}, X, Z, sy, sz);
		}

		for (const Plane& plane : plan.planes)
		{
			// Turned as a camera is: at rest it faces a camera at rest, whose up is the plan's +Z.
			const math::RigidTransformd pose = mountFromPlacement(plane.centre, plane.aim);
			surfaces.push_back({{SceneObject::Plane, plane.id}, string::format("plane '{}'", plane.name), plane.id.toString(), plane.centre, pose.rotate({0.0, 0.0, -1.0}), pose.rotate({0.0, -1.0, 0.0}), plane.size.x, plane.size.y, &plane.material});
		}

		for (const Box& box : plan.boxes)
		{
			const math::Quatd yaw = math::angleAxis(math::radians(box.yawDegrees), Z);
			const math::Vec3d h = box.size / 2.0;
			const math::Vec3d c = box.centre;
			auto add = [&](const char* face, math::Vec3d centre, math::Vec3d facing, math::Vec3d up, double w, double height)
			{
				surfaces.push_back({{SceneObject::Box, box.id}, string::format("box '{}'", box.name), string::format("{}/{}", box.id.toString(), face), centre, facing, up, w, height, &box.material});
			};
			// Each faces out, the sides reading upright, the top with the box's +Y at its top.
			add("+x", c + yaw * math::Vec3d{h.x, 0.0, 0.0}, yaw * X, Z, box.size.y, box.size.z);
			add("-x", c - yaw * math::Vec3d{h.x, 0.0, 0.0}, -(yaw * X), Z, box.size.y, box.size.z);
			add("+y", c + yaw * math::Vec3d{0.0, h.y, 0.0}, yaw * Y, Z, box.size.x, box.size.z);
			add("-y", c - yaw * math::Vec3d{0.0, h.y, 0.0}, -(yaw * Y), Z, box.size.x, box.size.z);
			add("+z", c + math::Vec3d{0.0, 0.0, h.z}, Z, yaw * Y, box.size.x, box.size.y);
			add("-z", c - math::Vec3d{0.0, 0.0, h.z}, -Z, yaw * Y, box.size.x, box.size.y);
		}
		return surfaces;
	}

	// The material's seed mixed with the surface's identity, FNV-1a then splitmix64, so the same on
	// every platform and different for every surface.
	static std::uint64_t surfaceSeed(std::uint64_t seed, const std::string& identity)
	{
		std::uint64_t hash = 1469598103934665603ull;
		for (const unsigned char c : identity)
		{
			hash ^= c;
			hash *= 1099511628211ull;
		}
		std::uint64_t z = seed ^ hash;
		z += 0x9e3779b97f4a7c15ull;
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		return z ^ (z >> 31);
	}

	static BakeKey bakeOf(const image::ProceduralTexture& texture, const Surface& surface)
	{
		const double density =
			std::min({kBakedTexelsPerCell / texture.cell, kMaxBakedTexels / surface.width, kMaxBakedTexels / surface.height});
		const auto texels = [&](double metres)
		{ return std::clamp(int(std::ceil(metres * density)), 1, kMaxBakedTexels); };
		return {surfaceSeed(texture.seed, surface.identity), texture.cell, density, texels(surface.width), texels(surface.height)};
	}

	// Where a surface's material puts its texture at (s, t), metres from its corner along across and up:
	// a picture once every tile, upright from the side the surface faces; a procedural surface its own
	// bake, covering it from its corner, so texel (i, j) is proceduralGrey at ((i + 0.5) / d, (j + 0.5) / d)
	// metres along across and up (the renderer reads OBJ's v upward, so v runs down from 1 to put row j at
	// t = j / d); a flat colour nowhere.
	static std::function<math::Vec2f(double, double)> uvOf(const Surface& surface)
	{
		if (const auto* image = std::get_if<ImageTexture>(&surface.material->surface))
		{
			const math::Vec2d tile = image->tile;
			return [tile](double s, double t)
			{ return math::Vec2f{float(s / tile.x), float(t / tile.y)}; };
		}
		if (const auto* procedural = std::get_if<image::ProceduralTexture>(&surface.material->surface))
		{
			const BakeKey key = bakeOf(*procedural, surface);
			return [key](double s, double t)
			{ return math::Vec2f{float(s * key.texelsPerMetre / key.width), float(1 - t * key.texelsPerMetre / key.height)}; };
		}
		return [](double, double)
		{ return math::Vec2f{0.0f}; };
	}

	// A surface as a mesh in the plan frame, its texture coordinates at each corner from (s, t), metres
	// from its corner along across and up.
	static std::shared_ptr<const mesh::Mesh> rectangle(const Surface& surface, const std::function<math::Vec2f(double, double)>& uvAt)
	{
		const math::Vec3d across = math::cross(surface.up, surface.facing);
		const math::Vec3d origin = surface.centre - across * (surface.width / 2) - surface.up * (surface.height / 2);
		mesh::Mesh mesh;
		const std::pair<double, double> corners[4] = {{0.0, 0.0}, {surface.width, 0.0}, {surface.width, surface.height}, {0.0, surface.height}};
		for (const auto& [s, t] : corners)
		{
			mesh.positions.push_back(math::Vec3f{origin + across * s + surface.up * t});
			mesh.normals.push_back(math::Vec3f{surface.facing});
			mesh.uvs.push_back(uvAt(s, t));
		}
		mesh.indices = {0, 1, 2, 0, 2, 3};
		mesh.parts = {{0, 0, 6}};
		mesh.materials = {mesh::Material{}};
		return std::make_shared<const mesh::Mesh>(std::move(mesh));
	}

	// A pole as a mesh in its mount's frame: a cylinder of `segments` sides from the floor (Z = 0) to its
	// top, capped there, its normals pointing out. 32 sides keep a 5 cm pole within 0.12 mm of round.
	static std::shared_ptr<const mesh::Mesh> cylinder(const Pole& pole)
	{
		constexpr int segments = 32;
		const float radius = float(pole.diameter / 2);
		const float height = float(pole.height);
		mesh::Mesh mesh;
		for (int i = 0; i < segments; ++i)
		{
			const double angle = 2 * 3.14159265358979323846 * i / segments;
			const math::Vec3f out{float(std::cos(angle)), float(std::sin(angle)), 0.0f};
			mesh.positions.push_back(out * radius);
			mesh.positions.push_back(out * radius + math::Vec3f{0.0f, 0.0f, height});
			mesh.normals.push_back(out);
			mesh.normals.push_back(out);
		}
		const auto top = std::uint32_t(mesh.positions.size());
		mesh.positions.push_back({0.0f, 0.0f, height});
		mesh.normals.push_back({0.0f, 0.0f, 1.0f});
		for (int i = 0; i < segments; ++i)
		{
			mesh.positions.push_back(mesh.positions[std::size_t(2 * i + 1)]);
			mesh.normals.push_back({0.0f, 0.0f, 1.0f});
		}
		for (std::uint32_t i = 0; i < segments; ++i)
		{
			const std::uint32_t next = (i + 1) % segments;
			const std::uint32_t side[6] = {2 * i, 2 * next, 2 * next + 1, 2 * i, 2 * next + 1, 2 * i + 1};
			mesh.indices.insert(mesh.indices.end(), std::begin(side), std::end(side));
			const std::uint32_t cap[3] = {top, top + 1 + i, top + 1 + next};
			mesh.indices.insert(mesh.indices.end(), std::begin(cap), std::end(cap));
		}
		mesh.uvs.assign(mesh.positions.size(), math::Vec2f{0.0f});
		mesh.parts = {{0, 0, std::uint32_t(mesh.indices.size())}};
		mesh.materials = {mesh::Material{}};
		return std::make_shared<const mesh::Mesh>(std::move(mesh));
	}

	// A rectangle of the board frame's plane at depth `z`, from (x0, y0) to (x1, y1), facing along
	// `facing` (-1 toward a camera in front of the board, +1 behind it). With `textured`, the raster maps
	// onto it upright: its top row at y0, since the board frame's Y runs down and OBJ's v up.
	static std::shared_ptr<const mesh::Mesh> sheet(double x0, double y0, double x1, double y1, double z, float facing, bool textured)
	{
		mesh::Mesh mesh;
		const std::pair<double, double> corners[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
		const math::Vec2f uvs[4] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
		for (int i = 0; i < 4; ++i)
		{
			mesh.positions.push_back(math::Vec3f{float(corners[i].first), float(corners[i].second), float(z)});
			mesh.normals.push_back(math::Vec3f{0.0f, 0.0f, facing});
			mesh.uvs.push_back(textured ? uvs[i] : math::Vec2f{0.0f});
		}
		mesh.indices = {0, 1, 2, 0, 2, 3};
		mesh.parts = {{0, 0, 6}};
		mesh.materials = {mesh::Material{}};
		return std::make_shared<const mesh::Mesh>(std::move(mesh));
	}

	// `board` drawn, or why not.
	static std::optional<BoardSheet> drawBoard(const BoardMover& board, std::vector<std::string>& problems)
	{
		const std::string owner = string::format("board '{}'", board.name);
		const std::optional<camera::board::Specification> specification = specificationOf(board);
		if (!specification || board.pixelsPerSquare == 0)
		{
			problems.push_back(string::format("{}: its pattern and instance make no board to draw", owner));
			return std::nullopt;
		}
		const double square = specification->instance().squareLength.value.metres();
		const double perMetre = board.pixelsPerSquare / square;
		// The margin to the nearest pixel, since the raster is whole pixels; the corners do not depend on it.
		const auto margin = std::uint32_t(std::lround(std::max(board.margin, 0.0) * perMetre));
		camera::board::RenderResult rendered = camera::board::render(specification->pattern(), {board.pixelsPerSquare, margin});
		if (!rendered.rendering)
		{
			for (const camera::board::RenderDiagnostic& d : rendered.diagnostics)
				problems.push_back(string::format("{}: {}", owner, d.detail));
			return std::nullopt;
		}
		image::Image raster = std::move(rendered.rendering->raster);
		if (raster.colorSpace() == image::ColorSpace::Unspecified)
			raster.setColorSpace(image::ColorSpace::sRGB); // codes as printed
		raster = render::storedTexture(raster);
		const double x0 = -double(margin) / perMetre, x1 = (raster.width() - double(margin)) / perMetre;
		const double y0 = -double(margin) / perMetre, y1 = (raster.height() - double(margin)) / perMetre;
		return BoardSheet{std::make_shared<const image::Image>(std::move(raster)), sheet(x0, y0, x1, y1, 0.0, -1.0f, true),
						  sheet(x0, y0, x1, y1, board.backing, 1.0f, false), std::string{}}; // its job says what it was drawn from
	}

	// What a picture is keyed by among the jobs, and so among the failures.
	static std::string pictureKey(const core::Uri& uri)
	{
		return string::format("picture {}", uri);
	}

	static std::string bakeKey(const BakeKey& key)
	{
		return string::format("bake {} {:.17g} {:.17g} {}x{}", key.seed, key.cell, key.texelsPerMetre, key.width, key.height);
	}

	// What a prop's mesh is read from: its file, and whether and at what rate it is a clip.
	static std::string propKey(const Prop& prop)
	{
		return string::format("prop {} {} {} {}/{}", prop.id.toString(), prop.mesh, prop.clip ? "clip" : "mesh", prop.clipRate.numerator,
							  prop.clipRate.denominator);
	}

	// What a board's drawing depends on: its pattern, its square's printed size, its margin, its pixels a
	// square and its backing. Not its identity's text, nor its path, which only place it.
	static std::string boardKey(const BoardMover& board)
	{
		const camera::board::PatternParameters& p = board.pattern;
		return string::format("board {} {} {}x{} {:.17g} {} {} {} {:.17g} {} {:.17g}", board.id.toString(), int(p.dictionary), p.squaresX,
							  p.squaresY, p.markerToSquare, p.firstMarkerId, int(p.layout), board.instance.squareLength.value.nanometres(),
							  board.margin, board.pixelsPerSquare, board.backing);
	}

	// A picture read, or why not, as a job's outcome.
	static AssetOutcome readPicture(const core::Uri& uri, const std::string& owner)
	{
		AssetOutcome outcome{pictureKey(uri), {}, {}};
		std::optional<image::Image> loaded = io::image::load(uri);
		if (!loaded)
		{
			outcome.problems.push_back(string::format("{}: the picture {} did not load", owner, uri));
			return outcome;
		}
		if (loaded->colorSpace() == image::ColorSpace::Unspecified)
		{
			log::warn("rigplan: the picture {} states no colour space, so it is taken as sRGB", uri);
			loaded->setColorSpace(image::ColorSpace::sRGB);
		}
		auto picture = std::make_shared<const image::Image>(render::storedTexture(*loaded));
		outcome.keep = [uri, picture](SceneAssets& assets)
		{ assets.pictures[uri] = picture; };
		return outcome;
	}

	// What `plan` needs from its assets, whether or not they hold it yet, in loadSceneAssets' order. One
	// walk serves the jobs and the pruning, so the two cannot disagree about what the plan needs.
	struct Needed
	{
		std::vector<std::pair<core::Uri, std::string>> pictures; // with the first owner to name each
		std::vector<std::pair<BakeKey, std::string>> bakes;		 // with its surface's owner
		std::vector<const Prop*> props;
		std::vector<const BoardMover*> boards;
	};

	static Needed neededBy(const RigPlan& plan, const SceneAssets& assets)
	{
		Needed needed;
		std::set<core::Uri> pictures;
		std::set<BakeKey> bakes;
		const auto picture = [&](const core::Uri& uri, const std::string& owner)
		{
			if (pictures.insert(uri).second)
				needed.pictures.emplace_back(uri, owner);
		};
		for (const Surface& surface : surfacesOf(plan))
		{
			if (const auto* texture = std::get_if<ImageTexture>(&surface.material->surface))
				picture(texture->image, surface.owner);
			else if (const auto* procedural = std::get_if<image::ProceduralTexture>(&surface.material->surface))
			{
				const BakeKey key = bakeOf(*procedural, surface);
				if (bakes.insert(key).second)
					needed.bakes.emplace_back(key, surface.owner);
			}
		}
		for (const Prop& prop : plan.props)
		{
			needed.props.push_back(&prop);
			// The pictures a prop's materials name are known once its mesh is read.
			const auto loaded = assets.props.find(prop.id);
			if (loaded == assets.props.end() || !loaded->second.clip)
				continue;
			const std::string owner = string::format("prop '{}'", prop.name);
			for (const mesh::Mesh& frame : loaded->second.clip->frames)
			{
				for (const mesh::Material& material : frame.materials)
				{
					if (material.diffuseTexture)
						picture(*material.diffuseTexture, owner);
				}
			}
		}
		for (const BoardMover& board : plan.boards)
			needed.boards.push_back(&board);
		return needed;
	}

	void sceneGeometry(const RigPlan& plan, SceneAssets& assets)
	{
		assets.surfaces.clear();
		for (const Surface& surface : surfacesOf(plan))
			assets.surfaces.push_back(rectangle(surface, uvOf(surface)));
		assets.poles.clear();
		for (const Mount& mount : plan.mounts)
		{
			if (const auto* pole = std::get_if<Pole>(&mount.kind))
				assets.poles.emplace(mount.id, cylinder(*pole));
		}
	}

	std::vector<AssetJob> missingAssets(const RigPlan& plan, const SceneAssets& assets)
	{
		const Needed needed = neededBy(plan, assets);
		std::vector<AssetJob> jobs;
		const auto wanted = [&](const std::string& key)
		{ return assets.failed.count(key) == 0; };

		for (const auto& [uri, owner] : needed.pictures)
		{
			if (assets.pictures.count(uri) != 0 || !wanted(pictureKey(uri)))
				continue;
			jobs.push_back({pictureKey(uri), string::format("reading {}", uri), [uri = uri, owner = owner]()
							{ return readPicture(uri, owner); }});
		}
		// The props' meshes come before the bakes in the jobs' order only where they did before: the old
		// loader read a surface's picture, then baked it, surface by surface, then read the props.
		for (const auto& [key, owner] : needed.bakes)
		{
			if (assets.baked.count(key) != 0 || !wanted(bakeKey(key)))
				continue;
			jobs.push_back({bakeKey(key), string::format("baking {}", owner), [key = key]()
							{
								const image::ProceduralTexture mixed{key.seed, key.cell};
								auto baked = std::make_shared<const image::Image>(
									render::storedTexture(image::bakeProceduralTexture(mixed, key.width, key.height, key.texelsPerMetre)));
								return AssetOutcome{bakeKey(key), [key, baked](SceneAssets& kept)
													{ kept.baked[key] = baked; },
													{}};
							}});
		}
		for (const Prop* prop : needed.props)
		{
			const std::string key = propKey(*prop);
			const auto loaded = assets.props.find(prop->id);
			if ((loaded != assets.props.end() && loaded->second.madeFrom == key) || !wanted(key))
				continue;
			jobs.push_back({key, string::format("reading {}", prop->mesh),
							[key, id = prop->id, name = prop->name, uri = prop->mesh, clip = prop->clip, rate = prop->clipRate]()
							{
								AssetOutcome outcome{key, {}, {}};
								std::optional<mesh::MeshClip> read;
								if (clip)
									read = io::mesh::loadClip(uri, rate);
								else if (std::optional<mesh::Mesh> one = io::mesh::load(uri))
									read = mesh::MeshClip{{std::move(*one)}, media::FrameRate{}};
								if (!read)
								{
									outcome.problems.push_back(string::format("prop '{}': its mesh {} did not load", name, uri));
									return outcome;
								}
								auto shared = std::make_shared<const mesh::MeshClip>(std::move(*read));
								outcome.keep = [id, key, shared](SceneAssets& kept)
								{ kept.props[id] = PropMesh{shared, key}; };
								return outcome;
							}});
		}
		for (const BoardMover* board : needed.boards)
		{
			const std::string key = boardKey(*board);
			const auto drawn = assets.boards.find(board->id);
			if ((drawn != assets.boards.end() && drawn->second.madeFrom == key) || !wanted(key))
				continue;
			jobs.push_back({key, string::format("drawing board '{}'", board->name), [key, board = *board]()
							{
								AssetOutcome outcome{key, {}, {}};
								std::optional<BoardSheet> sheet = drawBoard(board, outcome.problems);
								if (sheet)
								{
									sheet->madeFrom = key;
									outcome.keep = [id = board.id, sheet = std::move(*sheet)](SceneAssets& kept)
									{ kept.boards[id] = sheet; };
								}
								return outcome;
							}});
		}
		return jobs;
	}

	void addAsset(SceneAssets& assets, AssetOutcome outcome)
	{
		if (outcome.keep)
		{
			outcome.keep(assets);
			assets.failed.erase(outcome.key);
		}
		else
			assets.failed[outcome.key] = std::move(outcome.problems);
	}

	void pruneAssets(const RigPlan& plan, SceneAssets& assets)
	{
		const Needed needed = neededBy(plan, assets);
		std::set<std::string> keys;
		std::set<core::Uri> pictures;
		for (const auto& [uri, owner] : needed.pictures)
		{
			keys.insert(pictureKey(uri));
			pictures.insert(uri);
		}
		std::set<BakeKey> bakes;
		for (const auto& [key, owner] : needed.bakes)
		{
			keys.insert(bakeKey(key));
			bakes.insert(key);
		}
		std::set<core::Uuid> owners;
		for (const Prop* prop : needed.props)
		{
			keys.insert(propKey(*prop));
			owners.insert(prop->id);
		}
		for (const BoardMover* board : needed.boards)
		{
			keys.insert(boardKey(*board));
			owners.insert(board->id);
		}

		const auto prune = [](auto& map, const auto& keep)
		{
			for (auto it = map.begin(); it != map.end();)
				it = keep.count(it->first) == 0 ? map.erase(it) : std::next(it);
		};
		prune(assets.pictures, pictures);
		prune(assets.baked, bakes);
		prune(assets.props, owners);
		prune(assets.boards, owners);
		prune(assets.failed, keys);
	}

	AssetsResult loadSceneAssets(const RigPlan& plan)
	{
		AssetsResult result;
		sceneGeometry(plan, result.assets);
		// Each round either makes or fails every job it lists, and only a prop read in it can name a picture
		// the next round needs, so this ends.
		for (std::vector<AssetJob> jobs = missingAssets(plan, result.assets); !jobs.empty(); jobs = missingAssets(plan, result.assets))
		{
			for (AssetJob& job : jobs)
			{
				AssetOutcome outcome = job.run();
				result.problems.insert(result.problems.end(), outcome.problems.begin(), outcome.problems.end());
				addAsset(result.assets, std::move(outcome));
			}
		}
		return result;
	}

	// Where each mount and light stands, for one scene: a realisation's, or the nominal layout's, where
	// a mount or light without one is not drawn.
	struct Poses
	{
		std::function<std::optional<math::RigidTransformd>(std::size_t)> mount;
		std::function<std::optional<math::RigidTransformd>(std::size_t)> light;
	};

	// A surface waiting on its asset: a matte mid grey, never the white a missing texture leaves.
	static constexpr image::ColorRGBf kAwaited{0.5f, 0.5f, 0.5f};

	static SceneResult buildScene(const RigPlan& plan, const Poses& poses, const SceneAssets& assets, std::size_t frame)
	{
		SceneResult result;
		render::Scene& scene = result.scene;

		// A picture several surfaces show is one texture of the scene.
		std::map<const image::Image*, std::size_t> textureIndex;
		auto texture = [&](const std::shared_ptr<const image::Image>& image)
		{
			const auto [at, added] = textureIndex.emplace(image.get(), scene.textures.size());
			if (added)
				scene.textures.push_back(image);
			return at->second;
		};
		auto picture = [&](const core::Uri& uri, const std::string& owner) -> std::optional<std::size_t>
		{
			const auto found = assets.pictures.find(uri);
			if (found == assets.pictures.end())
			{
				result.problems.push_back(string::format("{}: the assets hold no picture {}", owner, uri));
				return std::nullopt;
			}
			return texture(found->second);
		};
		auto place = [&](std::shared_ptr<const mesh::Mesh> mesh, const math::Mat4d& planFromMesh, std::vector<std::size_t> materials,
						 SceneOwner owner)
		{
			scene.instances.push_back({scene.meshes.size(), planFromMesh, std::move(materials)});
			scene.meshes.push_back(std::move(mesh));
			result.owners.push_back(owner);
		};

		const std::vector<Surface> surfaces = surfacesOf(plan);
		for (std::size_t i = 0; i < surfaces.size(); ++i)
		{
			const Surface& surface = surfaces[i];
			if (i >= assets.surfaces.size())
			{
				result.problems.push_back(string::format("{}: the assets hold no mesh for it", surface.owner));
				continue;
			}
			render::Material material;
			material.specular = surface.material->specular;
			material.shininess = float(surface.material->shininess);
			if (const auto* colour = std::get_if<SolidColour>(&surface.material->surface))
				material.diffuse = colour->colour;
			else if (const auto* image = std::get_if<ImageTexture>(&surface.material->surface))
			{
				material.texture = picture(image->image, surface.owner);
				material.diffuse = material.texture ? image::ColorRGBf{1.0f, 1.0f, 1.0f} : kAwaited;
			}
			else
			{
				const BakeKey key = bakeOf(std::get<image::ProceduralTexture>(surface.material->surface), surface);
				const auto found = assets.baked.find(key);
				if (found == assets.baked.end())
					result.problems.push_back(string::format("{}: the assets hold no bake of its procedural texture", surface.owner));
				else
					material.texture = texture(found->second);
				material.diffuse = material.texture ? image::ColorRGBf{1.0f, 1.0f, 1.0f} : kAwaited;
			}
			scene.materials.push_back(material);
			place(assets.surfaces[i], math::Mat4d{1.0}, {scene.materials.size() - 1}, surface.object);
		}

		for (const Prop& prop : plan.props)
		{
			const std::string owner = string::format("prop '{}'", prop.name);
			const auto found = assets.props.find(prop.id);
			if (found == assets.props.end())
			{
				result.problems.push_back(string::format("{}: the assets hold no mesh for it", owner));
				continue;
			}
			const std::shared_ptr<const mesh::MeshClip>& clip = found->second.clip;
			const std::size_t shown = mesh::clipFrame(*clip, frame, plan.timeline.rate);
			std::shared_ptr<const mesh::Mesh> mesh(clip, &clip->frames[shown]); // shares the clip's lifetime, copies nothing
			std::vector<std::size_t> materials;
			for (const mesh::Material& own : mesh->materials)
			{
				render::Material material;
				material.diffuse = own.diffuse; // MTL multiplies Kd by map_Kd, as the renderer does
				if (own.diffuseTexture)
				{
					material.texture = picture(*own.diffuseTexture, owner);
					if (!material.texture)
						material.diffuse = kAwaited;
				}
				material.specular = own.specular;
				material.shininess = own.shininess;
				materials.push_back(scene.materials.size());
				scene.materials.push_back(material);
			}
			place(std::move(mesh), planFromProp(prop, frame), std::move(materials), {SceneObject::Prop, prop.id});
		}

		for (const BoardMover& board : plan.boards)
		{
			const auto found = assets.boards.find(board.id);
			if (found == assets.boards.end())
			{
				result.problems.push_back(string::format("board '{}': the assets hold no drawing of it", board.name));
				continue;
			}
			const BoardSheet& drawn = found->second;
			const math::Mat4d planFromSheet = planFromBoard(board, frame).matrix();
			render::Material print;
			print.diffuse = {1.0f, 1.0f, 1.0f};
			print.texture = texture(drawn.raster);
			scene.materials.push_back(print);
			place(drawn.print, planFromSheet, {scene.materials.size() - 1}, {SceneObject::Board, board.id});
			render::Material back;
			back.diffuse = {1.0f, 1.0f, 1.0f};
			scene.materials.push_back(back);
			place(drawn.back, planFromSheet, {scene.materials.size() - 1}, {SceneObject::Board, board.id});
		}

		// Each pole, floor to top at its mount (CONTEXT.md, "Mount"): real geometry, occluding what is
		// behind it. A pole states no material yet, so it is a matte mid grey.
		for (std::size_t i = 0; i < plan.mounts.size(); ++i)
		{
			if (!std::holds_alternative<Pole>(plan.mounts[i].kind))
				continue;
			const std::optional<math::RigidTransformd> planFromMount = poses.mount(i);
			if (!planFromMount)
				continue;
			const auto found = assets.poles.find(plan.mounts[i].id);
			if (found == assets.poles.end())
			{
				result.problems.push_back(string::format("pole '{}': the assets hold no mesh for it", plan.mounts[i].name));
				continue;
			}
			render::Material grey;
			grey.diffuse = {0.5f, 0.5f, 0.5f};
			scene.materials.push_back(grey);
			place(found->second, planFromMount->matrix(), {scene.materials.size() - 1}, {SceneObject::Pole, plan.mounts[i].id});
		}

		for (std::size_t i = 0; i < plan.lights.size(); ++i)
		{
			const std::optional<math::RigidTransformd> planFromLight = poses.light(i);
			if (!planFromLight)
				continue;
			const LightSpecification* specification = findLight(plan.specifications, plan.lights[i].light);
			if (!specification)
			{
				result.problems.push_back(string::format("light '{}': no specification '{}'", plan.lights[i].name, plan.lights[i].light));
				continue;
			}
			render::Light light;
			light.kind = specification->kind == LightKind::Spot ? render::LightKind::Spot : render::LightKind::Point;
			light.position = planFromLight->translation();
			light.direction = planFromLight->rotate({0.0, 0.0, 1.0}); // camera-frame axes: it shines along +Z
			light.colour = specification->colour;
			light.intensity = specification->intensity;
			light.innerConeDegrees = specification->innerConeDegrees;
			light.outerConeDegrees = specification->outerConeDegrees;
			scene.lights.push_back(light);
		}
		return result;
	}

	SceneResult renderScene(const RigPlan& plan, const Realisation& realisation, const SceneAssets& assets, std::size_t frame)
	{
		const bool mountsMatch = realisation.mounts.size() == plan.mounts.size();
		const bool lightsMatch = realisation.lights.size() == plan.lights.size();
		Poses poses;
		poses.mount = [&](std::size_t i) -> std::optional<math::RigidTransformd>
		{
			if (!mountsMatch)
				return std::nullopt;
			return realisation.mounts[i].planFromMount;
		};
		poses.light = [&](std::size_t i) -> std::optional<math::RigidTransformd>
		{
			if (!lightsMatch)
				return std::nullopt;
			return realisation.lights[i].planFromLight;
		};
		SceneResult result = buildScene(plan, poses, assets, frame);
		if (!mountsMatch)
			result.problems.push_back("the realisation is not of this rig plan: its mounts differ");
		if (!lightsMatch)
			result.problems.push_back("the realisation is not of this rig plan: its lights differ");
		return result;
	}

	SceneResult renderScene(const RigPlan& plan, const NominalLayout& layout, const SceneAssets& assets, std::size_t frame)
	{
		Poses poses;
		poses.mount = [&](std::size_t i) -> std::optional<math::RigidTransformd>
		{
			if (i >= layout.mounts.size() || layout.mounts[i].id != plan.mounts[i].id)
				return std::nullopt;
			return layout.mounts[i].planFromMount;
		};
		poses.light = [&](std::size_t i) -> std::optional<math::RigidTransformd>
		{
			if (i >= layout.lights.size() || layout.lights[i].id != plan.lights[i].id)
				return std::nullopt;
			return layout.lights[i].planFromLight;
		};
		return buildScene(plan, poses, assets, frame);
	}
} // namespace lain::rigplan
