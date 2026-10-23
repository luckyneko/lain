#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lain::io
{
	// The key a numbered run of files names its frame number by: "shot.<frame:04>.png" (lain::string's
	// pattern grammar). ONE spelling for every reader and writer of such a run — a render sweep writes
	// it, an image sequence and a mesh clip read it — since two spellings would mean what one writes
	// cannot be read back, and the disagreement would be silent. Hoisted from io::image when a mesh clip
	// became its second reader.
	//
	// ":04" is the padding CONVENTION, not part of the key: a width is a writing decision and no filter
	// at all on the way in, so "shot.7.png" and "shot.0007.png" both match.
	inline constexpr std::string_view frameKey = "frame";

	// The files matching a pattern that names frameKey in its filename, ordered NUMERICALLY by the
	// number there. The key marks where the number is, not how many digits to insist on, so an unpadded
	// "shot.7.png" is found and sorted as 7, which is the case a lexicographic directory listing gets
	// wrong. A name whose number is not a number ("shot.x.png") does not match. Local files only; an
	// unreadable folder yields nothing.
	std::vector<std::string> numberedFiles(const std::filesystem::path& pattern);
} // namespace lain::io
