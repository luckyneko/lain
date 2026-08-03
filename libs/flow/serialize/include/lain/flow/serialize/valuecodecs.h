#pragma once

#include <lain/data/value.h>
#include <lain/flow/param.h>
#include <lain/flow/portvalue.h>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <typeindex>

// lain::flow::serialize — Graph <-> data::Value (WORK.md Tier A #1). A separate target over flow's
// public API + lain::data, kept OUT of flow core by the boundary rule: it names lain::data and the
// concrete payload types the app registers. Peer in spirit to flow::edit, out-of-core for the dep.
//
// This header holds the value-serializer registry — the piece a Param (and any future savable
// PortValue) round-trips through.
namespace lain::flow::serialize
{
	// How one C++ value type maps to/from a data::Value, plus its stable string key. A captureless
	// pair of function pointers (built by ValueCodecs::registerType<T>). This is the serialization
	// analogue of PortType's reflective facts — deliberately NOT a field on PortType, because that
	// lives in flow core, which names no lain::data.
	struct ValueCodec
	{
		std::string typeKey;							   // stable key, e.g. "float" / "color"
		data::Value (*toValue)(const PortValue&);		   // PortValue -> Value
		bool (*fromValue)(const data::Value&, PortValue&); // Value -> PortValue (into the slot)
	};

	// The app-populated value-serializer registry: a table keyed by a PortValue's runtime
	// type_index. flow core can't own it (it names data), and only the app knows its param types
	// (image::ColorRGBf, a Choice, ...), so it lives here and the app fills it once —
	// registerType<float>("float"), registerType<image::ColorRGBf>("color"), ...
	class ValueCodecs
	{
	public:
		// Register T under `key`. The codec (de)serialises via data::toValue / data::fromValue<T>.
		// A re-register replaces.
		template <typename T>
		void registerType(std::string key);

		// The codec for a declared type, or nullptr if T was never registered.
		const ValueCodec* find(std::type_index type) const;
		bool contains(std::type_index type) const { return find(type) != nullptr; }
		std::size_t size() const { return m_codecs.size(); }

	private:
		std::map<std::type_index, ValueCodec> m_codecs;
	};

	// Serialize one Param to { "name", "type", "value" }, or nullopt if its declared type has no
	// registered codec (the caller records that as a load/save issue). The stored "type" is the
	// codec key — a cross-check; the param's DECLARED type is authoritative on read.
	[[nodiscard]] std::optional<data::Value> paramToValue(const Param& param, const ValueCodecs& codecs);

	// Decode a stored param Value's "value" AS THE PARAM'S DECLARED TYPE ("the type is the schema"),
	// returning it as a detached PortValue — or nullopt if that type has no codec, the "value" key is
	// absent, or the value doesn't read cleanly.
	//
	// It hands back a value rather than writing into the Param because a param is recipe: committing
	// one is Node::setParam's job, so that the write and the node's invalidation stay a single
	// operation. Symmetric with paramToValue, which likewise takes a const Param.
	[[nodiscard]] std::optional<PortValue> paramFromValue(const Param& param, const data::Value& stored,
														  const ValueCodecs& codecs);
} // namespace lain::flow::serialize

#include "lain/flow/serialize/details/valuecodecs.inl"
