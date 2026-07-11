#include "lain/flow/serialize/valuecodecs.h"

namespace lain::flow::serialize
{
	const ValueCodec* ValueCodecs::find(std::type_index type) const
	{
		const auto it = m_codecs.find(type);
		return it == m_codecs.end() ? nullptr : &it->second;
	}

	std::optional<data::Value> paramToValue(const Param& param, const ValueCodecs& codecs)
	{
		const ValueCodec* codec = codecs.find(param.type());
		if (!codec)
			return std::nullopt;

		data::Value out = data::Value::object();
		out.set("name", data::Value(param.name()));
		out.set("type", data::Value(codec->typeKey));
		out.set("value", codec->toValue(param.value()));
		return out;
	}

	bool paramFromValue(Param& param, const data::Value& stored, const ValueCodecs& codecs)
	{
		const ValueCodec* codec = codecs.find(param.type());
		if (!codec)
			return false;

		const data::Value* value = stored.find("value");
		if (!value)
			return false;

		return codec->fromValue(*value, param.value());
	}
} // namespace lain::flow::serialize
