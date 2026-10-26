#include "lain/rigplan/scene.h"

#include <lain/camera/board/rendering.h>
#include <lain/image/procedural.h>
#include <lain/io/image/load.h>
#include <lain/io/mesh/load.h>
#include <lain/log/log.h>
#include <lain/string/format.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include <variant>

namespace lain::rigplan
{
	// One flat rectangle of the plan: centred on `centre`, seen from the side `facing` points to,
	// `width` metres across and `height` along `up`. Across is up x facing, so it reads left to right
	// from that side.
	struct Surface
	{
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
					surfaces.push_back({string::format("the room's {}", name), string::format("room/{}", name), centre, facing, up, w, h, &s.material});
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
			surfaces.push_back({string::format("plane '{}'", plane.name), plane.id.toString(), plane.centre, pose.rotate({0.0, 0.0, -1.0}),
								pose.rotate({0.0, -1.0, 0.0}), plane.size.x, plane.size.y, &plane.material});
		}

		for (const Box& box : plan.boxes)
		{
			const math::Quatd yaw = math::angleAxis(math::radians(box.yawDegrees), Z);
			const math::Vec3d h = box.size / 2.0;
			const math::Vec3d c = box.centre;
			auto add = [&](const char* face, math::Vec3d centre, math::Vec3d facing, math::Vec3d up, double w, double height)
			{
				surfaces.push_back({string::format("box '{}'", box.name), string::format("{}/{}", box.id.toString(), face), centre, facing, up, w,
									height, &box.material});
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
		const double x0 = -double(margin) / perMetre, x1 = (raster.width() - double(margin)) / perMetre;
		const double y0 = -double(margin) / perMetre, y1 = (raster.height() - double(margin)) / perMetre;
		return BoardSheet{std::make_shared<const image::Image>(std::move(raster)), sheet(x0, y0, x1, y1, 0.0, -1.0f, true),
						  sheet(x0, y0, x1, y1, board.backing, 1.0f, false)};
	}

	AssetsResult loadSceneAssets(const RigPlan& plan)
	{
		AssetsResult result;
		SceneAssets& assets = result.assets;
		auto picture = [&](const core::Uri& uri, const std::string& owner)
		{
			if (assets.pictures.count(uri) != 0)
				return;
			std::optional<image::Image> loaded = io::image::load(uri);
			if (!loaded)
			{
				result.problems.push_back(string::format("{}: the picture {} did not load", owner, uri));
				return;
			}
			if (loaded->colorSpace() == image::ColorSpace::Unspecified)
			{
				log::warn("rigplan: the picture {} states no colour space, so it is taken as sRGB", uri);
				loaded->setColorSpace(image::ColorSpace::sRGB);
			}
			assets.pictures.emplace(uri, std::make_shared<const image::Image>(std::move(*loaded)));
		};

		for (const Surface& surface : surfacesOf(plan))
		{
			assets.surfaces.push_back(rectangle(surface, uvOf(surface)));
			if (const auto* texture = std::get_if<ImageTexture>(&surface.material->surface))
				picture(texture->image, surface.owner);
			else if (const auto* procedural = std::get_if<image::ProceduralTexture>(&surface.material->surface))
			{
				const BakeKey key = bakeOf(*procedural, surface);
				if (assets.baked.count(key) == 0)
				{
					const image::ProceduralTexture mixed{key.seed, key.cell};
					assets.baked.emplace(key, std::make_shared<const image::Image>(
												  image::bakeProceduralTexture(mixed, key.width, key.height, key.texelsPerMetre)));
				}
			}
		}

		for (const Prop& prop : plan.props)
		{
			std::optional<mesh::MeshClip> clip;
			if (prop.clip)
				clip = io::mesh::loadClip(prop.mesh, prop.clipRate);
			else if (std::optional<mesh::Mesh> one = io::mesh::load(prop.mesh))
				clip = mesh::MeshClip{{std::move(*one)}, media::FrameRate{}};
			const std::string owner = string::format("prop '{}'", prop.name);
			if (!clip)
			{
				result.problems.push_back(string::format("{}: its mesh {} did not load", owner, prop.mesh));
				continue;
			}
			for (const mesh::Mesh& frame : clip->frames)
			{
				for (const mesh::Material& material : frame.materials)
				{
					if (material.diffuseTexture)
						picture(*material.diffuseTexture, owner);
				}
			}
			assets.props.emplace(prop.id, std::make_shared<const mesh::MeshClip>(std::move(*clip)));
		}

		for (const BoardMover& board : plan.boards)
		{
			if (std::optional<BoardSheet> drawn = drawBoard(board, result.problems))
				assets.boards.emplace(board.id, std::move(*drawn));
		}

		for (const Mount& mount : plan.mounts)
		{
			if (const auto* pole = std::get_if<Pole>(&mount.kind))
				assets.poles.emplace(mount.id, cylinder(*pole));
		}
		return result;
	}

	SceneResult renderScene(const RigPlan& plan, const Realisation& realisation, const SceneAssets& assets, std::size_t frame)
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
		auto place = [&](std::shared_ptr<const mesh::Mesh> mesh, const math::Mat4d& planFromMesh, std::vector<std::size_t> materials)
		{
			scene.instances.push_back({scene.meshes.size(), planFromMesh, std::move(materials)});
			scene.meshes.push_back(std::move(mesh));
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
				material.diffuse = {1.0f, 1.0f, 1.0f};
				material.texture = picture(image->image, surface.owner);
			}
			else
			{
				const BakeKey key = bakeOf(std::get<image::ProceduralTexture>(surface.material->surface), surface);
				material.diffuse = {1.0f, 1.0f, 1.0f};
				const auto found = assets.baked.find(key);
				if (found == assets.baked.end())
					result.problems.push_back(string::format("{}: the assets hold no bake of its procedural texture", surface.owner));
				else
					material.texture = texture(found->second);
			}
			scene.materials.push_back(material);
			place(assets.surfaces[i], math::Mat4d{1.0}, {scene.materials.size() - 1});
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
			const std::shared_ptr<const mesh::MeshClip>& clip = found->second;
			const std::size_t shown = mesh::clipFrame(*clip, frame, plan.timeline.rate);
			std::shared_ptr<const mesh::Mesh> mesh(clip, &clip->frames[shown]); // shares the clip's lifetime, copies nothing
			std::vector<std::size_t> materials;
			for (const mesh::Material& own : mesh->materials)
			{
				render::Material material;
				material.diffuse = own.diffuse; // MTL multiplies Kd by map_Kd, as the renderer does
				if (own.diffuseTexture)
					material.texture = picture(*own.diffuseTexture, owner);
				material.specular = own.specular;
				material.shininess = own.shininess;
				materials.push_back(scene.materials.size());
				scene.materials.push_back(material);
			}
			place(std::move(mesh), planFromProp(prop, frame), std::move(materials));
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
			place(drawn.print, planFromSheet, {scene.materials.size() - 1});
			render::Material back;
			back.diffuse = {1.0f, 1.0f, 1.0f};
			scene.materials.push_back(back);
			place(drawn.back, planFromSheet, {scene.materials.size() - 1});
		}

