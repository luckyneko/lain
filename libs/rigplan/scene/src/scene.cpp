#include "lain/rigplan/scene.h"

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

		for (const Surface& surface : surfacesOf(plan))
		{
			render::Material material;
			material.specular = surface.material->specular;
			material.shininess = float(surface.material->shininess);
			std::function<math::Vec2f(double, double)> uvAt = [](double, double)
			{ return math::Vec2f{0.0f}; };
			if (const auto* colour = std::get_if<SolidColour>(&surface.material->surface))
				material.diffuse = colour->colour;
			else if (const auto* image = std::get_if<ImageTexture>(&surface.material->surface))
			{
				// One copy of the picture every tile, upright from the side the surface faces.
				material.diffuse = {1.0f, 1.0f, 1.0f};
				material.texture = picture(image->image, surface.owner);
				const math::Vec2d tile = image->tile;
				uvAt = [tile](double s, double t)
				{ return math::Vec2f{float(s / tile.x), float(t / tile.y)}; };
			}
			else
			{
				// The surface's own bake, covering it from its corner: texel (i, j) is proceduralGrey at
				// ((i + 0.5) / d, (j + 0.5) / d) metres along across and up. The renderer reads OBJ's v
				// upward, so v runs down from 1 to put row j at t = j / d.
				const BakeKey key = bakeOf(std::get<image::ProceduralTexture>(surface.material->surface), surface);
				material.diffuse = {1.0f, 1.0f, 1.0f};
				const auto found = assets.baked.find(key);
				if (found == assets.baked.end())
					result.problems.push_back(string::format("{}: the assets hold no bake of its procedural texture", surface.owner));
				else
					material.texture = texture(found->second);
				uvAt = [key](double s, double t)
				{
					return math::Vec2f{float(s * key.texelsPerMetre / key.width), float(1 - t * key.texelsPerMetre / key.height)};
				};
			}
			scene.materials.push_back(material);
			place(rectangle(surface, uvAt), math::Mat4d{1.0}, {scene.materials.size() - 1});
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
			place(std::move(mesh), planFromProp(prop), std::move(materials));
		}

		// Each pole, floor to top at its realised mount (CONTEXT.md, "Mount"): real geometry, occluding what
		// is behind it. A pole states no material yet, so it is a matte mid grey.
		if (realisation.mounts.size() != plan.mounts.size())
			result.problems.push_back("the realisation is not of this rig plan: its mounts differ");
		else
		{
			for (std::size_t i = 0; i < plan.mounts.size(); ++i)
			{
				const auto* pole = std::get_if<Pole>(&plan.mounts[i].kind);
				if (!pole)
					continue;
				render::Material grey;
				grey.diffuse = {0.5f, 0.5f, 0.5f};
				scene.materials.push_back(grey);
				place(cylinder(*pole), realisation.mounts[i].planFromMount.matrix(), {scene.materials.size() - 1});
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
