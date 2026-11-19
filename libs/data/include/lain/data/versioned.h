#pragma once

#include "lain/data/value.h"

#include <cstdint>
#include <string>
#include <vector>

// What every versioned document shares, whatever it holds — a camera model's, a rig's, a rig plan's:
// a version number first, then the body. Hoisted out of lain::camera::serialize when lain::rigplan::
// serialize became its second user.
namespace lain::data
{
	// {version, ...body}: the version first, so a reader meets it before anything it might not
	// understand. `body` must be an object.
	Value versionedDocument(std::uint32_t version, Value body);

	// Why `document` cannot be read at all as a `kind` document ("camera-model", "rig") no newer than
	// `newest`: it is not an object, or it has no version, or a newer one. Empty when it can be read.
	std::vector<std::string> versionProblems(const Value& document, const char* kind, std::uint32_t newest);
} // namespace lain::data
