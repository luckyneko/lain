#include "lain/core/platform.h"

#include <cstdlib>
#include <string>

namespace lain::core
{
	std::string envVar(std::string_view name)
	{
		const std::string key(name); // getenv / _dupenv_s want a null-terminated C string
#ifdef _WIN32
		char* value = nullptr;
		std::size_t len = 0;
		std::string result;
		if (_dupenv_s(&value, &len, key.c_str()) == 0 && value != nullptr)
		{
			result = value;
			std::free(value);
		}
		return result;
#else
		const char* value = std::getenv(key.c_str());
		return value != nullptr ? std::string(value) : std::string();
#endif
	}
} // namespace lain::core
