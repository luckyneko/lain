#include "lain/core/uuid.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>

namespace lain::core
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// A hex digit's value, or -1 if `c` is not one. Case-insensitive (parse is liberal).
	static int hexValue(char c)
	{
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	}

	// 64 random bits. The generator is thread_local, so generate() needs no lock and two threads
	// never share a sequence; it is seeded once per thread from random_device mixed with the clock
	// (random_device is deterministic on a few toolchains, and two threads created in the same
	// instant would otherwise be at risk of seeding alike).
	static std::uint64_t randomBits()
	{
		static thread_local std::mt19937_64 engine = []
		{
			std::random_device device;
			const std::uint64_t seed = (static_cast<std::uint64_t>(device()) << 32) ^ device() ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
			return std::mt19937_64{seed};
		}();
		return engine();
	}

	// --- Uuid --------------------------------------------------------------------

	Uuid Uuid::generate()
	{
		// RFC 9562 §5.7, version 7: a 48-bit big-endian Unix timestamp in milliseconds, then the
		// 4-bit version and 12 bits of randomness, then the 2-bit variant and 62 more random bits.
		// Same-millisecond monotonicity is optional in the spec and not implemented — nothing here
		// derives an order from a uuid (see the ordering note on operator<).
		const auto now = std::chrono::system_clock::now().time_since_epoch();
		const auto millis = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
		const std::uint64_t randA = randomBits();
		const std::uint64_t randB = randomBits();

		Uuid id;
		for (std::size_t i = 0; i < 6; ++i)
			id.m_bytes[i] = static_cast<std::uint8_t>((millis >> (8 * (5 - i))) & 0xff);
		id.m_bytes[6] = static_cast<std::uint8_t>(0x70 | ((randA >> 8) & 0x0f)); // version 7 + rand_a high nibble
		id.m_bytes[7] = static_cast<std::uint8_t>(randA & 0xff);
		id.m_bytes[8] = static_cast<std::uint8_t>(0x80 | ((randB >> 56) & 0x3f)); // variant 0b10 + rand_b
		for (std::size_t i = 9; i < 16; ++i)
			id.m_bytes[i] = static_cast<std::uint8_t>((randB >> (8 * (15 - i))) & 0xff);
		return id;
	}

	std::optional<Uuid> Uuid::parse(std::string_view text)
	{
		// Hyphens are stripped wherever they fall rather than checked against the 8-4-4-4-12 groups:
		// the point is that a hand-written or pasted id works, and a mis-grouped one is still
		// unambiguous. Everything else must be a hex digit, and there must be exactly 32 of them.
		Uuid id;
		std::size_t digits = 0;
		for (const char c : text)
		{
			if (c == '-')
				continue;
			const int value = hexValue(c);
			if (value < 0 || digits >= 32)
				return std::nullopt;
			// Two digits per byte, high nibble first.
			std::uint8_t& byte = id.m_bytes[digits / 2];
			byte = static_cast<std::uint8_t>((digits % 2 == 0) ? (value << 4) : (byte | value));
			++digits;
		}
		if (digits != 32)
			return std::nullopt;
		return id;
	}

	std::string Uuid::toString() const
	{
		static constexpr char kHex[] = "0123456789abcdef";
		std::string out;
		out.reserve(36);
		for (std::size_t i = 0; i < 16; ++i)
		{
			if (i == 4 || i == 6 || i == 8 || i == 10)
				out.push_back('-');
			out.push_back(kHex[m_bytes[i] >> 4]);
			out.push_back(kHex[m_bytes[i] & 0x0f]);
		}
		return out;
	}

	std::string Uuid::shortString() const
	{
		// The TAIL, not the head. A version-7 id leads with a millisecond timestamp, so every id
		// minted in one session shares its first several characters — a prefix would render four
		// distinct nodes as four copies of "019fc0ab...". The last 32 bits are pure randomness, so
		// they distinguish. ASCII "..." rather than an ellipsis glyph: this lands in ImGui labels,
		// whose default font atlas stops at U+00FF.
		const std::string canonical = toString();
		return "..." + canonical.substr(canonical.size() - 8);
	}

	bool Uuid::isNil() const
	{
		for (const std::uint8_t byte : m_bytes)
		{
			if (byte != 0)
				return false;
		}
		return true;
	}
} // namespace lain::core
