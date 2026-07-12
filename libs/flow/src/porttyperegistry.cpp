#include "lain/flow/porttyperegistry.h"

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

	void registerPortType(std::string key, std::type_index type, PortTypeCreator creator)
	{
		keyByType()[type] = key; // record the reverse before key is moved into the forward table
		registry()[std::move(key)] = std::move(creator);
	}

	std::string portTypeKey(std::type_index type)
	{
		const auto it = keyByType().find(type);
		return it == keyByType().end() ? std::string{} : it->second;
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
} // namespace lain::flow
