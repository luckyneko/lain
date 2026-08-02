#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional> // std::hash specialisation below
#include <optional>
#include <string>
#include <string_view>

namespace lain::core
{
	// A 128-bit universally unique identifier (RFC 9562). Coordination-free identity: two machines
	// with no shared counter mint ids that will not collide, which is what a document edited on two
	// laptops needs. Used for flow's NodeId; nothing here is graph-specific.
	//
	// PARSING IS LIBERAL, GENERATION IS STRICT. generate() emits version 7 (a 48-bit millisecond
	// timestamp then random bits — so a minted id carries a rough creation-time shape in logs and
	// diffs). parse() accepts ANY well-formed uuid: 32 hex digits, either case, hyphens anywhere or
	// nowhere — no policing of the version or variant nibbles. That is deliberate: the value is
	// opaque to us, we need uniqueness rather than a version, and it is what makes hand-editing a
	// document work (`uuidgen` emits an UPPERCASE v4, and a pasted one must simply work).
	//
	// Only what an identifier needs: generate, parse, format, compare, hash. Not the rest of the
	// standard — no v1/v3/v4/v5/v8, no namespace derivation, no binary/urn forms, no brace syntax.
	class Uuid
	{
	public:
		// The NIL uuid (all bits zero) — the reserved "no value" sentinel, and what a document's
		// "00000000-0000-0000-0000-000000000000" parses to. Never minted by generate().
		Uuid() = default;

		// Mint a fresh version-7 uuid. Thread-safe (its generator state is thread-local).
		static Uuid generate();

		// Parse a uuid, or nullopt if `text` is not 32 hex digits once hyphens are removed. Case
		// insensitive. The nil uuid parses successfully, to the nil value — a caller that treats nil
		// as "missing" (as flow's loader does) checks isNil(), not the parse.
		static std::optional<Uuid> parse(std::string_view text);

		// The canonical lowercase 8-4-4-4-12 form: "019fbafb-2c31-7b8e-9a4f-1d6c0f2a8e55".
		std::string toString() const;

		// A truncated, DISPLAY-ONLY form: "..." then the last 8 hex digits — enough to tell two ids
		// apart at a glance in a node header or a log line, where the full 36 characters are noise.
		// The TAIL rather than the head, because a v7 id leads with a timestamp: ids minted in one
		// session share their first characters, and a prefix would show them all alike. Ambiguous by
		// construction, so never parse or key on it.
		std::string shortString() const;

		bool isNil() const;
		explicit operator bool() const { return !isNil(); }

		// The raw 16 bytes, big-endian (byte 0 is the leading hex pair of the canonical form).
		const std::array<std::uint8_t, 16>& bytes() const { return m_bytes; }

		// Ordering is a plain big-endian byte compare — it exists so a Uuid can key an ordered map,
		// and NOT as a creation order: v7's time prefix makes it *roughly* chronological, but ids
		// minted in the same millisecond are unordered and parse() accepts other versions. Anything
		// that needs a real order must track one.
		friend bool operator==(const Uuid& a, const Uuid& b) { return a.m_bytes == b.m_bytes; }
		friend bool operator!=(const Uuid& a, const Uuid& b) { return !(a == b); }
		friend bool operator<(const Uuid& a, const Uuid& b) { return a.m_bytes < b.m_bytes; }

	private:
		std::array<std::uint8_t, 16> m_bytes{}; // nil until generated or parsed
	};
} // namespace lain::core

// Hash so a Uuid keys an unordered_map/set. Folds the 16 bytes as two 64-bit halves — the bits are
// already well distributed (v7's tail is random), so no avalanche step is needed.
template <>
struct std::hash<lain::core::Uuid>
{
	std::size_t operator()(const lain::core::Uuid& id) const noexcept
	{
		const std::array<std::uint8_t, 16>& b = id.bytes();
		std::uint64_t high = 0;
		std::uint64_t low = 0;
		for (std::size_t i = 0; i < 8; ++i)
		{
			high = (high << 8) | b[i];
			low = (low << 8) | b[i + 8];
		}
		return static_cast<std::size_t>(high ^ (low + 0x9e3779b97f4a7c15ULL + (high << 6) + (high >> 2)));
	}
};
