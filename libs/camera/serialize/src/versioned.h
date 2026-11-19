#pragma once

#include <lain/data/value.h>

#include <cstdint>
#include <string>
#include <vector>

// What every versioned camera document shares, whatever it holds: a camera model's, a rig's. Private
// to lain::camera::serialize.
namespace lain::camera
{
	// {version, ...body}: the version first, so a reader meets it before anything it might not
	// understand.
	data::Value versionedDocument(std::uint32_t version, data::Value body);

	// Why `document` cannot be read at all as a `kind` document ("camera-model", "rig") no newer than
	// `newest`: it is not an object, or it has no version, or a newer one. Empty when it can be read.
	std::vector<std::string> versionProblems(const data::Value& document, const char* kind, std::uint32_t newest);
} // namespace lain::camera
