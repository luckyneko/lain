#pragma once

namespace lain::io::mesh::obj
{
	// Register this plugin's OBJ reader into the io::mesh registry (under "obj"). Called once by the
	// generated registerMeshCodecs() aggregator — or directly by a consumer that wants only OBJ. Nothing in
	// the io::mesh core names tinyobjloader.
	void registerCodec();
} // namespace lain::io::mesh::obj
