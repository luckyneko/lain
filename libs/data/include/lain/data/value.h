#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// lain::data::Value — the format-neutral serialization DOM (see CONTEXT.md "The `data`
// library"). A recursive variant that every codec and transport pivots through: reflection
// maps a C++ type to/from a Value (toValue / fromValue, data.h), a codec turns a Value to/from
// bytes (lain::io::data, later). Value names no format — Base64, JSON syntax, etc. live in the
// codec, never here.
//
// Numbers are the WIDENED-CANONICAL set (Int64 / UInt64 / Double): every narrow C++ int/float
// rides one losslessly, and the narrowing happens in the typed read, not the DOM. Object is
// INSERTION-ORDERED (a vector, not a map) so output is deterministic and diff-clean.
//
// v1 scope: Null / Bool / Int / UInt / Double / String / Array / Object. Deferred (next slice):
// a Bytes arm (memory::Buffer, Base64 at the text-codec boundary).
namespace lain::data
{
	class Value
	{
	public:
		// The DOM arms, in variant-index order (type() maps straight to this).
		enum class Type
		{
			Null,
			Bool,
			Int,	// signed, stored as int64
			UInt,	// unsigned, stored as uint64
			Double,
			String,
			Array,
			Object,
		};

		using Array = std::vector<Value>;
		// Object is an insertion-ordered list of key/value pairs (NOT a map): deterministic,
		// diff-clean output. As a std::vector it may legally hold the still-incomplete Value
		// here without a heap indirection (pair is only instantiated later, where Value is
		// complete). Access an entry as .first (key) / .second (value).
		using Object = std::vector<std::pair<std::string, Value>>;

		Value() noexcept = default;			   // Null (monostate)
		Value(std::nullptr_t) noexcept {}	   // Null
		Value(bool b) : m_data(b) {}		   //
		Value(const char* s) : m_data(std::string(s)) {}
		Value(std::string s) : m_data(std::move(s)) {}
		Value(Array a) : m_data(std::move(a)) {}
		Value(Object o) : m_data(std::move(o)) {}

		// Any integral (bar bool) → Int/UInt by signedness; any floating → Double. One pair of
		// templated ctors so Value(42) / Value(id.value()) / Value(3.5f) all land losslessly.
		template <typename I, std::enable_if_t<std::is_integral_v<I> && !std::is_same_v<I, bool>, int> = 0>
		Value(I v)
		{
			if constexpr (std::is_signed_v<I>)
				m_data = static_cast<std::int64_t>(v);
			else
				m_data = static_cast<std::uint64_t>(v);
		}

		template <typename F, std::enable_if_t<std::is_floating_point_v<F>, int> = 0>
		Value(F v)
			: m_data(static_cast<double>(v))
		{
		}

		static Value array() { return Value(Array{}); }
		static Value object() { return Value(Object{}); }

		Type type() const noexcept;
		std::string_view typeName() const noexcept; // for messages/logs

		bool isNull() const noexcept { return type() == Type::Null; }
		bool isBool() const noexcept { return type() == Type::Bool; }
		bool isString() const noexcept { return type() == Type::String; }
		bool isArray() const noexcept { return type() == Type::Array; }
		bool isObject() const noexcept { return type() == Type::Object; }
		bool isNumber() const noexcept
		{
			const Type t = type();
			return t == Type::Int || t == Type::UInt || t == Type::Double;
		}

		// Scalar extraction — PURE: a type mismatch returns nullopt/nullptr and touches nothing
		// (what makes a failed fromValue rollback-safe). asDouble also succeeds for Int/UInt (an
		// integer is a valid double); asInt64/asUInt64 are exact-arm only — the cross-int
		// widening + range check lives in the typed read (details/reflect.h).
		std::optional<bool> asBool() const;
		std::optional<std::int64_t> asInt64() const;
		std::optional<std::uint64_t> asUInt64() const;
		std::optional<double> asDouble() const;
		const std::string* asString() const;

		const Array* asArray() const;
		Array* asArray();
		const Object* asObject() const;
		Object* asObject();

		// Object upsert, insertion-ordered: overwrites an existing key in place, else appends.
		// Makes this Value an Object first if it is not one. Returns the stored value.
		Value& set(std::string key, Value value);
		// Object lookup by key, or nullptr (also nullptr if this is not an Object).
		const Value* find(std::string_view key) const;

		// Array append; makes this Value an Array first if it is not one. Returns the appended value.
		Value& push(Value value);

		bool operator==(const Value& other) const { return m_data == other.m_data; }
		bool operator!=(const Value& other) const { return !(*this == other); }

	private:
		std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, Array, Object> m_data;
	};
} // namespace lain::data
