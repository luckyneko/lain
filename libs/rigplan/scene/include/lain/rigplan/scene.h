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
#include <functional>
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
	// side is longer than kMaxBakedTexels, which every device lain renders on can hold. Measured enough
	// (M15 slice 4d, test-camera-capture): doubling it moves a capture by under half a grey level and
	// changes how many of its SIFT features the exact texture also gives by at most 4%.
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

	// A board mover's printed sheet: the board renderer's raster, and the print and the plain back as
	// meshes in the board frame (camera::board::Specification), so a frame places them by planFromBoard.
	// The print spans the raster from its first texel's edge to its last's, so a texel centre sits where
	// the board frame puts it; the back is `backing` behind it.
	struct BoardSheet
	{
		std::shared_ptr<const image::Image> raster; // its codes as printed, stored as the device stores them
		std::shared_ptr<const mesh::Mesh> print;
		std::shared_ptr<const mesh::Mesh> back;
		std::string madeFrom; // what it was drawn from, so a board whose drawing changed is drawn again
	};

	// A prop's mesh or mesh clip as read, and what it was read from, so a prop whose file changed is
	// read again.
	struct PropMesh
	{
		std::shared_ptr<const mesh::MeshClip> clip;
		std::string madeFrom;
	};

	// What a rig plan draws, read, baked and built once so a render scene at any frame shares it: each
	// prop's mesh (a clip of one) or mesh clip, each picture a material or a mesh's material names, each
	// procedural surface baked to its extent, each board drawn, and the meshes the plan's own geometry
	// makes. Every frame's scene names these by the same pointers, so Renderer::update re-uploads none of
	// them from one frame to the next: a frame costs what moved. Every texture is held as the device
	// stores it (render::storedTexture), made so by the job that read, baked or drew it, so a host that
	// uploads on its frame loop pays the copy alone when one lands.
	struct SceneAssets
	{
		std::map<core::Uuid, PropMesh> props;
		std::map<core::Uri, std::shared_ptr<const image::Image>> pictures;
		std::map<BakeKey, std::shared_ptr<const image::Image>> baked;
		std::map<core::Uuid, BoardSheet> boards;
		// Every flat surface (the room's, the planes', the boxes'), in the order renderScene draws them, in
		// the plan frame; and every pole by its mount, in its mount's frame. Neither moves with a frame,
		// and a pole's size has no tolerance, so the plan alone decides them.
		std::vector<std::shared_ptr<const mesh::Mesh>> surfaces;
		std::map<core::Uuid, std::shared_ptr<const mesh::Mesh>> poles;
		// Why each asset job that failed made nothing, by its key, so it is not tried again while the plan
		// still asks for the same thing (pruneAssets forgets it once the plan does not).
		std::map<std::string, std::vector<std::string>> failed;
	};

	// What one asset job made, for addAsset to keep on the caller's thread.
	struct AssetOutcome
	{
		std::string key;
		std::function<void(SceneAssets&)> keep; // adds what it made; empty when it made nothing
		std::vector<std::string> problems;		// why it made nothing, each naming its owner and file
	};

	// One piece of the work of loading a rig plan's assets: a picture or a prop's mesh to read, a
	// procedural surface to bake, a board to draw. It runs on any thread and reads only what it captured,
	// never the plan, so the plan may change while it runs; what it makes is kept by addAsset.
	struct AssetJob
	{
		std::string key;  // what it makes, unique among the jobs a plan needs at once
		std::string what; // for a person: "baking the room's floor"
		std::function<AssetOutcome()> run;
	};

	// The surfaces' and poles' meshes `plan` draws, made at once: they are small, and resizing a room or
	// a box changes them every frame of a drag.
	void sceneGeometry(const RigPlan& plan, SceneAssets& assets);

	// The jobs that would make what `plan` draws and `assets` lacks, in the order loadSceneAssets runs
	// them: the surfaces' pictures and bakes, the props' meshes, the pictures loaded props' materials
	// name, the boards. An asset made from what has since changed (a prop's file, a board's pattern) is
	// lacking; one whose job failed is not, until pruneAssets forgets the failure.
	std::vector<AssetJob> missingAssets(const RigPlan& plan, const SceneAssets& assets);

	// Keeps what a job made, replacing what was made before under the same owner, or remembers why it
	// made nothing.
	void addAsset(SceneAssets& assets, AssetOutcome outcome);

	// Drops what `plan` no longer draws, and the failures of jobs it no longer needs.
	void pruneAssets(const RigPlan& plan, SceneAssets& assets);

	struct AssetsResult
	{
		SceneAssets assets;
		std::vector<std::string> problems; // what did not load, each naming its owner and file
	};

	// Every asset `plan` draws, its paths absolute (a document's are resolved when it is read): its
	// geometry, then every missing asset's job run on the calling thread, in missingAssets' order, until
	// nothing is missing. A prop whose mesh or clip will not load, a picture that will not, or a board the
	// board renderer will not draw (a build with none is the commonest) is a problem naming it, and the
	// rest still loads. A rig plan with a board holds it in any build; only drawing it needs a renderer,
	// and a capture is refused rather than drawn without the board, whose ground truth would then
	// describe corners its pixels lack. A picture that states no colour space is taken as sRGB, as the
	// web takes one, with a warning in the log, since the renderer refuses an untagged texture.
	AssetsResult loadSceneAssets(const RigPlan& plan);

	// What a plan object a render scene's instance draws is.
	enum class SceneObject
	{
		Room, // one of its surfaces
		Plane,
		Box, // one of its faces
		Prop,
		Board, // its print or its back
		Pole,  // a pole mount's
	};

	struct SceneOwner
	{
		SceneObject object;
		core::Uuid id; // nil for the room, which has no identity
	};

	struct SceneResult
	{
		render::Scene scene;
		std::vector<SceneOwner> owners;	   // one per instance of `scene`, in its order: what each draws
		std::vector<std::string> problems; // an asset `assets` lacks; the scene is incomplete when any
	};

	// `realisation` of `plan` at capture frame `frame`, as a render scene in the plan frame:
	//   * the room's present surfaces, each facing in, and the planes and boxes, each a rectangle per
	//     face whose picture reads upright from the side it faces (a floor's with north at its top);
	//   * each prop's mesh, or the frame of its clip the capture frame shows (mesh::clipFrame), placed
	//     by planFromProp at that frame, its file's own materials mapped one for one (MTL's Kd, map_Kd,
	//     Ks, Ns);
	//   * each board's sheet at planFromBoard: its print white under its raster, its back plain white;
	//   * each pole, a cylinder from the floor to its top at its realised mount, a matte mid grey;
	//   * each light, its specification's kind, colour, intensity and cone at its realised pose.
	// A picture repeats every tile, in metres; a procedural texture is baked once per surface at the
	// surface's own extent, so it reads proceduralGrey at each point of it, in metres from the
	// surface's corner, with its seed mixed with the surface's identity so that no two surfaces of one
	// material repeat each other (a targetless method would match them as one). A rig plan states no
	// ambient light: what lights it is its lights. A surface whose picture or bake `assets` lacks is a
	// problem and is drawn a matte mid grey, as is a prop's material whose picture it lacks, so a surface
	// waiting on its asset never reads as a lit white one.
	SceneResult renderScene(const RigPlan& plan, const Realisation& realisation, const SceneAssets& assets, std::size_t frame);

	// The same, of `plan` as it was meant (nominalLayout), for an editor: a pole whose mount, or a light
	// that, has no pose there is not drawn, since check() already says why.
	SceneResult renderScene(const RigPlan& plan, const NominalLayout& layout, const SceneAssets& assets, std::size_t frame);
} // namespace lain::rigplan
