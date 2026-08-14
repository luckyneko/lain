#pragma once

// The registry of addable PORT TYPES for dynamic ports (M4 vertical b) — a dynamic node's "+" menu
// is its keys. lain's Factory idiom applied to port types: registerPortType<T>("Image") stores a
// creator that calls node.addDynamicPort<T>(name), so flow core names no payload type — the app
// registers image::Image / a future Voxel, exactly as it registers codecs. A service-shaped seam
// over a hidden process-wide registry.

#include "lain/flow/dynamicports.h"
#include "lain/flow/porttype.h"
#include "lain/flow/types.h"

#include <lain/meta/traits.h> // is_vector_v / vector_element_t — a registered collection records its element

#include <functional>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <vector>

namespace lain::flow
{
	// Adds a pin of a registered type to a dynamic node; returns its PortId.
	using PortTypeCreator = std::function<PortId(DynamicPortsNode&, std::string name)>;

	// Register an addable port type under `key` with an explicit creator (the general form; a
	// re-register replaces). `type` is the value type the creator adds — recorded so a serializer
	// can reverse-look-up the key from a live pin (portTypeKey). The typed form below is the common
	// way in and fills both `type` and `creator` from T.
	void registerPortType(std::string key, std::type_index type, PortTypeCreator creator);

	// Record that `list` is the collection type whose elements are `element` — the reverse direction
	// of PortType::element. Called for you by the typed registerPortType<T> below when T is a
	// collection; exposed because the general registerPortType form cannot deduce it.
	void registerListType(std::type_index element, const PortType* list);

	// The registered collection type whose elements are `element`, or nullptr if no list form of it
	// has been registered. This is what lets a MAP lift an inner pin of type T to an outer port of
	// type vector<T> (ADR-0014): a PortType knows its own element type, but nothing can walk that
	// backwards, because there is no way to name std::vector<T> from a runtime type_index alone.
	//
	// So a type is mappable exactly when its list form is registered — which an app must do anyway
	// for a collection pin to serialize (a dynamic pin is replayed through its port-type key).
	const PortType* listTypeFor(std::type_index element);

	// Register an addable port type under `key` for a value type T (the common form): the creator
	// calls node.addDynamicPort<T>(name), and T's type_index is recorded for portTypeKey. The app
	// calls e.g. registerPortType<image::Image>("Image") once at startup.
	//
	// Registering a COLLECTION (registerPortType<std::vector<image::Image>>("ListOfImage")) also
	// records it as the list form of its element type, which is what makes that element mappable.
	template <typename T>
	void registerPortType(std::string key)
	{
		registerPortType(std::move(key), std::type_index(typeid(T)),
						 [](DynamicPortsNode& node, std::string name)
						 { return node.addDynamicPort<T>(std::move(name)); });
		if constexpr (lain::meta::is_vector_v<T>)
			registerListType(std::type_index(typeid(lain::meta::vector_element_t<T>)), &portType<T>());
	}

	// The registered key for a value type, or empty if `type` was never registered. The reverse of
	// addPortOfType: lets a serializer label a live dynamic pin (Port::type() is a type_index) by its
	// stable port-type key on save.
	std::string portTypeKey(std::type_index type);

	// The registered keys, alphabetical (for a stable "+" menu).
	std::vector<std::string> portTypeKeys();

	// Whether `key` is registered.
	bool portTypeRegistered(const std::string& key);

	// Add a pin of the registered type `key` to `node`; returns its PortId, or a null PortId if
	// `key` is not registered.
	PortId addPortOfType(DynamicPortsNode& node, const std::string& key, std::string name);
} // namespace lain::flow
