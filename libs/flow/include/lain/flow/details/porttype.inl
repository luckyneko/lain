#pragma once

// Template definitions for lain::flow's PortType flyweight (see porttype.h): the
// per-type describe bridge and the shared-descriptor accessor.

namespace lain::flow
{
	namespace detail
	{
		// The type-erasure bridge for PortType::describe: recover the T from the port's
		// slot and render it via meta::toString. Empty slot -> "(empty)". Instantiated per
		// T in the TU that declares the port, so flow never names the concrete type (a GPU
		// handle with no toString falls back to its type name inside meta::toString).
		template <typename T>
		std::string describePortValue(const PortValue& value)
		{
			if (value.empty())
				return "(empty)";
			return lain::meta::toString(value.get<T>());
		}
	} // namespace detail

	template <typename T>
	const PortType& portType()
	{
		static const PortType info{
			std::type_index(typeid(T)),
			lain::meta::typeName<T>(),
			&detail::describePortValue<T>};
		return info;
	}
} // namespace lain::flow
