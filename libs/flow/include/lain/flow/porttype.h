#pragma once

#include "lain/flow/portvalue.h"

#include <lain/meta/tostring.h>	 // meta::toString — the value->string bridge
#include <lain/meta/typenames.h> // meta::typeName — the human type name

#include <cstddef>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace lain::flow
{
	// The reflective descriptor for a port's declared type T — a flyweight: one static
	// instance per T, referenced by every Port of that type. The reflective facts about
	// a type (its identity, its human name, how to render a value of it) are per-type,
	// not per-port, so they live here once instead of being copied into each Port. A
	// Port holds a single `const PortType*`.
	//
	// This is the extension point for per-type facilities: a future value viewer or
	// serializer becomes another field here, not another functor on every Port.
	struct PortType
	{
		std::type_index index;					   // runtime identity — what connect() type-checks
		std::string_view name;					   // human type name (meta::typeName<T>)
		std::string (*describe)(const PortValue&); // current value as text (bridges meta::toString)

		// --- the COLLECTION capability (ADR-0014) --------------------------------------------
		// Filled only when T is a collection (a std::vector); left null for every other type, so
		// `element != nullptr` IS the question "can this be mapped over?" — there is no separate
		// flag and no registry of mappable types to keep in step.
		//
		// It exists so a MAP NODE can split one value across N child evaluations and gather their
		// results, without flow core naming a payload type: exactly the bridge `describe` already
		// is, instantiated per T in the TU that declares the port.
		//
		// `at` ALIASES — element i shares the collection's own allocation, so splitting costs a
		// refcount bump per element and no payload copy. `gather` cannot do the same, because a
		// vector owns its elements: it copies one element per entry, which ADR-0014 accepts and
		// records as the cost of a natural std::vector payload.
		const PortType* element = nullptr;							  // the element type's own descriptor
		std::size_t (*size)(const PortValue&) = nullptr;			  // how many elements (empty slot -> 0)
		PortValue (*at)(const PortValue&, std::size_t) = nullptr;	  // element i, aliased; out of range -> empty
		PortValue (*gather)(const std::vector<PortValue>&) = nullptr; // N elements -> one collection

		// Whether a value of this type can be mapped over — one question asked in one place,
		// rather than each caller testing which of the four fields it happens to need.
		bool isCollection() const { return element != nullptr; }
	};

	// The single shared PortType for T (a function-local static: built once, stable
	// address, merged across TUs). Captured into a Port at addInput/addOutput<T>.
	template <typename T>
	const PortType& portType();
} // namespace lain::flow

#include "lain/flow/details/porttype.inl"
