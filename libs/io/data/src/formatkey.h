#pragma once

// The format key derived from a uri — shared by io::data's load (read) and save (write), private
// to the library. Kept here so the read and write paths key formats identically. (Same helper as
// io::image's; duplicated rather than shared so neither seam depends on the other.)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace lain::io::data
{
	// The lowercase format key for a uri: its file extension without the leading dot, or empty when
	// there is none. path::extension reads the last component's extension, so a scheme prefix
	// ("file://dir/x.JSON") is harmless and the result here is "json".
	inline std::string formatKeyFromUri(std::string_view uri)
	{
		std::string ext = std::filesystem::path(uri).extension().string();
		if (!ext.empty() && ext.front() == '.')
			ext.erase(ext.begin());
		std::transform(ext.begin(), ext.end(), ext.begin(),
					   [](unsigned char c)
					   { return static_cast<char>(std::tolower(c)); });
		return ext;
	}
} // namespace lain::io::data
