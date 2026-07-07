#pragma once

#include <fmt/format.h>

#include <cassert>
#include <string_view>
#include <utility>

// lain::log — a thin logging surface over spdlog. The backend is named nowhere but
// log.cpp; consumers see only lain's own Level enum and the typed front-ends below.
// Formatting is fmt's (C++17 has no std::format), so a format string is checked
// against its arguments at compile time.
namespace lain::log
{
	// Severity, low -> high. Our own enum (mapped to the backend's levels in log.cpp)
	// so spdlog never appears in a consumer's translation unit.
	enum class Level
	{
		Trace,
		Debug,
		Info,
		Warn,
		Error,
		Critical,
		Off,
	};

	// Drop messages below `level`. Defaults to the backend's start level until set.
	void setLevel(Level level);
	Level level();

	// The backend seam: emit an already-formatted message at `level`. Every typed
	// front-end below funnels here, and a caller holding a ready string (or one built
	// with a runtime format) can call it directly. The only entry point that names
	// the backend (defined in log.cpp); braces in `message` are treated as data.
	void log(Level level, std::string_view message);

	// Typed front-ends: format with fmt (checked against the argument types at compile
	// time) and forward to log(). For a runtime/dynamic format string, build the
	// message yourself and pass it through log(), or wrap it — e.g. for a dynamic
	// payload use log::info("{}", value).
	template <typename... Args>
	void trace(fmt::format_string<Args...> format, Args&&... args)
	{
		log(Level::Trace, fmt::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void debug(fmt::format_string<Args...> format, Args&&... args)
	{
		log(Level::Debug, fmt::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void info(fmt::format_string<Args...> format, Args&&... args)
	{
		log(Level::Info, fmt::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void warn(fmt::format_string<Args...> format, Args&&... args)
	{
		log(Level::Warn, fmt::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void error(fmt::format_string<Args...> format, Args&&... args)
	{
		log(Level::Error, fmt::format(format, std::forward<Args>(args)...));
	}

	template <typename... Args>
	void critical(fmt::format_string<Args...> format, Args&&... args)
	{
		log(Level::Critical, fmt::format(format, std::forward<Args>(args)...));
	}

	// A checked precondition: returns `cond`, and when it is false logs an error (the rich,
	// formatted detail) then asserts. So a violation aborts loudly in a debug build; in a
	// release build (NDEBUG) it logs and returns false, letting the caller bail. Branch on
	// it: `if (!log::ensure(cond, "...")) return {};`. (Unlike an assert macro it cannot
	// stringify the expression — the formatted message carries the detail instead.)
	template <typename... Args>
	bool ensure(bool cond, fmt::format_string<Args...> format, Args&&... args)
	{
		if (!cond)
		{
			error(format, std::forward<Args>(args)...);
			assert(false && "lain::log::ensure failed — see the logged error");
		}
		return cond;
	}
} // namespace lain::log
