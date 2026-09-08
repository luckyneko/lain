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
#include <optional>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace lain::flow
{
	// Adds a pin of a registered type to a dynamic node; returns its PortId.
	using PortTypeCreator = std::function<PortId(DynamicPortsNode&, std::string name)>;

	// Register an addable port type under `key` with an explicit creator (the general form; a
	// re-register replaces). `type` is the value type's flyweight — recorded in BOTH directions, so
	// a serializer can name a live pin's type on save (portTypeKey) and a loader can resolve a
	// stored key back to the type itself (portTypeFor). The typed form below is the common way in
	// and fills both from T.
	//
	// It takes the PortType rather than a bare type_index because the two would otherwise be a fact
	// stated twice — `type.index` IS the identity, so they cannot disagree.
	void registerPortType(std::string key, const PortType& type, PortTypeCreator creator);

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
		registerPortType(std::move(key), portType<T>(),
						 [](DynamicPortsNode& node, std::string name)
						 { return node.addDynamicPort<T>(std::move(name)); });
		if constexpr (lain::meta::is_vector_v<T>)
			registerListType(std::type_index(typeid(lain::meta::vector_element_t<T>)), &portType<T>());
	}

	// The registered key for a value type, or empty if `type` was never registered. The reverse of
	// addPortOfType: lets a serializer label a live dynamic pin (Port::type() is a type_index) by its
	// stable port-type key on save.
	std::string portTypeKey(std::type_index type);

	// The registered type for a key, or nullptr if `key` was never registered — the reverse of
	// portTypeKey. What a LOADER resolves a document's stored payload type through, and what a host
	// turns a dropdown selection into (ADR-0022).
	const PortType* portTypeFor(const std::string& key);

	// The registered keys, alphabetical (for a stable "+" menu).
	std::vector<std::string> portTypeKeys();

	// Whether `key` is registered.
	bool portTypeRegistered(const std::string& key);

	// Add a pin of the registered type `key` to `node`; returns its PortId, or a null PortId if
	// `key` is not registered.
	PortId addPortOfType(DynamicPortsNode& node, const std::string& key, std::string name);

	// --- VALUE CONVERSIONS (ADR-0022) ---------------------------------------------------------
	// How a value of one payload type becomes a value of another. A registry rather than a PortType
	// field for the reason listTypeFor is one: a conversion is a relation BETWEEN two types, and
	// nothing can enumerate the second at the point a PortType for the first is built. The app fills
	// it at startup, exactly as it fills the port types — flow core names no payload type.
	//
	// It is read by the CAST NODE and by a host building its menus, and by nothing else. In
	// particular `connect` never consults it: an edge type-checks exactly, and a conversion is
	// something a graph asks for by having a node that does it (see ADR-0022's rejection of
	// implicit coercion, and ADR-0020's "no silent lossy conversion").

	// A conversion answers an EMPTY PortValue when it cannot convert this value — a parse that
	// failed, say. That is not a special error channel: an empty value is already how "produced
	// nothing" travels (ADR-0007), so a Cast whose conversion fails suppresses like any other node.
	using ValueConversion = std::function<PortValue(const PortValue&)>;

	// Register how a `from` value becomes a `to` value (a re-register replaces). The typed forms
	// below are the common way in.
	void registerConversion(const PortType& from, const PortType& to, ValueConversion conversion);

	// A conversion spelled as a plain function over the concrete types — From and To are DEDUCED
	// from the function, so the registration cannot name one pair and convert another. Return To for
	// a total conversion, std::optional<To> for one that can fail (an empty optional converts to an
	// empty value).
	template <typename From, typename To>
	void registerConversion(To (*convert)(const From&))
	{
		registerConversion(portType<From>(), portType<To>(),
						   [convert](const PortValue& value) -> PortValue
						   {
							   PortValue out;
							   if (value.holds<From>())
								   out.set<To>(convert(value.get<From>()));
							   return out;
						   });
	}

	template <typename From, typename To>
	void registerConversion(std::optional<To> (*convert)(const From&))
	{
		registerConversion(portType<From>(), portType<To>(),
						   [convert](const PortValue& value) -> PortValue
						   {
							   PortValue out;
							   if (!value.holds<From>())
								   return out;
							   if (std::optional<To> converted = convert(value.get<From>()))
								   out.set<To>(std::move(*converted));
							   return out;
						   });
	}

	// The static_cast conversion, for a pair the language already converts (int -> float). Spelled
	// out per pair rather than offered for every convertible pair, because WHICH conversions a graph
	// may perform is a decision the app makes — `float -> int` truncates, and that is a policy, not
	// a fact about the types.
	template <typename From, typename To>
	void registerConversion()
	{
		registerConversion<From, To>(+[](const From& value) -> To
									 { return static_cast<To>(value); });
	}

	// Whether a conversion from `from` to `to` is registered. `from == to` is NOT a conversion:
	// nothing needs converting, and reporting one would put an identity Cast in every menu.
	bool conversionRegistered(std::type_index from, std::type_index to);

	// Convert `value` to `to`, or an empty PortValue when there is no such conversion, the value is
	// empty, or the conversion could not convert it. One call for all three, because a Cast treats
	// them alike: it produced nothing.
	PortValue convertValue(const PortValue& value, const PortType& to);

	// The types `from` can be converted TO, alphabetically by registered key — what a host offers as
	// a Cast's target, and what generates the per-pair palette entries.
	std::vector<const PortType*> conversionsFrom(std::type_index from);

	// Every registered conversion as {from, to}, alphabetically by the pair's keys. The palette's
	// whole menu of casts, so the entries cannot drift from what the registry can actually do.
	std::vector<std::pair<const PortType*, const PortType*>> registeredConversions();
} // namespace lain::flow
