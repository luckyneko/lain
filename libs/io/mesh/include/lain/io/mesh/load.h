#pragma once

#include "lain/io/mesh/reader.h"

#include <lain/core/factory.h>
#include <lain/core/uri.h>
#include <lain/media/framespec.h>
#include <lain/mesh/mesh.h>

#include <optional>
#include <string_view>

namespace lain::io::mesh
{
	// The reader registry: the process-wide table of MeshReaders keyed by a lowercase format key (the
	// file extension without the dot: "obj"). A plugin registers its reader here, and load() / decode()
	// look one up by key, as io::image's registry does.
	lain::core::Factory<MeshReader>& readerRegistry();

	// Decode in-memory `bytes` already known to be `formatKey`, reading what they name through `folder`.
	// Returns nothing when no reader is registered for the key, the reader rejects the bytes, or what it
	// produced is not a usable mesh (mesh::validate), the reason logged in each case.
	[[nodiscard]] std::optional<lain::mesh::Mesh> decode(std::string_view formatKey, const memory::Buffer& bytes,
														 const MeshFolder& folder);

	// Load the mesh at `uri`: read its bytes (io::read), pick the reader by its extension, and decode
	// it, reading what it names from its own folder. Nothing on any failure, the reason logged.
	[[nodiscard]] std::optional<lain::mesh::Mesh> load(const lain::core::Uri& uri);

	// Load a numbered run of meshes as one clip: every file matching `pattern` (naming io::frameKey, as
	// "walk.<frame:04>.obj"), in the order of their numbers, at `rate`. A clip is a whole asset, as a
	// mesh is, so this is a load. Nothing when no file matches or any frame fails to load: a clip with
	// a frame missing would jump, so the refusal names the frame instead.
	[[nodiscard]] std::optional<lain::mesh::MeshClip> loadClip(const lain::core::Uri& pattern, media::FrameRate rate = {});
} // namespace lain::io::mesh
