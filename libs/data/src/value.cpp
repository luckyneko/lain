#include "lain/data/value.h"

namespace lain::data
{
	Value::Type Value::type() const noexcept
	{
		// Alternatives are declared in Type order, so the variant index IS the Type.
		return static_cast<Type>(m_data.index());
	}

	std::string_view Value::typeName() const noexcept
	{
		switch (type())
		{
			case Type::Null: return "null";
			case Type::Bool: return "bool";
			case Type::Int: return "int";
			case Type::UInt: return "uint";
			case Type::Double: return "double";
			case Type::String: return "string";
			case Type::Bytes: return "bytes";
			case Type::Array: return "array";
			case Type::Object: return "object";
		}
		return "?";
	}

	std::optional<bool> Value::asBool() const
	{
		if (const bool* p = std::get_if<bool>(&m_data))
			return *p;
		return std::nullopt;
	}

	std::optional<std::int64_t> Value::asInt64() const
	{
		if (const std::int64_t* p = std::get_if<std::int64_t>(&m_data))
			return *p;
		return std::nullopt;
	}

	std::optional<std::uint64_t> Value::asUInt64() const
	{
		if (const std::uint64_t* p = std::get_if<std::uint64_t>(&m_data))
			return *p;
		return std::nullopt;
	}

	std::optional<double> Value::asDouble() const
	{
		// A double reads back as a double; an Int/UInt is a valid double too (the widening a
		// fromValue<float> wants). Int/UInt do NOT read back through asInt64/asUInt64's arms —
		// those stay exact so the typed integer read can range-check deliberately.
		if (const double* d = std::get_if<double>(&m_data))
			return *d;
		if (const std::int64_t* i = std::get_if<std::int64_t>(&m_data))
			return static_cast<double>(*i);
		if (const std::uint64_t* u = std::get_if<std::uint64_t>(&m_data))
			return static_cast<double>(*u);
		return std::nullopt;
	}

	const std::string* Value::asString() const
	{
		return std::get_if<std::string>(&m_data);
	}

	const std::vector<std::byte>* Value::asBytes() const
	{
		return std::get_if<std::vector<std::byte>>(&m_data);
	}

	const Value::Array* Value::asArray() const
	{
		return std::get_if<Array>(&m_data);
	}

	Value::Array* Value::asArray()
	{
		return std::get_if<Array>(&m_data);
	}

	const Value::Object* Value::asObject() const
	{
		return std::get_if<Object>(&m_data);
	}

	Value::Object* Value::asObject()
	{
		return std::get_if<Object>(&m_data);
	}

	Value& Value::set(std::string key, Value value)
	{
		if (!std::holds_alternative<Object>(m_data))
			m_data = Object{};

		Object& obj = std::get<Object>(m_data);
		for (auto& entry : obj)
		{
			if (entry.first == key)
			{
				entry.second = std::move(value);
				return entry.second;
			}
		}

		obj.emplace_back(std::move(key), std::move(value));
		return obj.back().second;
	}

	const Value* Value::find(std::string_view key) const
	{
		const Object* obj = std::get_if<Object>(&m_data);
		if (!obj)
			return nullptr;

		for (const auto& entry : *obj)
		{
			if (entry.first == key)
				return &entry.second;
		}
		return nullptr;
	}

	Value& Value::push(Value value)
	{
		if (!std::holds_alternative<Array>(m_data))
			m_data = Array{};

		Array& arr = std::get<Array>(m_data);
		arr.push_back(std::move(value));
		return arr.back();
	}
} // namespace lain::data
