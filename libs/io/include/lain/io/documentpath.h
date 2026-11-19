#pragma once

#include <filesystem>
#include <string>

namespace lain::io
{
	// ADR-0027 for ONE path: a document names the files it needs relative to itself on disk, and holds
	// them absolute in memory. Every document format that stores a path applies these two at the file
	// boundary (a graph document's path params, a rig plan's meshes and textures), so the rule has one
	// implementation, and its subtleties stay fixed once. Hoisted from flow::serialize when the rig plan
	// document became its second user.
	//
	// All of it is LEXICAL. Nothing reads the file system: whether a path is "inside" a folder is a
	// question about spellings, answered the same way for a file that does not exist yet. Both sides are
	// made absolute and lexically normal, and neither is canonicalised, since resolving symlinks on one
	// side only (/var against /private/var on macOS) would put a file "outside" the folder it is in.
	//
	// An empty path ("no file chosen yet") and a uri with a scheme are returned as written: rebasing an
	// empty path would name the document's own folder, and a uri must never pass through
	// std::filesystem, whose libstdc++ generic_string() collapses "s3://bucket" into "s3:/bucket".

	// What a document saved in `folder` writes for `text`. A path inside the folder's tree is written
	// relative to it; one outside stays absolute, since `../` is never written: a document moved on its
	// own still finds footage kept elsewhere, where a `../` chain would break exactly then. A relative
	// `text` is the working directory's, since that is what the OS reads it against, so it is made
	// absolute first.
	[[nodiscard]] std::string relativizePath(const std::string& text, const std::filesystem::path& folder);

	// What a document read from `folder` holds in memory for the stored `text`: a relative path read
	// against the folder, an absolute one as it is.
	[[nodiscard]] std::string resolvePath(const std::string& text, const std::filesystem::path& folder);
} // namespace lain::io
