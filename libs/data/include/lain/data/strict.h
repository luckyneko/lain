#pragma once

#include "lain/data/value.h"

#include <string>
#include <vector>

namespace lain::data
{
	// Every way `stored` (what a document said) differs in SHAPE from `reencoded` (what reading it and
	// writing it back produced), as problems named by path: a key one has and the other lacks, a
	// string that came back different (an enum or variant name nothing recognised, so it reverted to
	// a default), an array of another length, or a value of another kind. Numbers are compared by kind
	// only, since a number may come back in another numeric arm (a positive Int reads as UInt, a float
	// as a double) without meaning anything else.
	//
	// It is how a reader stays strict over fromValue, which reads a struct member best-effort ON
	// PURPOSE (a document written by a newer build still loads): reading tolerates, and this reports
	// what the tolerance hid. fromValueStrict (data.h) is the usual way in.
	std::vector<std::string> shapeDifferences(const Value& stored, const Value& reencoded);
} // namespace lain::data
