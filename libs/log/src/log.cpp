#include <lain/log/log.h>

#include <spdlog/spdlog.h>

namespace lain::log
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	static spdlog::level::level_enum toSpdlog(Level level)
	{
		switch (level)
		{
			case Level::Trace:
				return spdlog::level::trace;
			case Level::Debug:
				return spdlog::level::debug;
			case Level::Info:
				return spdlog::level::info;
			case Level::Warn:
				return spdlog::level::warn;
			case Level::Error:
				return spdlog::level::err;
			case Level::Critical:
				return spdlog::level::critical;
			case Level::Off:
				return spdlog::level::off;
		}
		return spdlog::level::info;
	}

	static Level fromSpdlog(spdlog::level::level_enum level)
	{
		switch (level)
		{
			case spdlog::level::trace:
				return Level::Trace;
			case spdlog::level::debug:
				return Level::Debug;
			case spdlog::level::info:
				return Level::Info;
			case spdlog::level::warn:
				return Level::Warn;
			case spdlog::level::err:
				return Level::Error;
			case spdlog::level::critical:
				return Level::Critical;
			case spdlog::level::off:
				return Level::Off;
			default:
				return Level::Info;
		}
	}

	// --- public API -------------------------------------------------------------

	void setLevel(Level level)
	{
		spdlog::set_level(toSpdlog(level));
	}

	Level level()
	{
		return fromSpdlog(spdlog::get_level());
	}

	void log(Level level, std::string_view message)
	{
		// "{}" + message (not message as the format) so any braces in the already-
		// formatted text are emitted as data, never reinterpreted as placeholders.
		spdlog::log(toSpdlog(level), "{}", message);
	}
} // namespace lain::log
