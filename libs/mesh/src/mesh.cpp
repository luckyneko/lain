#include "lain/mesh/mesh.h"

#include <lain/string/format.h>

#include <cassert>
#include <cmath>

namespace lain::mesh
{
	std::vector<std::string> validate(const Mesh& mesh)
	{
		std::vector<std::string> problems;
		const std::size_t vertices = mesh.positions.size();
		if (vertices == 0 || mesh.indices.empty())
			problems.push_back("the mesh has no triangles");
		if (!mesh.normals.empty() && mesh.normals.size() != vertices)
			problems.push_back(string::format("the mesh has {} normals for {} vertices", mesh.normals.size(), vertices));
		if (!mesh.uvs.empty() && mesh.uvs.size() != vertices)
			problems.push_back(string::format("the mesh has {} texture coordinates for {} vertices", mesh.uvs.size(), vertices));
		if (mesh.indices.size() % 3 != 0)
			problems.push_back(string::format("the mesh has {} indices, which is not whole triangles", mesh.indices.size()));
		for (std::size_t i = 0; i < mesh.indices.size(); ++i)
		{
			if (mesh.indices[i] >= vertices)
			{
				problems.push_back(string::format("index {} names vertex {} of {}", i, mesh.indices[i], vertices));
				break; // one is enough to say the indices are broken
			}
		}
		for (std::size_t v = 0; v < vertices; ++v)
		{
			const math::Vec3f& p = mesh.positions[v];
			if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
			{
				problems.push_back(string::format("vertex {} has a position that is not finite", v));
				break;
			}
		}

		// The parts tile the indices: in order, each whole triangles, together every index once.
		std::size_t next = 0;
		for (std::size_t k = 0; k < mesh.parts.size(); ++k)
		{
			const Part& part = mesh.parts[k];
			if (part.firstIndex != next || part.indexCount % 3 != 0)
				problems.push_back(string::format("part {} does not continue the parts before it in whole triangles", k));
			if (part.material >= mesh.materials.size())
				problems.push_back(string::format("part {} names material {} of {}", k, part.material, mesh.materials.size()));
			next = std::size_t(part.firstIndex) + part.indexCount;
		}
		if (next != mesh.indices.size())
			problems.push_back(string::format("the parts cover {} of the mesh's {} indices", next, mesh.indices.size()));
		return problems;
	}

	std::size_t clipFrame(const MeshClip& clip, std::size_t captureFrame, media::FrameRate captureRate)
	{
		assert(!clip.frames.empty());
		const std::size_t count = clip.frames.size();
		if (!captureRate.specified() || !clip.rate.specified())
			return captureFrame % count;
		// The capture frame's time, captureFrame / captureRate seconds, in clip frames, rounded half up:
		// (captureFrame * capture.den * clip.num) / (capture.num * clip.den), all in integers.
		const std::uint64_t numerator = std::uint64_t(captureFrame) * captureRate.denominator * clip.rate.numerator;
		const std::uint64_t denominator = std::uint64_t(captureRate.numerator) * clip.rate.denominator;
		const std::uint64_t nearest = (2 * numerator + denominator) / (2 * denominator);
		return std::size_t(nearest % count);
	}
} // namespace lain::mesh
