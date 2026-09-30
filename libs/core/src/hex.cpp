#include "lain/core/hex.h"

namespace lain::core
{
	std::string hexDigits(const std::uint8_t* bytes, std::size_t count)
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string out;
		out.reserve(count * 2);
		for (std::size_t i = 0; i < count; ++i)
		{
			out.push_back(kHex[bytes[i] >> 4]);
			out.push_back(kHex[bytes[i] & 0x0f]);
		}
		return out;
	}

	int hexDigitValue(char c)
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}
} // namespace lain::core
