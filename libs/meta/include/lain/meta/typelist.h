#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

// A compile-time list of types with a few queries — the metaprogramming primitive behind
// enum-indexed type tables (e.g. image's ChannelType -> storage type, PixelFormat ->
// Color). Pure std: no magic_enum / nameof, so this header is cheap to include. Sits
// directly in lain::meta (type-level), beside typeName / traits.
namespace lain::meta
{
	// A type carried as a value, so a runtime dispatch can hand a compile-time type to a
	// generic lambda (C++17 has no template lambdas). Recover the type inside the lambda
	// with `typename decltype(tag)::type`.
	template <typename T>
	struct TypeTag
	{
		using type = T;
	};

	template <typename... Ts>
	struct TypeList
	{
		// How many types are in the list.
		static constexpr std::size_t size = sizeof...(Ts);

		// The type at a (compile-time) index.
		template <std::size_t I>
		using at = std::tuple_element_t<I, std::tuple<Ts...>>;

		// Whether the list holds T.
		template <typename T>
		static constexpr bool contains = (std::is_same_v<T, Ts> || ...);

		// The index of the first T in the list (T must be present).
		template <typename T>
		static constexpr std::size_t indexOf()
		{
			static_assert(contains<T>, "TypeList::indexOf<T>: T is not in the list");
			constexpr bool matches[] = {std::is_same_v<T, Ts>...};
			for (std::size_t i = 0; i < size; ++i)
			{
				if (matches[i])
					return i;
			}
			return size; // unreachable given the static_assert
		}

		// sizeof the type at a runtime index.
		static constexpr std::size_t sizeAt(std::size_t i)
		{
			constexpr std::size_t sizes[] = {sizeof(Ts)...};
			return sizes[i];
		}

		// Dispatch a runtime index to fn(TypeTag<at<i>>{}) — the runtime bridge from an
		// index (e.g. an enum value) to its compile-time type. fn is instantiated for every
		// type in the list (all must compile); exactly the matching one runs.
		template <typename F>
		static void visitAt(std::size_t i, F&& fn)
		{
			visitAtImpl(i, std::forward<F>(fn), std::index_sequence_for<Ts...>{});
		}

	private:
		template <typename F, std::size_t... Is>
		static void visitAtImpl(std::size_t i, F&& fn, std::index_sequence<Is...>)
		{
			((i == Is ? static_cast<void>(fn(TypeTag<at<Is>>{})) : static_cast<void>(0)), ...);
		}
	};
} // namespace lain::meta
