#include "lain/flow/porttyperegistry.h"

#include <map>
#include <utility>

namespace lain::flow
{
	// The hidden process-wide registry. Ordered, so portTypeKeys() is stable/alphabetical.
	static std::map<std::string, PortTypeCreator>& registry()
	{
		static std::map<std::string, PortTypeCreator> instance;
		return instance;
	}

	void registerPortType(std::string key, PortTypeCreator creator)
	{
		registry()[std::move(key)] = std::move(creator);
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
