#include "lain/core/sha256.h"

#include "lain/core/hex.h"

#include <algorithm>
#include <cstring>

namespace lain::core
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// The first 32 bits of the fractional parts of the cube roots of the first 64 primes.
	static constexpr std::uint32_t kRound[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

	// The first 32 bits of the fractional parts of the square roots of the first 8 primes.
	static constexpr std::array<std::uint32_t, 8> kInitial = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
															  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

	static std::uint32_t rotateRight(std::uint32_t value, int bits)
	{
		return (value >> bits) | (value << (32 - bits));
	}

	// --- Sha256 -----------------------------------------------------------------

	Sha256::Sha256()
		: m_state(kInitial)
	{
	}

	void Sha256::compress(const std::uint8_t* block)
	{
		std::uint32_t w[64];
		for (int i = 0; i < 16; ++i)
			w[i] = (std::uint32_t(block[4 * i]) << 24) | (std::uint32_t(block[4 * i + 1]) << 16) |
				   (std::uint32_t(block[4 * i + 2]) << 8) | std::uint32_t(block[4 * i + 3]);
		for (int i = 16; i < 64; ++i)
		{
			const std::uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
			const std::uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2], d = m_state[3];
		std::uint32_t e = m_state[4], f = m_state[5], g = m_state[6], h = m_state[7];
		for (int i = 0; i < 64; ++i)
		{
			const std::uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
			const std::uint32_t choose = (e & f) ^ (~e & g);
			const std::uint32_t t1 = h + s1 + choose + kRound[i] + w[i];
			const std::uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
			const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
			const std::uint32_t t2 = s0 + majority;
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		m_state[0] += a;
		m_state[1] += b;
		m_state[2] += c;
		m_state[3] += d;
		m_state[4] += e;
		m_state[5] += f;
		m_state[6] += g;
		m_state[7] += h;
	}

	void Sha256::update(const void* data, std::size_t size)
	{
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		m_totalBytes += size;
		while (size > 0)
		{
			const std::size_t take = std::min(size, m_block.size() - m_blockSize);
			std::memcpy(m_block.data() + m_blockSize, bytes, take);
			m_blockSize += take;
			bytes += take;
			size -= take;
			if (m_blockSize == m_block.size())
			{
				compress(m_block.data());
				m_blockSize = 0;
			}
		}
	}

	Sha256Digest Sha256::finish()
	{
		// Padding: one 1 bit, then zeros until 8 bytes short of a block boundary, then the message
		// length in bits, big-endian. When fewer than 9 bytes are left in the current block the
		// length does not fit, and the padding runs into one more block.
		const std::uint64_t bits = m_totalBytes * 8;
		const std::uint8_t one = 0x80;
		update(&one, 1);
		const std::uint8_t zero = 0;
		while (m_blockSize != 56)
			update(&zero, 1);
		std::uint8_t length[8];
		for (int i = 0; i < 8; ++i)
			length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
		update(length, 8);

		Sha256Digest digest;
		for (int i = 0; i < 8; ++i)
		{
			digest.bytes[4 * i] = static_cast<std::uint8_t>(m_state[i] >> 24);
			digest.bytes[4 * i + 1] = static_cast<std::uint8_t>(m_state[i] >> 16);
			digest.bytes[4 * i + 2] = static_cast<std::uint8_t>(m_state[i] >> 8);
			digest.bytes[4 * i + 3] = static_cast<std::uint8_t>(m_state[i]);
		}
		*this = Sha256{};
		return digest;
	}

	// --- free functions ---------------------------------------------------------

	std::string Sha256Digest::toString() const
	{
		return hexDigits(bytes.data(), bytes.size());
	}

	Sha256Digest sha256(const void* data, std::size_t size)
	{
		Sha256 hash;
		hash.update(data, size);
		return hash.finish();
	}
} // namespace lain::core
