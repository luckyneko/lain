#pragma once

#include <lain/data/value.h>

#include <filesystem>

// Document-relative paths (ADR-0027). A param stored under kPathTypeKey names a FILE, and the format
// says where a relative one points: at the folder of the document that stored it. In memory every
// such path is absolute; on disk one inside the document's folder tree is relative to it, and one
// outside stays absolute.
//
// These are transforms over a whole document Value, applied by a host at the FILE boundary: after
// toValue when writing a file, before fromValue when reading one (a linked template included, against
// its own folder). Anything that never touches a file — an undo snapshot — is simply not transformed,
// which is why the serializer itself needs no idea where a document lives.
namespace lain::flow::serialize
{
	// The value-codec key a file reference is stored under. Part of the format, not a host's choice:
	// a host registers its std::filesystem::path codec under this key, and the transforms below find
	// path params by it.
	inline constexpr const char* kPathTypeKey = "path";

	// Rewrite every path param in `document` (nested bodies included) into the form a file in
	// `folder` stores. Inside the folder's tree: relative. Outside: absolute; `../` is never written. A
	// relative in-memory path means the working directory, so it is made absolute first. An empty path
	// and a uri with a scheme are left as they are.
	void relativizePaths(data::Value& document, const std::filesystem::path& folder);

	// The inverse, for a document read from a file in `folder`: every relative path param is read
	// against the folder, so afterwards each one is absolute. An absolute path, an empty one and a uri
	// are left as they are.
	void resolvePaths(data::Value& document, const std::filesystem::path& folder);
} // namespace lain::flow::serialize
