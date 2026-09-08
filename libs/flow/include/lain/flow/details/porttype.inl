#pragma once

// Template definitions for lain::flow's PortType flyweight (see porttype.h): the
// per-type describe bridge, the per-type collection bridge, and the shared-descriptor
// accessor.

#include <lain/meta/traits.h> // is_vector_v / vector_element_t — what makes a type a collection

#include <string>
#include <type_traits>
#include <utility>

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

		// PortType::defaultValue for T: a slot holding a default-constructed T. Instantiated only
		// when T is default-constructible — a type that is not simply has no default, and the
		// payload-type mechanism reports a reset it cannot perform rather than inventing a value.
		template <typename T>
		PortValue defaultPortValue()
		{
			PortValue value;
			value.set<T>(T{});
			return value;
		}

		// PortType::compare for a less-comparable T: -1 / 0 / +1, spelled with `<` alone so a type
		// that defines only that one operator is still orderable.
		//
		// An empty slot on either side answers 0. That is not "equal": a comparison against a value
		// that was never produced has no answer, and the only caller (a Compare node) checks both
		// inputs for emptiness before asking — an empty input suppresses it, as it does any node.
		// Answering 0 rather than asserting keeps this a total function, as `at` and `size` are.
		template <typename T>
		int comparePortValues(const PortValue& lhs, const PortValue& rhs)
		{
			if (lhs.empty() || rhs.empty())
				return 0;
			const T& a = lhs.get<T>();
			const T& b = rhs.get<T>();
			if (a < b)
				return -1;
			return (b < a) ? 1 : 0;
		}

		// PortType::describe for a collection T. meta::toString has no arm for a vector, so it would
		// fall back to the bare type name — which says nothing a pin's type label does not already
		// say, and would be what the Inspector, the pin tooltips and the cli dump showed for EVERY
		// port a map has. The COUNT is the fact worth reporting, and it is the one a reader of a
		// collection actually wants.
		template <typename T>
		std::string describeCollection(const PortValue& value)
		{
			if (value.empty())
				return "(empty)";
			const std::size_t count = value.get<T>().size();
			return std::to_string(count) + (count == 1 ? " item" : " items");
		}

		// PortType::size for a collection T. An EMPTY slot has no elements rather than being an
		// error: absence already travels as an empty value (ADR-0007), and a map whose input was
		// suppressed upstream must read as "nothing to do", not throw inside a scheduler step.
		template <typename T>
		std::size_t collectionSize(const PortValue& value)
		{
			return value.empty() ? std::size_t{0} : value.get<T>().size();
		}

		// PortType::at for a collection T: element `index`, ALIASED into the collection's own
		// payload, so handing one element to a child costs no copy (ADR-0014).
		//
		// Out of range — or an empty slot — yields an empty PortValue rather than throwing, for
		// the same reason as above: the caller is a scheduler step, and an empty value is already
		// how "no value here" is expressed everywhere else in the engine.
		template <typename T>
		PortValue collectionAt(const PortValue& value, std::size_t index)
		{
			if (value.empty())
				return {};
			const T& items = value.get<T>();
			if (index >= items.size())
				return {};

			// Normally an alias into the vector's own storage — that is the point of the whole
			// capability. A PROXY container has no element to point at (std::vector<bool> packs
			// bits, so operator[] yields a value rather than a reference, and aliasing a temporary
			// would dangle), so it falls back to a copy. Cheap: a container is only ever a proxy
			// for something small.
			if constexpr (std::is_reference_v<decltype(items[index])>)
			{
				return PortValue::alias(value, items[index]);
			}
			else
			{
				PortValue copied;
				copied.set<lain::meta::vector_element_t<T>>(items[index]);
				return copied;
			}
		}

		// PortType::gather for a collection T: one collection built from N element slots. The one
		// direction that cannot alias — a vector owns its elements — so it copies per element.
		//
		// An empty or wrongly-typed element yields an EMPTY result. That is a mechanical failure
		// report, deliberately not a policy: what a hole MEANS (ADR-0014 has a map suppress its
		// whole output and name the offending element) is the map's decision, not this bridge's.
		template <typename T>
		PortValue gatherCollection(const std::vector<PortValue>& items)
		{
			using Element = lain::meta::vector_element_t<T>;
			T gathered;
			gathered.reserve(items.size());
			for (const PortValue& item : items)
			{
				if (!item.template holds<Element>()) // empty, or the wrong type
					return {};
				gathered.push_back(item.template get<Element>());
			}
			PortValue result;
			result.set(std::move(gathered));
			return result;
		}

		// The two OPTIONAL per-type bridges, selected at the one place a PortType is built. They are
		// functions rather than `if constexpr` inside the aggregate because a null field and a filled
		// one are the same expression here, so each capability is decided in exactly one place.
		template <typename T>
		constexpr PortValue (*defaultValueOf())()
		{
			if constexpr (std::is_default_constructible_v<T>)
				return &defaultPortValue<T>;
			else
				return nullptr;
		}

		template <typename T>
		constexpr int (*comparerOf())(const PortValue&, const PortValue&)
		{
			if constexpr (lain::meta::is_less_comparable_v<T>)
				return &comparePortValues<T>;
			else
				return nullptr;
		}
	} // namespace detail

	template <typename T>
	const PortType& portType()
	{
		if constexpr (lain::meta::is_vector_v<T>)
		{
			// A collection, so the capability is filled. `element` resolves the element type's own
			// descriptor — a separate function-local static, so a vector<vector<E>> nests with no
			// special case and each level answers isCollection() for itself.
			static const PortType info{
				std::type_index(typeid(T)),
				lain::meta::typeName<T>(),
				&detail::describeCollection<T>,
				detail::defaultValueOf<T>(),
				detail::comparerOf<T>(),
				&portType<lain::meta::vector_element_t<T>>(),
				&detail::collectionSize<T>,
				&detail::collectionAt<T>,
				&detail::gatherCollection<T>};
			return info;
		}
		else
		{
			// Not a collection: the four collection fields keep their null defaults, which is what
			// isCollection() reads.
			static const PortType info{
				std::type_index(typeid(T)),
				lain::meta::typeName<T>(),
				&detail::describePortValue<T>,
				detail::defaultValueOf<T>(),
				detail::comparerOf<T>()};
			return info;
		}
	}
} // namespace lain::flow
