#pragma once

// ValueCodecs::registerType<T> — captures T in a captureless pair of function pointers that route
// through the lain::data reflection facade.
//
// Split from the header for READABILITY only. It used to claim the split kept data.h off
// non-registration sites; it does not, and never did — valuecodecs.h includes this unconditionally,
// as every .inl in the tree is included by its own header, so a split buys no include and no
// rebuild. What it does buy is a header that reads as an interface, which is the whole test
// (CLAUDE.md, *Where a body lives*).

#include <lain/data/data.h> // data::toValue / data::fromValue

#include <typeinfo>
#include <utility>

namespace lain::flow::serialize
{
	template <typename T>
	void ValueCodecs::registerType(std::string key)
	{
		ValueCodec codec;
		codec.typeKey = std::move(key);
		codec.toValue = [](const PortValue& value) -> data::Value
		{ return data::toValue(value.get<T>()); };
		codec.fromValue = [](const data::Value& value, PortValue& slot) -> bool
		{
			if (auto read = data::fromValue<T>(value))
			{
				slot.set<T>(std::move(*read));
				return true;
			}
			return false;
		};
		m_codecs[std::type_index(typeid(T))] = std::move(codec);
	}
} // namespace lain::flow::serialize
