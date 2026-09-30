#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lain::core
{
	// Hex text for bytes: two lower-case digits per byte, high nibble first, nothing between.
	//
	// One implementation because there are two consumers: Uuid's canonical form and a Sha256Digest's.
	// A second copy is how two spellings of one digest come to disagree about case, and a content
	// hash compared as text is only as good as its spelling is fixed.
	std::string hexDigits(const std::uint8_t* bytes, std::size_t count);

	// A hex digit's value (0-15), either case, or -1 when `c` is not one.
	int hexDigitValue(char c);
} // namespace lain::core