		// Each pole, floor to top at its realised mount (CONTEXT.md, "Mount"): real geometry, occluding what
		// is behind it. A pole states no material yet, so it is a matte mid grey.
		if (realisation.mounts.size() != plan.mounts.size())
			result.problems.push_back("the realisation is not of this rig plan: its mounts differ");
		else
		{
			for (std::size_t i = 0; i < plan.mounts.size(); ++i)
			{
				if (!std::holds_alternative<Pole>(plan.mounts[i].kind))
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
				place(found->second, realisation.mounts[i].planFromMount.matrix(), {scene.materials.size() - 1});
			}
		}

		if (realisation.lights.size() != plan.lights.size())
			result.problems.push_back("the realisation is not of this rig plan: its lights differ");
		else
		{
			for (std::size_t i = 0; i < plan.lights.size(); ++i)
			{
				const LightSpecification* specification = findLight(plan.specifications, plan.lights[i].light);
				if (!specification)
				{
					result.problems.push_back(string::format("light '{}': no specification '{}'", plan.lights[i].name, plan.lights[i].light));
					continue;
				}
				const math::RigidTransformd& planFromLight = realisation.lights[i].planFromLight;
				render::Light light;
				light.kind = specification->kind == LightKind::Spot ? render::LightKind::Spot : render::LightKind::Point;
				light.position = planFromLight.translation();
				light.direction = planFromLight.rotate({0.0, 0.0, 1.0}); // camera-frame axes: it shines along +Z
				light.colour = specification->colour;
				light.intensity = specification->intensity;
				light.innerConeDegrees = specification->innerConeDegrees;
				light.outerConeDegrees = specification->outerConeDegrees;
				scene.lights.push_back(light);
			}
		}
		return result;
	}
} // namespace lain::rigplan
