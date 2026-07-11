#pragma once

#include "lain/data/archive.h"
#include "lain/data/value.h"

#include <optional>

// lain::data — the central header + the T <-> Value reflection facade. This is NOT serialization
// (that is Value <-> bytes, in lain::io::data): a Value is a tree, not a byte stream — hence the
// toValue / fromValue verbs.
//
// A type declares its mapping with ONE function, `serialize(Archive&, T&)` (free/ADL, or a member
// `.serialize(Archive&)`), naming each field once and serving BOTH directions — the Archive carries
// the direction. That single visitor is the "simpler than nlohmann" win (no paired to_json/from_json
// with duplicated keys) and the seam a future streaming/binary backend slots behind without touching
// any serialize.
//
//   struct Vec3 { float x, y, z; };
//   void serialize(lain::data::Archive& ar, Vec3& v)
//   {
//       ar.member("x", v.x).member("y", v.y).member("z", v.z);
//   }
//
// Include this header to CALL the facade; include archive.h alone to WRITE a serialize().
//
// Deferred (next slice): enum-as-name (meta::enums), tagged variant, member Bytes, std::map, and
// the LAIN_SERIALIZE(T, fields...) macro that expands to exactly the visitor above.
namespace lain::data
{
	// Reflect a C++ value into a Value tree. Handles arithmetic / bool / std::string, std::vector,
	// std::optional, and any type with a serialize(). Format-neutral — no bytes, no JSON.
	template <typename T>
	Value toValue(const T& value);

	// Reflect a Value back into a T, or nullopt on a shape/type mismatch (PURE — a failure leaves
	// no partial state). "The type is the schema": success == the data conformed to T. T must be
	// default-constructible.
	template <typename T>
	std::optional<T> fromValue(const Value& value);
} // namespace lain::data

#include "lain/data/details/data.inl"
