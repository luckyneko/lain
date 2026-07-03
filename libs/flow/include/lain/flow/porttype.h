#pragma once

#include "lain/flow/portvalue.h"

#include <lain/meta/tostring.h>	 // meta::toString — the value->string bridge
#include <lain/meta/typenames.h> // meta::typeName — the human type name

#include <string>
#include <string_view>
#include <typeindex>

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
	};

	// The single shared PortType for T (a function-local static: built once, stable
	// address, merged across TUs). Captured into a Port at addInput/addOutput<T>.
	template <typename T>
	const PortType& portType();
} // namespace lain::flow

#include "lain/flow/details/porttype.inl"
