#pragma once

// What a Renderer, a GpuScene and a Viewport hold on the device, and the recording they share.
// Private to lain::render.

#include "lain/render/capture.h"
#include "lain/render/scene.h"
#include "lain/render/view.h"

#include <archimedes/archimedes.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lain::render::detail
{
	// The shaders' blocks, std140 for the uniforms and std430 for the storage buffers. Each must match
	// its declaration in shaders/ exactly.
	struct FrameConstants
	{
		float clipFromWorld[16];
		float eye[4];
		float ambient[4];
		float cameraAxis[4]; // the camera's +Z in clip space: clipFromCamera's third column
		std::uint32_t lightCount;
		std::uint32_t encode; // 1: lain's sRGB curve, for a display
		std::uint32_t padding[2];
	};
	static_assert(sizeof(FrameConstants) == 128, "the Frame block is 128 bytes under std140");

	struct ObjectConstants
	{
		float worldFromMesh[16];
		float normalFromMesh[16];
		std::uint32_t flags;
		std::uint32_t padding[3];
	};
	static_assert(sizeof(ObjectConstants) == 144, "the Object block is 144 bytes under std140");
	inline constexpr std::uint32_t kHasNormals = 1u;

	struct MaterialConstants
	{
		float diffuse[4];  // w: 1 when the material has a texture
		float specular[4]; // w: Blinn-Phong's exponent
	};

	struct LightConstants
	{
		float position[4];
		float direction[4];
		float radiance[4];
		float cone[4];
	};
	static_assert(sizeof(LightConstants) == 64, "a Light is 64 bytes under std430");

	struct LineVertex
	{
		float position[4];
		float colour[4];
	};
	static_assert(sizeof(LineVertex) == 32, "a LineVertex is 32 bytes under std430");

	// A capture's per-tile and per-band constants (capture.comp), std140.
	struct TileConstants
	{
		float face[4];			// focal length, principal point x and y
		std::int32_t texels[4]; // the tile's first texel column and row, then its last
		std::int32_t open[4];	// its left, top, right and bottom edges, 1 when open
		std::int32_t pixels[4]; // the camera pixels it serves: first column and row, then end
		std::int32_t rule[4];	// its face's axis, 1 when faces are chosen by major axis, s, the image's width
	};
	static_assert(sizeof(TileConstants) == 80, "the Tile block is 80 bytes under std140");

	struct BandConstants
	{
		std::int32_t rows[4]; // the band's first camera row, then its end
	};

	struct Vertex
	{
		float position[3];
		float normal[3];
		float uv[2];
	};

	void copyMatrix(const math::Mat4d& m, float (&out)[16]);

	struct RendererState
	{
		acm::Device* device = nullptr;
		std::vector<std::string> problems;
		acm::DescriptorSetLayout sceneLayout; // a surface's: the frame, its instance, its material, a texture, the lights
		acm::DescriptorSetLayout lineLayout;  // a line's: the frame, the line vertices
		acm::Pipeline pipeline;				  // the offscreen pass's
		acm::Sampler sampler;
		acm::Shader sceneVertex;
		acm::Shader sceneFragment;
		acm::Shader lineVertex;
		acm::Shader lineFragment;
		acm::DescriptorSetLayout captureLayout; // a remap's: its tile, its band, the tile's texels, the band's corners and sums
		acm::Shader captureShader;
		acm::ComputePipeline capturePipeline;
	};

	struct SceneState
	{
		const RendererState* owner = nullptr; // the renderer that uploaded it
		std::uint64_t serial = 0;			  // unique to this upload in the process, for whoever caches against it

		struct Mesh
		{
			acm::Buffer vertices;
			acm::Buffer indices;
		};
		std::vector<Mesh> meshes;
		std::vector<acm::Texture> textures; // the scene's, in its order
		acm::Texture white;					// fills the binding of a material without a texture
		acm::Buffer objects;				// an ObjectConstants per instance, `objectStride` apart
		acm::Buffer lights;
		std::vector<acm::Buffer> materials;
		std::vector<std::size_t> materialTextures; // per material, its texture's index, or none

		// The synchronous drawing's frame constants and its descriptor sets, one per material, after
		// the buffers they name. A viewport keeps its own, per frame in flight.
		acm::Buffer frame;
		std::vector<acm::DescriptorSet> sets;

		// One draw per part of each instance, in the scene's order.
		struct Draw
		{
			std::size_t mesh;
			std::size_t material;
			std::uint32_t objectOffset;
			std::uint32_t firstIndex;
			std::uint32_t indexCount;
		};
		std::vector<Draw> draws;

		image::ColorRGBf ambient;
		image::ColorRGBf background;
		std::uint32_t lightCount = 0;
	};

	// A ray table's copy on the device (RayTable, ADR-0028): its corners and sums in bands, and the one
	// texture every tile is drawn into in turn, sized for the largest.
	struct RayTableState
	{
		const RendererState* owner = nullptr;
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		std::uint32_t samples = 1;
		bool cube = false;
		std::vector<CaptureFace> faces;
		std::vector<CaptureTile> tiles;

		struct Band
		{
			std::uint32_t first = 0; // its first camera row
			std::uint32_t end = 0;	 // and its end
			acm::Buffer corners;	 // (end - first + 1) x (width + 1) vec4s
			acm::Buffer sums;		 // (end - first) x width vec4s
			acm::Buffer constants;	 // its BandConstants
			acm::DescriptorSet set;
		};
		std::vector<Band> bands;

		acm::Buffer tileConstants; // rewritten before each tile's submission
		acm::Texture texels;	   // a tile's picture: RGBA32F, a storage image as well as a target
		acm::RenderTarget target;
	};

	// A surface's descriptor set for `material`, reading its per-view constants from `frame`.
	acm::DescriptorSet sceneSet(const RendererState& renderer, const SceneState& scene, std::size_t material, const acm::Buffer& frame);

	// `view`'s constants for `scene`.
	FrameConstants frameConstants(const SceneState& scene, const View& view, bool encode);

	// Records every draw of `scene` through `pipeline` into an open rendering scope, with `sets` (one
	// per material). Returns the first refusal, or nothing.
	acm::Error recordScene(acm::CommandBuffer& cmd, const acm::Pipeline& pipeline, const SceneState& scene,
						   const std::vector<acm::DescriptorSet>& sets);

	// The scene pipeline, for targets with the formats and sample count of `target`.
	acm::Pipeline scenePipeline(const RendererState& renderer, const acm::RenderTarget& target);
} // namespace lain::render::detail
