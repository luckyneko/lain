// Meshes: what makes one usable, and which clip frame a capture frame shows.

#include <lain/mesh/mesh.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using lain::mesh::Mesh;
using lain::mesh::MeshClip;

static lain::mesh::Material material(const char* name)
{
	lain::mesh::Material m;
	m.name = name;
	return m;
}

// Two triangles in two parts, each with its own material.
static Mesh quad()
{
	Mesh m;
	m.positions = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
	m.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
	m.uvs = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	m.indices = {0, 1, 2, 0, 2, 3};
	m.materials = {material("red"), material("blue")};
	m.parts = {{0, 0, 3}, {1, 3, 3}};
	return m;
}

TEST_CASE("a well-formed mesh is usable", "[mesh]")
{
	REQUIRE(lain::mesh::validate(quad()).empty());
	Mesh bare = quad();
	bare.normals.clear(); // normals and uvs are optional, together or apart
	bare.uvs.clear();
	REQUIRE(lain::mesh::validate(bare).empty());
}

TEST_CASE("a mesh with a broken part is not usable", "[mesh]")
{
	const auto refused = [](const Mesh& m)
	{
		return !lain::mesh::validate(m).empty();
	};
	Mesh m = quad();
	SECTION("no triangles")
	{
		m.indices.clear();
		m.parts.clear();
		REQUIRE(refused(m));
	}
	SECTION("a normal per vertex, or none")
	{
		m.normals.pop_back();
		REQUIRE(refused(m));
	}
	SECTION("a texture coordinate per vertex, or none")
	{
		m.uvs.push_back({0, 0});
		REQUIRE(refused(m));
	}
	SECTION("indices in whole triangles")
	{
		m.indices.push_back(0);
		m.parts.back().indexCount = 4;
		REQUIRE(refused(m));
	}
	SECTION("an index past the last vertex")
	{
		m.indices[4] = 4;
		REQUIRE(refused(m));
	}
	SECTION("a position that is not finite")
	{
		m.positions[2].x = std::numeric_limits<float>::infinity();
		REQUIRE(refused(m));
	}
	SECTION("parts that leave indices uncovered")
	{
		m.parts.pop_back();
		REQUIRE(refused(m));
	}
	SECTION("parts out of order")
	{
		std::swap(m.parts[0], m.parts[1]);
		REQUIRE(refused(m));
	}
	SECTION("parts that overlap, though they end at the last index")
	{
		// Only the per-part check sees this: the total still comes out right.
		m.parts = {{0, 0, 3}, {1, 0, 6}};
		REQUIRE(refused(m));
	}
	SECTION("a part that splits a triangle, though the indices are whole triangles")
	{
		m.parts = {{0, 0, 4}, {1, 4, 2}};
		REQUIRE(refused(m));
	}
	SECTION("a part naming a material the mesh lacks")
	{
		m.parts[1].material = 2;
		REQUIRE(refused(m));
	}
}

static MeshClip clipOf(std::size_t frames, lain::media::FrameRate rate)
{
	MeshClip clip;
	clip.frames.assign(frames, quad());
	clip.rate = rate;
	return clip;
}

TEST_CASE("a clip frame is the one nearest the capture frame's time, looping", "[mesh][clip]")
{
	SECTION("at the capture's own rate, frame for frame, wrapping round")
	{
		const MeshClip clip = clipOf(4, {24, 1});
		const std::vector<std::size_t> expected{0, 1, 2, 3, 0, 1, 2, 3};
		for (std::size_t k = 0; k < expected.size(); ++k)
			REQUIRE(lain::mesh::clipFrame(clip, k, {24, 1}) == expected[k]);
	}
	SECTION("a 12 fps clip under a 24 fps capture, the nearest clip frame, half rounding up")
	{
		// Capture frame k is at k / 24 s, which is k / 2 clip frames.
		const MeshClip clip = clipOf(10, {12, 1});
		const std::vector<std::size_t> expected{0, 1, 1, 2, 2, 3, 3, 4};
		for (std::size_t k = 0; k < expected.size(); ++k)
			REQUIRE(lain::mesh::clipFrame(clip, k, {24, 1}) == expected[k]);
	}
	SECTION("NTSC over a film-rate clip, exactly, however long the capture")
	{
		// Capture frame k at 30000/1001 fps is at k * 1001 / 30000 s, which is k * 1001 * 24 / 30000
		// clip frames. A float would drift over a day of frames; integers do not.
		const MeshClip clip = clipOf(1000000, {24, 1});
		const std::size_t k = 2589410; // about a day at 29.97
		const std::size_t expected = (2 * k * 1001 * 24 + 30000) / (2 * 30000) % 1000000;
		REQUIRE(lain::mesh::clipFrame(clip, k, {30000, 1001}) == expected);
	}
	SECTION("either rate unspecified, frame for frame")
	{
		REQUIRE(lain::mesh::clipFrame(clipOf(3, {}), 4, {24, 1}) == 1);
		REQUIRE(lain::mesh::clipFrame(clipOf(3, {12, 1}), 4, {}) == 1);
	}
	SECTION("a static mesh is a clip of one")
	{
		REQUIRE(lain::mesh::clipFrame(clipOf(1, {24, 1}), 1234, {24, 1}) == 0);
	}
}
