#pragma once

#include <lain/meta/traits.h>	 // has_to_string / has_ostream
#include <lain/meta/typenames.h> // typeName<T>() — the fallback

#include <sstream>
#include <string>
#include <type_traits>

// Best-effort stringify for any type, in lain::meta (introspection, not formatting).
// Unlike lain::string::format — which needs a *formattable* type and errors on an
// opaque one — toString always produces something, degrading to the type's name. It
// is fmt-free (built only on meta's own traits + typeName), so it can sit this low.
// The dispatch is compile-time (if constexpr), so each arm compiles only for the
// types it applies to.
namespace lain::meta
{
	// Render `v` as a human-readable string: a bool as true/false, a type with a
	// string-returning toString() through that, anything stream-insertable through an
	// ostream, and everything else as "<TypeName>" (no value, just the type). The free
	// toString detects the *member* toString() — the two never collide.
	template <typename T>
	std::string toString(const T& v)
	{
		if constexpr (std::is_same_v<T, bool>)
			return v ? "true" : "false";
		else if constexpr (has_to_string_v<T>)
			return v.toString();
		else if constexpr (has_ostream_v<T>)
		{
			std::ostringstream os;
			os << v;
			return os.str();
		}
		else
			return "<" + std::string(typeName<T>()) + ">";
	}
} // namespace lain::meta
