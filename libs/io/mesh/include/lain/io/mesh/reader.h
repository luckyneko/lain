#pragma once

#include <lain/core/uri.h>
#include <lain/memory/buffer.h>
#include <lain/mesh/mesh.h>

#include <optional>
#include <string_view>

namespace lain::io::mesh
{
	// The folder a mesh file lies in, as its reader may reach it. A mesh format names files beside itself
	// (an OBJ its MTL, an MTL its textures), and a reader is never given a path to open them with: it is
	// handed the bytes it decodes and this, which the facade implements over io::read. So no codec plugin
	// opens a file of its own (ADR-0004), as a video reader is handed a stream rather than a uri.
	class MeshFolder
	{
	public:
		virtual ~MeshFolder() = default;

		// The bytes of a file the mesh names, by the name it gives it, relative to the mesh's own folder;
		// nothing when it cannot be read (the read logs why). A backslash separates folders as a slash
		// does, since meshes written on Windows name their files that way.
		virtual std::optional<memory::Buffer> read(std::string_view name) const = 0;

		// Where a file the mesh only refers to (a texture) lives, as an absolute uri; nothing when the
		// mesh came from no folder (decoded from memory).
		virtual std::optional<core::Uri> locate(std::string_view name) const = 0;
	};

	// The folder of a mesh decoded from memory: there is nothing beside it, so a format that names files
	// beside itself cannot be decoded this way.
	class NoFolder : public MeshFolder
	{
	public:
		std::optional<memory::Buffer> read(std::string_view) const override { return std::nullopt; }
		std::optional<core::Uri> locate(std::string_view) const override { return std::nullopt; }
	};

	// A Reader decodes one mesh format's bytes into a lain::mesh::Mesh, reading what the mesh names
	// through `folder`. One implementation per format; concrete readers live in plugins
	// (plugins/io/mesh/<fmt>) and register into the reader registry, so lain::io::mesh pulls in no
	// parser itself. Expected to be stateless: the registry constructs one per decode.
	class MeshReader
	{
	public:
		virtual ~MeshReader() = default;

		// The mesh `bytes` hold, or nothing when they are not one of this format. The facade checks what
		// comes back (mesh::validate) and logs a refusal, so a reader never has to log or throw itself.
		virtual std::optional<lain::mesh::Mesh> decode(const memory::Buffer& bytes, const MeshFolder& folder) const = 0;
	};
} // namespace lain::io::mesh
