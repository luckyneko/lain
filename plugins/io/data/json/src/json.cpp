#include "lain/io/data/json/register.h"

#include <lain/data/value.h>
#include <lain/io/data/load.h> // readerRegistry
#include <lain/io/data/reader.h>
#include <lain/io/data/save.h> // writerRegistry
#include <lain/io/data/writer.h>
#include <lain/memory/buffer.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace lain::io::data::json
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// ordered_json (not plain json) preserves object key INSERTION order on both parse and dump,
	// matching Value::Object — the property behind deterministic, diff-clean, idempotent files.
	using Json = nlohmann::ordered_json;

	// Encode raw bytes as a Base64 string — JSON has no byte type, so a Bytes node rides as a
	// string ("Base64 is a per-format encoding"). Read back it is a plain String: JSON cannot
	// distinguish it from any other string, so a lossless byte round-trip wants a binary codec.
	static std::string base64Encode(const std::vector<std::byte>& bytes)
	{
		static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		auto sextet = [](const std::byte& b)
		{ return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(b)); };

		std::string out;
		out.reserve(((bytes.size() + 2) / 3) * 4);
		std::size_t i = 0;
		for (; i + 3 <= bytes.size(); i += 3)
		{
			const std::uint32_t n = (sextet(bytes[i]) << 16) | (sextet(bytes[i + 1]) << 8) | sextet(bytes[i + 2]);
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += table[(n >> 6) & 63];
			out += table[n & 63];
		}
		if (const std::size_t rem = bytes.size() - i; rem == 1)
		{
			const std::uint32_t n = sextet(bytes[i]) << 16;
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += "==";
		}
		else if (rem == 2)
		{
			const std::uint32_t n = (sextet(bytes[i]) << 16) | (sextet(bytes[i + 1]) << 8);
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += table[(n >> 6) & 63];
			out += '=';
		}
		return out;
	}

	// Value -> ordered_json. Distinct number arms keep ints and doubles apart in the output
	// (Int/UInt -> "4", Double -> "4.0"), so nothing degrades to a single JSON number kind.
	static Json toJson(const lain::data::Value& value)
	{
		using Type = lain::data::Value::Type;
		switch (value.type())
		{
			case Type::Null: return nullptr;
			case Type::Bool: return *value.asBool();
			case Type::Int: return *value.asInt64();
			case Type::UInt: return *value.asUInt64();
			case Type::Double: return *value.asDouble();
			case Type::String: return *value.asString();
			case Type::Bytes: return base64Encode(*value.asBytes());
			case Type::Array:
			{
				Json out = Json::array();
				for (const auto& element : *value.asArray())
					out.push_back(toJson(element));
				return out;
			}
			case Type::Object:
			{
				Json out = Json::object();
				for (const auto& [key, val] : *value.asObject())
					out[key] = toJson(val);
				return out;
			}
		}
		return nullptr; // unreachable
	}

	// ordered_json -> Value. JSON has no signed/unsigned distinction, so nlohmann classifies a
	// non-negative integer as unsigned: a positive Int normalises to UInt on read. Harmless — the
	// typed read (fromValue<int>) cross-accepts, and the text is stable — but why *back != v at the
	// DOM level for a positive Int.
	static lain::data::Value fromJson(const Json& j)
	{
		if (j.is_null())
			return lain::data::Value();
		if (j.is_boolean())
			return lain::data::Value(j.get<bool>());
		if (j.is_number_unsigned())
			return lain::data::Value(j.get<std::uint64_t>());
		if (j.is_number_integer())
			return lain::data::Value(j.get<std::int64_t>());
		if (j.is_number_float())
			return lain::data::Value(j.get<double>());
		if (j.is_string())
			return lain::data::Value(j.get<std::string>());
		if (j.is_array())
		{
			lain::data::Value out = lain::data::Value::array();
			for (const auto& element : j)
				out.push(fromJson(element));
			return out;
		}
		if (j.is_object())
		{
			lain::data::Value out = lain::data::Value::object();
			for (auto it = j.begin(); it != j.end(); ++it)
				out.set(it.key(), fromJson(it.value()));
			return out;
		}
		return lain::data::Value();
	}

	class JsonReader : public DataReader
	{
	public:
		std::optional<lain::data::Value> decode(const memory::Buffer& bytes) const override
		{
			const char* first = reinterpret_cast<const char*>(bytes.data());
			const char* last = first + bytes.size();
			// allow_exceptions = false -> a parse error yields a discarded value, not a throw.
			const Json j = Json::parse(first, last, nullptr, false);
			if (j.is_discarded())
				return std::nullopt;
			return fromJson(j);
		}
	};

	class JsonWriter : public DataWriter
	{
	public:
		std::optional<memory::Buffer> encode(const lain::data::Value& value) const override
		{
			const std::string text = toJson(value).dump(2); // 2-space indent: readable + stable
			memory::Buffer buffer(text.size());
			if (!text.empty())
				std::memcpy(buffer.data(), text.data(), text.size());
			return buffer;
		}
	};

	void registerCodec()
	{
		readerRegistry().registerType<JsonReader>("json");
		writerRegistry().registerType<JsonWriter>("json");
	}
} // namespace lain::io::data::json
