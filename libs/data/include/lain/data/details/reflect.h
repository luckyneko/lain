#pragma once

// The read/write dispatch ENGINE for the lain::data reflection layer — where the recursion
// lives. detail::writeValue (T -> Value) and detail::readValue (Value -> T) are the two symmetric
// ladders; the public toValue / fromValue (data.h) and Archive::member (archive.inl) are thin
// callers of them. Adding a supported category is one arm here; a new user type is one serialize()
// overload — the "layered definitions" model.

#include "lain/data/archive.h"
#include "lain/data/value.h"

#include <lain/meta/enums.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace lain::data::detail
{
	// --- category traits for the dispatch ladders ---

	template <typename T>
	struct is_optional : std::false_type
	{
	};
	template <typename U>
	struct is_optional<std::optional<U>> : std::true_type
	{
	};
	template <typename T>
	inline constexpr bool is_optional_v = is_optional<T>::value;

	template <typename T>
	struct is_vector : std::false_type
	{
	};
	template <typename U, typename A>
	struct is_vector<std::vector<U, A>> : std::true_type
	{
	};
	template <typename T>
	inline constexpr bool is_vector_v = is_vector<T>::value;

	// A std::string-keyed map serializes as an Object; a map with any other key type does not
	// (an Object's keys are strings). Non-string-keyed maps are a later arm (Array of pairs).
	template <typename T>
	struct is_string_map : std::false_type
	{
	};
	template <typename V, typename C, typename A>
	struct is_string_map<std::map<std::string, V, C, A>> : std::true_type
	{
	};
	template <typename T>
	inline constexpr bool is_string_map_v = is_string_map<T>::value;

	// The serialize() customization point, either shape. has_free_serialize's unqualified call is
	// found by ADL at instantiation (the user's serialize lives in their type's namespace); member
	// is preferred when both exist — the private-member escape hatch.
	template <typename T, typename = void>
	struct has_member_serialize : std::false_type
	{
	};
	template <typename T>
	struct has_member_serialize<T, std::void_t<decltype(std::declval<T&>().serialize(std::declval<Archive&>()))>>
		: std::true_type
	{
	};
	template <typename T>
	inline constexpr bool has_member_serialize_v = has_member_serialize<T>::value;

	template <typename T, typename = void>
	struct has_free_serialize : std::false_type
	{
	};
	template <typename T>
	struct has_free_serialize<T, std::void_t<decltype(serialize(std::declval<Archive&>(), std::declval<T&>()))>>
		: std::true_type
	{
	};
	template <typename T>
	inline constexpr bool has_free_serialize_v = has_free_serialize<T>::value;

	template <typename T>
	inline constexpr bool has_serialize_v = has_member_serialize_v<T> || has_free_serialize_v<T>;

	template <typename T>
	void callSerialize(Archive& ar, T& value)
	{
		if constexpr (has_member_serialize_v<T>)
			value.serialize(ar);
		else
			serialize(ar, value);
	}

	// --- write ladder: T -> Value ---

	template <typename T>
	Value writeValue(const T& value)
	{
		using U = std::remove_cv_t<std::remove_reference_t<T>>;
		if constexpr (std::is_same_v<U, bool>)
			return Value(value);
		else if constexpr (std::is_enum_v<U>)
		{
			// Enum as its name (human-readable, refactor-stable); an unnamed value falls back to
			// the underlying integer so it still round-trips.
			const std::string_view n = lain::meta::enums::name(value);
			if (!n.empty())
				return Value(std::string(n));
			return Value(static_cast<std::underlying_type_t<U>>(value));
		}
		else if constexpr (std::is_integral_v<U> || std::is_floating_point_v<U>)
			return Value(value);
		else if constexpr (std::is_same_v<U, std::string>)
			return Value(value);
		else if constexpr (std::is_same_v<U, std::filesystem::path>)
			return Value(value.generic_string()); // portable forward-slash form
		else if constexpr (std::is_same_v<U, std::vector<std::byte>>)
			return Value(value); // Bytes
		else if constexpr (is_optional_v<U>)
			return value ? writeValue(*value) : Value();
		else if constexpr (is_string_map_v<U>)
		{
			Value out = Value::object();
			for (const auto& [key, mapped] : value)
				out.set(key, writeValue(mapped));
			return out;
		}
		else if constexpr (is_vector_v<U>)
		{
			Value out = Value::array();
			for (const auto& e : value)
				out.push(writeValue(e));
			return out;
		}
		else if constexpr (has_serialize_v<U>)
		{
			Value out = Value::object();
			Archive ar(Archive::Mode::Save, &out);
			// Save only reads the value; serialize's non-const T& is honoured with a const_cast
			// (the single-function-both-directions tradeoff, as in cereal).
			callSerialize(ar, const_cast<U&>(value));
			return out;
		}
		else
		{
			static_assert(sizeof(T) == 0, "lain::data::toValue: T needs a serialize(Archive&, T&) or a supported std type");
			return Value();
		}
	}

	// --- read ladder: Value -> T (returns false, out untouched, on a mismatch) ---

	template <typename I>
	bool readIntegral(const Value& v, I& out)
	{
		// Accept the matching signedness first, then the other arm if it fits — every narrow C++
		// integer is served by the DOM's Int64/UInt64 with a deliberate range check.
		constexpr I lo = std::numeric_limits<I>::min();
		constexpr I hi = std::numeric_limits<I>::max();
		if constexpr (std::is_signed_v<I>)
		{
			if (auto i = v.asInt64())
			{
				if (*i >= static_cast<std::int64_t>(lo) && *i <= static_cast<std::int64_t>(hi))
				{
					out = static_cast<I>(*i);
					return true;
				}
				return false;
			}
			if (auto u = v.asUInt64())
			{
				if (*u <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) && static_cast<std::int64_t>(*u) <= static_cast<std::int64_t>(hi))
				{
					out = static_cast<I>(*u);
					return true;
				}
				return false;
			}
		}
		else
		{
			if (auto u = v.asUInt64())
			{
				if (*u <= static_cast<std::uint64_t>(hi))
				{
					out = static_cast<I>(*u);
					return true;
				}
				return false;
			}
			if (auto i = v.asInt64())
			{
				if (*i >= 0 && static_cast<std::uint64_t>(*i) <= static_cast<std::uint64_t>(hi))
				{
					out = static_cast<I>(*i);
					return true;
				}
				return false;
			}
		}
		return false;
	}

	template <typename T>
	bool readValue(const Value& v, T& out)
	{
		using U = std::remove_cv_t<std::remove_reference_t<T>>;
		if constexpr (std::is_same_v<U, bool>)
		{
			if (auto b = v.asBool())
			{
				out = *b;
				return true;
			}
			return false;
		}
		else if constexpr (std::is_enum_v<U>)
		{
			if (const std::string* s = v.asString())
			{
				if (auto e = lain::meta::enums::fromString<U>(*s))
				{
					out = *e;
					return true;
				}
				return false;
			}
			std::underlying_type_t<U> raw{}; // unnamed fallback: the underlying integer
			if (readIntegral(v, raw))
			{
				out = static_cast<U>(raw);
				return true;
			}
			return false;
		}
		else if constexpr (std::is_integral_v<U>)
		{
			return readIntegral(v, out);
		}
		else if constexpr (std::is_floating_point_v<U>)
		{
			if (auto d = v.asDouble())
			{
				out = static_cast<U>(*d);
				return true;
			}
			return false;
		}
		else if constexpr (std::is_same_v<U, std::string>)
		{
			if (const std::string* s = v.asString())
			{
				out = *s;
				return true;
			}
			return false;
		}
		else if constexpr (std::is_same_v<U, std::filesystem::path>)
		{
			if (const std::string* s = v.asString())
			{
				out = std::filesystem::path(*s);
				return true;
			}
			return false;
		}
		else if constexpr (std::is_same_v<U, std::vector<std::byte>>)
		{
			if (const std::vector<std::byte>* b = v.asBytes())
			{
				out = *b;
				return true;
			}
			return false;
		}
		else if constexpr (is_optional_v<U>)
		{
			typename U::value_type tmp{};
			if (readValue(v, tmp))
			{
				out = std::move(tmp);
				return true;
			}
			out.reset();
			return false;
		}
		else if constexpr (is_string_map_v<U>)
		{
			const Value::Object* obj = v.asObject();
			if (!obj)
				return false;

			U tmp;
			for (const auto& [key, child] : *obj)
			{
				typename U::mapped_type mapped{};
				if (!readValue(child, mapped))
					return false; // rollback: out is untouched
				tmp.emplace(key, std::move(mapped));
			}
			out = std::move(tmp);
			return true;
		}
		else if constexpr (is_vector_v<U>)
		{
			const Value::Array* arr = v.asArray();
			if (!arr)
				return false;

			U tmp;
			tmp.reserve(arr->size());
			for (const Value& e : *arr)
			{
				typename U::value_type elem{};
				if (!readValue(e, elem))
					return false; // rollback: out is untouched
				tmp.push_back(std::move(elem));
			}
			out = std::move(tmp);
			return true;
		}
		else if constexpr (has_serialize_v<U>)
		{
			if (v.type() != Value::Type::Object)
				return false;

			Archive ar(Archive::Mode::Load, &v);
			callSerialize(ar, out);
			return true; // best-effort: per-field tolerance is inside member()
		}
		else
		{
			static_assert(sizeof(T) == 0, "lain::data::fromValue: T needs a serialize(Archive&, T&) or a supported std type");
			return false;
		}
	}
} // namespace lain::data::detail
