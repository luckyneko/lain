#include "lain/flow/porttyperegistry.h"

#include <algorithm>
#include <map>
#include <typeindex>
#include <utility>

namespace lain::flow
{
	// The hidden process-wide registry. Ordered, so portTypeKeys() is stable/alphabetical.
	static std::map<std::string, PortTypeCreator>& registry()
	{
		static std::map<std::string, PortTypeCreator> instance;
		return instance;
	}

	// The reverse table: a registered value type's type_index -> its key. Populated only by the
	// typed registerPortType<T> form, so a serializer can name a live pin's type on save.
	static std::map<std::type_index, std::string>& keyByType()
	{
		static std::map<std::type_index, std::string> instance;
		return instance;
	}

	// element type_index -> the registered collection type holding it. The reverse of
	// PortType::element, which cannot be derived: naming std::vector<T> needs T at compile time, and
	// a mirroring pass has only a runtime type. See listTypeFor.
	static std::map<std::type_index, const PortType*>& listByElement()
	{
		static std::map<std::type_index, const PortType*> instance;
		return instance;
	}

	// key -> the registered type's flyweight. The reverse of keyByType, and what turns a document's
	// stored payload type (a string) back into a type (ADR-0022).
	static std::map<std::string, const PortType*>& typeByKey()
	{
		static std::map<std::string, const PortType*> instance;
		return instance;
	}

	void registerPortType(std::string key, const PortType& type, PortTypeCreator creator)
	{
		keyByType()[type.index] = key; // record both reverses before key is moved into the forward table
		typeByKey()[key] = &type;
		registry()[std::move(key)] = std::move(creator);
	}

	void registerListType(std::type_index element, const PortType* list)
	{
		if (list != nullptr)
			listByElement()[element] = list;
	}

	const PortType* listTypeFor(std::type_index element)
	{
		const auto it = listByElement().find(element);
		return it == listByElement().end() ? nullptr : it->second;
	}

	std::string portTypeKey(std::type_index type)
	{
		const auto it = keyByType().find(type);
		return it == keyByType().end() ? std::string{} : it->second;
	}

	const PortType* portTypeFor(const std::string& key)
	{
		const auto it = typeByKey().find(key);
		return it == typeByKey().end() ? nullptr : it->second;
	}

	std::vector<std::string> portTypeKeys()
	{
		std::vector<std::string> keys;
		keys.reserve(registry().size());
		for (const auto& entry : registry())
			keys.push_back(entry.first);
		return keys;
	}

	bool portTypeRegistered(const std::string& key) { return registry().count(key) != 0; }

	PortId addPortOfType(DynamicPortsNode& node, const std::string& key, std::string name)
	{
		const auto it = registry().find(key);
		if (it == registry().end())
			return PortId{};
		return it->second(node, std::move(name));
	}

	// --- value conversions -------------------------------------------------------------------
	// {from, to} -> how. Ordered by the type_index pair, which is stable within a run but says
	// nothing about the KEYS, so the two enumerating functions sort by key before answering — a
	// menu whose order changed between runs would be its own small bug.
	static std::map<std::pair<std::type_index, std::type_index>, ValueConversion>& conversions()
	{
		static std::map<std::pair<std::type_index, std::type_index>, ValueConversion> instance;
		return instance;
	}

	// from -> to, remembered as the flyweights so a caller can be handed the TYPE rather than an
	// identity it would have to look back up.
	static std::map<std::pair<std::type_index, std::type_index>, std::pair<const PortType*, const PortType*>>& conversionTypes()
	{
		static std::map<std::pair<std::type_index, std::type_index>, std::pair<const PortType*, const PortType*>> instance;
		return instance;
	}

	void registerConversion(const PortType& from, const PortType& to, ValueConversion conversion)
	{
		// A conversion from a type to itself is not one — convertValue answers such a request from
		// the value it was given, and offering it would put an identity Cast in every menu.
		if (from.index == to.index || !conversion)
			return;

		const std::pair<std::type_index, std::type_index> pair{from.index, to.index};
		conversions()[pair] = std::move(conversion);
		conversionTypes()[pair] = {&from, &to};
	}

	bool conversionRegistered(std::type_index from, std::type_index to)
	{
		return conversions().count({from, to}) != 0;
	}

	PortValue convertValue(const PortValue& value, const PortType& to)
	{
		if (value.empty())
			return {};
		if (value.type() == to.index)
			return value; // already the wanted type; nothing to convert and nothing to lose

		const auto it = conversions().find({value.type(), to.index});
		return it == conversions().end() ? PortValue{} : it->second(value);
	}

	// Sort a list of types by their registered key, so every menu built from this registry lists
	// them in the same order the port-type menus use. An unregistered type sorts last under its
	// empty key, which cannot happen today (a conversion is registered against real types) but
	// keeps the comparison total.
	static void sortByKey(std::vector<const PortType*>& types)
	{
		std::sort(types.begin(), types.end(),
				  [](const PortType* lhs, const PortType* rhs)
				  { return portTypeKey(lhs->index) < portTypeKey(rhs->index); });
	}

	std::vector<const PortType*> conversionsFrom(std::type_index from)
	{
		std::vector<const PortType*> targets;
		for (const auto& entry : conversionTypes())
		{
			if (entry.first.first == from)
				targets.push_back(entry.second.second);
		}
		sortByKey(targets);
		return targets;
	}

	std::vector<std::pair<const PortType*, const PortType*>> registeredConversions()
	{
		std::vector<std::pair<const PortType*, const PortType*>> pairs;
		pairs.reserve(conversionTypes().size());
		for (const auto& entry : conversionTypes())
			pairs.push_back(entry.second);

		std::sort(pairs.begin(), pairs.end(),
				  [](const auto& lhs, const auto& rhs)
				  {
					  const std::string lf = portTypeKey(lhs.first->index);
					  const std::string rf = portTypeKey(rhs.first->index);
					  if (lf != rf)
						  return lf < rf;
					  return portTypeKey(lhs.second->index) < portTypeKey(rhs.second->index);
				  });
		return pairs;
	}
} // namespace lain::flow
