#pragma once

#include <lain/flow/portvalue.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

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

		// `spellings` lists what the binder accepts when that is a closed set (an enum's names), so
		// `list` and a refusal can say what to type. Empty for a binder that takes open text: a
		// number or a path cannot be listed.
		void add(std::type_index type, Binder binder, std::vector<std::string> spellings = {});
		bool has(std::type_index type) const;

		// Parse `value` for `type`. nullopt means either no binder for the type OR a parse failure —
		// the caller disambiguates with has().
		std::optional<lain::flow::PortValue> bind(std::type_index type, const std::string& value) const;

		// What the type's binder accepts, in the order it lists them; empty when it takes open text or
		// there is no binder.
		const std::vector<std::string>& spellings(std::type_index type) const;

	private:
		struct Entry
		{
			Binder binder;
			std::vector<std::string> spellings;
		};
		std::map<std::type_index, Entry> m_binders;
	};

	// Register the built-in binders: the scalars (int / float / bool / string), an image (loads the
	// path), a frame sequence (opens the uri), a frame position, a camera model (reads a camera-model
	// document), a registered rig (reads a rig document), a rig's footage and models, each from a
	// folder whose entries, by name, are one camera each, and a calibration method by name
	// (`--method board-then-targetless`).
	void registerBoundaryBinders(BoundaryBinders& binders);

	// An enumerator's name as a command line spells it: lower case, words joined by '-', so
	// BoardThenTargetless is board-then-targetless. An enum binder lists its choices this way.
	std::string kebabCase(std::string_view name);
} // namespace flowview
