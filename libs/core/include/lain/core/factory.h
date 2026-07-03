#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lain::core
{
	// A registry that maps a name to a creator for some Base type, so callers can
	// construct objects by string key — a UI palette, a deserialiser keyed on a saved
	// type name, or a plugin registration point. Each creator captures whatever
	// construction context it needs (a device, config, ...), so the factory itself
	// stays context-agnostic and holds only Base.
	//
	// Keys are ordered, so keys() is stable / alphabetical — convenient for a menu.
	// create() returns nullptr for an unknown key rather than throwing.
	template <typename Base>
	class Factory
	{
	public:
		// Produces a fresh Base; construction context is captured at registration.
		using Creator = std::function<std::unique_ptr<Base>()>;

		// Register `creator` under `key`, replacing any existing entry. Returns true
		// if this added a new key, false if it replaced an existing creator.
		bool registerType(std::string key, Creator creator);

		// Convenience: register a creator that constructs T (deriving from Base) from
		// `args`, captured by copy — so args must be copyable, and each create() builds
		// a fresh T from them. Removes the make_unique boilerplate and guarantees the
		// creator builds T. The key stays an explicit, stable string — deliberately not
		// derived from the type name (see WORK.md on a future type-owned key).
		template <typename T, typename... Args>
		bool registerType(std::string key, Args&&... args);

		// Create the object registered under `key`, or nullptr if none is.
		std::unique_ptr<Base> create(const std::string& key) const;

		// Whether a creator is registered under `key`.
		bool contains(const std::string& key) const;

		// Number of registered creators.
		std::size_t size() const;

		// The registered keys, alphabetical — for building a palette / menu.
		std::vector<std::string> keys() const;

	private:
		std::map<std::string, Creator> m_creators;
	};
} // namespace lain::core

#include "lain/core/details/factory.inl"
