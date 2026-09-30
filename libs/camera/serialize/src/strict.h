#pragma once

#include <lain/data/value.h>

#include <string>
#include <vector>

namespace lain::camera::detail
{
	// Every way `stored` (what a document said) differs in SHAPE from `reencoded` (what reading it and
	// writing it back produced), as problems named by path: a key one has and the other does not, a
	// string that came back different (an enum name nothing recognised, so it reverted to a default),
	// or a value of another kind. Numbers are compared by kind only, since a number may come back in
	// another numeric arm (a positive Int reads as UInt) without meaning anything else.
	//
	// This is how the camera readers stay strict over lain::data, which reads a member best-effort on
	// purpose: reading tolerates, and this reports what the tolerance hid. Private to camera::serialize
	// until a second format wants it, when it belongs in lain::data.
	std::vector<std::string> shapeDifferences(const data::Value& stored, const data::Value& reencoded,
											  const std::string& path = "");
} // namespace lain::camera::detail
