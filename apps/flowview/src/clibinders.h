#pragma once

#include <lain/flow/portvalue.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <typeindex>

namespace flowview
{
	// The CLI medium for a boundary type: parse a command-line string into a typed flow::PortValue.
	// Parallel to ParamEditors (the gui medium) and ValueCodecs (the on-disk medium) — a type declares
	// how it binds from the cli, so `flowview run --<input> <value>` stays type-agnostic and flow core
	// names no payload type. A type with no registered binder simply isn't cli-bindable (a clear error).
	class BoundaryBinders
	{
	public:
		// string -> value, or nullopt if the string doesn't parse as the type.
		using Binder = std::function<std::optional<lain::flow::PortValue>(const std::string&)>;

		void add(std::type_index type, Binder binder);
		bool has(std::type_index type) const;

		// Parse `value` for `type`. nullopt means either no binder for the type OR a parse failure —
		// the caller disambiguates with has().
		std::optional<lain::flow::PortValue> bind(std::type_index type, const std::string& value) const;

	private:
		std::map<std::type_index, Binder> m_binders;
	};

	// Register the built-in binders: the scalars (int / float / bool / string) + image (loads the path).
	void registerBoundaryBinders(BoundaryBinders& binders);
} // namespace flowview
