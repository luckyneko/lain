#include "lain/io/mesh/obj/register.h"

#include <lain/io/mesh/load.h>

#include <tiny_obj_loader.h>

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace lain::io::mesh::obj
{
	static std::string textOf(const memory::Buffer& bytes)
	{
		return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	// Hands tinyobjloader an OBJ's MTL by reading it through the folder, never the file system.
	class FolderMaterialReader : public tinyobj::MaterialReader
	{
	public:
		explicit FolderMaterialReader(const MeshFolder& folder)
			: m_folder(folder)
		{
		}

		bool operator()(const std::string& name, std::vector<tinyobj::material_t>* materials,
						std::map<std::string, int>* names, std::string* warn, std::string* err) override
		{
			const std::optional<memory::Buffer> bytes = m_folder.read(name);
			if (!bytes)
			{
				if (warn)
					*warn += "material file '" + name + "' could not be read\n";
				return false;
			}
			std::istringstream in(textOf(*bytes));
			tinyobj::LoadMtl(names, materials, &in, warn, err);
			return true;
		}

	private:
		const MeshFolder& m_folder;
	};

	static image::ColorRGBf colourOf(const tinyobj::real_t (&rgb)[3])
	{
		return image::ColorRGBf{rgb[0], rgb[1], rgb[2]};
	}

	// An OBJ, triangulated, its corners shared where the file shares them: one vertex per distinct
	// (position, normal, texture coordinate) a corner names. Its faces are grouped into one part per
	// material, in the order the materials are listed.
	//
	// Three readings of what an OBJ leaves open, each a choice rather than an error:
	//   * normals are all or none: if any corner lacks one the mesh has none, and a renderer derives
	//     each face's own, since a mesh with half its normals would shade inconsistently;
	//   * a corner with no texture coordinate gets (0, 0) when others have them, since untextured parts
	//     of a textured mesh are common;
	//   * a face with no material, or a material its MTL lacks (an MTL that cannot be read included),
	//     gets a default one: a plain grey, so a mesh whose MTL went missing still shows its shape.
	// A face naming a vertex the file does not have is not a mesh at all.
	class ObjReader : public MeshReader
	{
	public:
		std::optional<lain::mesh::Mesh> decode(const memory::Buffer& bytes, const MeshFolder& folder) const override
		{
			std::istringstream in(textOf(bytes));
			tinyobj::attrib_t attrib;
			std::vector<tinyobj::shape_t> shapes;
			std::vector<tinyobj::material_t> materials;
			std::string warn, err;
			FolderMaterialReader readMaterials(folder);
			if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &in, &readMaterials, /*triangulate*/ true,
								  /*default_vcols_fallback*/ false))
				return std::nullopt;

			lain::mesh::Mesh mesh;
			for (const tinyobj::material_t& m : materials)
			{
				lain::mesh::Material material;
				material.name = m.name;
				material.diffuse = colourOf(m.diffuse);
				material.specular = colourOf(m.specular);
				material.shininess = m.shininess;
				if (!m.diffuse_texname.empty())
					material.diffuseTexture = folder.locate(m.diffuse_texname);
				mesh.materials.push_back(material);
			}

			bool normals = true;
			bool uvs = false;
			for (const tinyobj::shape_t& shape : shapes)
			{
				for (const tinyobj::index_t& corner : shape.mesh.indices)
				{
					normals = normals && corner.normal_index >= 0;
					uvs = uvs || corner.texcoord_index >= 0;
				}
			}

			const auto vertexCount = int(attrib.vertices.size() / 3);
			const auto normalCount = int(attrib.normals.size() / 3);
			const auto uvCount = int(attrib.texcoords.size() / 2);
			std::map<std::tuple<int, int, int>, std::uint32_t> shared;
			// The triangles of each material, then one more list for faces with none of the MTL's.
			std::vector<std::vector<std::uint32_t>> byMaterial(materials.size() + 1);
			for (const tinyobj::shape_t& shape : shapes)
			{
				for (std::size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face)
				{
					if (shape.mesh.num_face_vertices[face] != 3)
						return std::nullopt; // triangulation failed on this face
					const int id = shape.mesh.material_ids[face];
					std::vector<std::uint32_t>& triangles =
						byMaterial[id >= 0 && std::size_t(id) < materials.size() ? std::size_t(id) : materials.size()];
					for (std::size_t c = 0; c < 3; ++c)
					{
						const tinyobj::index_t& corner = shape.mesh.indices[3 * face + c];
						const int n = normals ? corner.normal_index : -1;
						const int t = uvs ? corner.texcoord_index : -1;
						if (corner.vertex_index < 0 || corner.vertex_index >= vertexCount || n >= normalCount || t >= uvCount)
							return std::nullopt;
						const auto [at, added] = shared.try_emplace({corner.vertex_index, n, t}, std::uint32_t(mesh.positions.size()));
						if (added)
						{
							const tinyobj::real_t* p = &attrib.vertices[3 * std::size_t(corner.vertex_index)];
							mesh.positions.push_back({p[0], p[1], p[2]});
							if (normals)
							{
								const tinyobj::real_t* q = &attrib.normals[3 * std::size_t(n)];
								mesh.normals.push_back({q[0], q[1], q[2]});
							}
							if (uvs)
							{
								const tinyobj::real_t* u = t >= 0 ? &attrib.texcoords[2 * std::size_t(t)] : nullptr;
								mesh.uvs.push_back(u ? math::Vec2f{u[0], u[1]} : math::Vec2f{0.0f, 0.0f});
							}
						}
						triangles.push_back(at->second);
					}
				}
			}

			if (!byMaterial.back().empty())
			{
				lain::mesh::Material fallback;
				fallback.name = "default";
				mesh.materials.push_back(fallback);
			}
			for (std::size_t m = 0; m < byMaterial.size(); ++m)
			{
				if (byMaterial[m].empty())
					continue;
				mesh.parts.push_back({m, std::uint32_t(mesh.indices.size()), std::uint32_t(byMaterial[m].size())});
				mesh.indices.insert(mesh.indices.end(), byMaterial[m].begin(), byMaterial[m].end());
			}
			return mesh;
		}
	};

	void registerCodec()
	{
		readerRegistry().registerType<ObjReader>("obj");
	}
} // namespace lain::io::mesh::obj
