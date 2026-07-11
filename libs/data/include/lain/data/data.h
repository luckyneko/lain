#pragma once

#include "lain/data/archive.h"
#include "lain/data/macros.h"
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
// Supported without a serialize(): arithmetic/bool/enum (as its name)/std::string/
// std::filesystem::path/std::vector/std::optional/std::map<std::string,V>/std::variant (tagged)/
// raw bytes (std::vector<std::byte>). One-liner declarations (macros.h): LAIN_SERIALIZE(T,
// fields...), LAIN_SERIALIZE_INTRUSIVE(...), LAIN_SERIALIZE_VARIANT_ARM(Arm, "key").
//
// Deferred: the memory::Buffer <-> Bytes bridge, untagged variant (try-each-arm), non-string-keyed
// maps, and the Value <-> bytes codecs (lain::io::data — json first).
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
