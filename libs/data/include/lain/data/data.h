#pragma once

#include "lain/data/archive.h"
#include "lain/data/details/reflect.h" // the dispatch engine toValue / fromValue bottom out on
#include "lain/data/macros.h"
#include "lain/data/strict.h"
#include "lain/data/value.h"

#include <optional>
#include <string>
#include <vector>

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
	Value toValue(const T& value)
	{
		return detail::writeValue(value);
	}

	// Reflect a Value back into a T, or nullopt on a shape/type mismatch (PURE — a failure leaves
	// no partial state). "The type is the schema": success == the data conformed to T. T must be
	// default-constructible.
	template <typename T>
	std::optional<T> fromValue(const Value& value)
	{
		T out{};
		if (detail::readValue(value, out))
			return out;
		return std::nullopt;
	}

	// The outcome of a strict read: the value, or every way the document failed to be one.
	template <typename T>
	struct StrictRead
	{
		std::optional<T> value;
		std::vector<std::string> problems; // empty exactly when `value` is set
	};

	// fromValue, then shapeDifferences between the document and the value written back: an unknown or
	// missing key, an unrecognised enum or variant name, or a value of the wrong kind is a problem named
	// by its path instead of a default. For documents a person types, where best-effort reading would
	// hide a typo; fromValue stays the tolerant read for documents another build wrote.
	template <typename T>
	StrictRead<T> fromValueStrict(const Value& value)
	{
		StrictRead<T> result;
		std::optional<T> read = fromValue<T>(value);
		if (!read)
		{
			result.problems.push_back("the document does not have the expected shape");
			return result;
		}
		result.problems = shapeDifferences(value, toValue(*read));
		if (result.problems.empty())
			result.value = std::move(read);
		return result;
	}
} // namespace lain::data
