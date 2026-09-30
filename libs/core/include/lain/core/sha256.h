#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace lain::core
{
	// A SHA-256 digest (FIPS 180-4): 32 bytes, rendered as 64 lower-case hex digits.
	struct Sha256Digest
	{
		std::array<std::uint8_t, 32> bytes{};

		std::string toString() const; // 64 lower-case hex digits

		friend bool operator==(const Sha256Digest& a, const Sha256Digest& b) { return a.bytes == b.bytes; }
		friend bool operator!=(const Sha256Digest& a, const Sha256Digest& b) { return a.bytes != b.bytes; }
	};

	// SHA-256 over bytes that arrive in pieces, so a large file is hashed without being held whole.
	// std-only, because what needs it is identity rather than security: a board pattern's fingerprint
	// and the content hashes that pin an external dataset. Pieces may be any size; the digest depends
	// only on the concatenated bytes.
	class Sha256
	{
	public:
		Sha256();

		void update(const void* data, std::size_t size);
		void update(std::string_view bytes) { update(bytes.data(), bytes.size()); }

		// The digest of everything given since construction or the last finish(), which also resets
		// this to a fresh hash, so one object can hash several things in turn.
		Sha256Digest finish();

	private:
		void compress(const std::uint8_t* block);

		std::array<std::uint32_t, 8> m_state{};
		std::array<std::uint8_t, 64> m_block{};
		std::size_t m_blockSize = 0;	// bytes waiting in m_block
		std::uint64_t m_totalBytes = 0; // the message length, which the padding records in bits
	};

	// The digest of one contiguous run of bytes.
	Sha256Digest sha256(const void* data, std::size_t size);
	inline Sha256Digest sha256(std::string_view bytes)
	{
		return sha256(bytes.data(), bytes.size());
	}
} // namespace lain::core
