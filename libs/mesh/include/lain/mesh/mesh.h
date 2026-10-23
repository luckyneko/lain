#pragma once

#include <lain/core/uri.h>
#include <lain/image/color.h>
#include <lain/math/types.h>
#include <lain/media/framespec.h> // FrameRate

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lain::mesh
{
	// A surface's material as a mesh file states it (an OBJ's MTL): what the file says, unjudged.
	// A rig plan maps it onto its own closed set of materials (CONTEXT.md, "Material").
	struct Material
	{
		std::string name;
		image::ColorRGBf diffuse{0.8f, 0.8f, 0.8f};	 // MTL Kd
		std::optional<core::Uri> diffuseTexture;	 // MTL map_Kd, absolute: resolved against the file's own folder
		image::ColorRGBf specular{0.0f, 0.0f, 0.0f}; // MTL Ks
		float shininess = 0;						 // MTL Ns, the specular exponent
	};

	// A run of a mesh's triangles that share one material.
	struct Part
	{
		std::size_t material = 0;	  // an index into Mesh::materials
		std::uint32_t firstIndex = 0; // the first of its indices in Mesh::indices
		std::uint32_t indexCount = 0; // three per triangle
	};

	// An indexed triangle mesh, in its file's own frame and units: whoever places it says which axis is
	// up and what a unit is, and converts it once. Each vertex has a position, and a normal and a texture
	// coordinate when the file gives them all (normals and uvs are empty or as long as positions).
	struct Mesh
	{
		std::vector<math::Vec3f> positions;
		std::vector<math::Vec3f> normals;
		std::vector<math::Vec2f> uvs;
		std::vector<std::uint32_t> indices; // three per triangle, into the vertices
		std::vector<Part> parts;			// in order, together covering every index once
		std::vector<Material> materials;
	};

	// Why `mesh` is not a usable mesh: no triangles, a normal or uv count that is not the vertex count,
	// an index count that is not a multiple of three or an index past the last vertex, parts that do
	// not cover the indices in order or name a material it lacks, or a position that is not finite.
	// Empty when it is usable. What a reader produced is checked against this before anyone draws it.
	std::vector<std::string> validate(const Mesh& mesh);

	// A finite run of meshes at a frame rate (CONTEXT.md, "Mesh clip"): an animation a prop may play on
	// a loop while its path moves it. A static mesh is a clip of one.
	struct MeshClip
	{
		std::vector<Mesh> frames;
		media::FrameRate rate; // unspecified means one clip frame per capture frame
	};

	// The clip frame a capture frame shows: the one nearest the capture frame's time, wrapping round
	// the clip so it loops, never a blend of two (the meshes of a clip need not share a topology). When
	// either rate is unspecified the clip advances one frame per capture frame. The arithmetic is exact,
	// in integers, so no rate drifts over a long capture. Precondition: the clip has a frame.
	std::size_t clipFrame(const MeshClip& clip, std::size_t captureFrame, media::FrameRate captureRate);
} // namespace lain::mesh
