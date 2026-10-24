#pragma once

#include <lain/core/uri.h>
#include <lain/core/uuid.h>
#include <lain/image/image.h>
#include <lain/mesh/mesh.h>
#include <lain/render/scene.h>
#include <lain/rigplan/realisation.h>
#include <lain/rigplan/rigplan.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace lain::rigplan
{
	// A rig plan as lain::render draws it (CONTEXT.md, "Render scene"; ADR-0028): its room, planes,
	// boxes, props and poles, in the plan frame, lit by its lights. Camera and light housings are not in
	// it yet; check() keeps a camera clear of its own pole (PlanProblem::InsidePole).

	// How finely a procedural texture is baked: this many texels to its finest noise cell, which is two
	// across the narrowest soft edge of its discs (a tenth of a cell wide). Capped so that no baked
	// side is longer than kMaxBakedTexels, which every device lain renders on can hold.
	inline constexpr double kBakedTexelsPerCell = 20;
	inline constexpr int kMaxBakedTexels = 8192;

	// A procedural surface's baked picture, keyed by everything it depends on: the texture's seed
	// (already mixed with the surface's identity) and cell, the texel density, and the texel size.
	struct BakeKey
	{
		std::uint64_t seed = 0;
		double cell = 0;
		double texelsPerMetre = 0;
		int width = 0;
		int height = 0;

		bool operator<(const BakeKey& rhs) const
		{
			return std::tie(seed, cell, texelsPerMetre, width, height) < std::tie(rhs.seed, rhs.cell, rhs.texelsPerMetre, rhs.width, rhs.height);
		}
	};

	// What a rig plan draws that is not in the plan itself, read and baked once so a render scene at
	// any frame shares it: each prop's mesh (a clip of one) or mesh clip, each picture a material or a
	// mesh's material names, and each procedural surface baked to its extent.
	struct SceneAssets
	{
		std::map<core::Uuid, std::shared_ptr<const mesh::MeshClip>> props;
		std::map<core::Uri, std::shared_ptr<const image::Image>> pictures;
		std::map<BakeKey, std::shared_ptr<const image::Image>> baked;
	};

	struct AssetsResult
	{
		SceneAssets assets;
		std::vector<std::string> problems; // what did not load, each naming its owner and file
	};

	// Every asset `plan` draws, its paths absolute (a document's are resolved when it is read). A prop
	// whose mesh or clip will not load, or a picture that will not, is a problem naming it, and the
	// rest still loads. A picture that states no colour space is taken as sRGB, as the web takes one,
	// with a warning in the log, since the renderer refuses an untagged texture.
	AssetsResult loadSceneAssets(const RigPlan& plan);

	struct SceneResult
	{
		render::Scene scene;
		std::vector<std::string> problems; // an asset `assets` lacks; the scene is incomplete when any
	};

	// `realisation` of `plan` at capture frame `frame`, as a render scene in the plan frame:
	//   * the room's present surfaces, each facing in, and the planes and boxes, each a rectangle per
	//     face whose picture reads upright from the side it faces (a floor's with north at its top);
	//   * each prop's mesh, or the frame of its clip the capture frame shows (mesh::clipFrame), placed
	//     by planFromProp, its file's own materials mapped one for one (MTL's Kd, map_Kd, Ks, Ns);
	//   * each pole, a cylinder from the floor to its top at its realised mount, a matte mid grey;
	//   * each light, its specification's kind, colour, intensity and cone at its realised pose.
	// A picture repeats every tile, in metres; a procedural texture is baked once per surface at the
	// surface's own extent, so it reads proceduralGrey at each point of it, in metres from the
	// surface's corner, with its seed mixed with the surface's identity so that no two surfaces of one
	// material repeat each other (a targetless method would match them as one). A rig plan states no
	// ambient light: what lights it is its lights.
	SceneResult renderScene(const RigPlan& plan, const Realisation& realisation, const SceneAssets& assets, std::size_t frame);
} // namespace lain::rigplan
