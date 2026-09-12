#include "lain/io/transport.h"

#include "lain/io/uri.h"
#include "localstream.h" // openLocalReadStream / createLocalWriteStream

#include <lain/log/log.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace lain::io
{
	// --- scheme dispatch -------------------------------------------------------
	//
	// The scheme is asked about ONCE, by Uri::path(): a uri it can turn into a path is one this
	// library can serve, and one it cannot is the unsupported-scheme branch. Splitting that into a
	// separate isLocal() test would be a second place to decide the same thing. canonicalise leaves
	// a non-local uri untouched, so canonicalising first costs nothing and keeps one name per
	// resource.
	//
	// NOT A REGISTRY, and that is a decision rather than an omission (WORK.md M10 slice 3): one
	// member is not a registry, the same judgement that left plugins/io/video without an aggregator
	// and io::sequence with a facade. The day a second scheme is actually served, these two
	// functions are where it attaches — the local backend is already reached through a pair of
	// factory functions (localstream.h) rather than a named class, so nothing above has to change.

	std::unique_ptr<ReadStream> openStream(const lain::core::Uri& uri)
	{
		lain::core::Uri canonical = canonicalise(uri);
		const std::optional<std::filesystem::path> path = canonical.path();
		if (!path)
		{
			log::warn("io::openStream: unsupported scheme '{}' in uri: {}",
					  std::string(canonical.scheme()), uri);
			return nullptr;
		}
		return openLocalReadStream(*path, std::move(canonical));
	}

	std::unique_ptr<WriteStream> createStream(const lain::core::Uri& uri)
	{
		lain::core::Uri canonical = canonicalise(uri);
		const std::optional<std::filesystem::path> path = canonical.path();
		if (!path)
		{
			log::warn("io::createStream: unsupported scheme '{}' in uri: {}",
					  std::string(canonical.scheme()), uri);
			return nullptr;
		}
		return createLocalWriteStream(*path, std::move(canonical));
	}

	// --- the whole-asset pair --------------------------------------------------

	std::optional<memory::Buffer> read(const core::Uri& uri)
	{
		// The whole-asset read is the incremental one used once: open, size, fill. Scheme
		// dispatch, the local backend and the failure reporting all live in openStream, so
		// there is exactly one implementation of "get bytes from a uri" per scheme.
		const std::unique_ptr<ReadStream> stream = openStream(uri);
		if (!stream)
			return std::nullopt; // reason logged by openStream

		const std::optional<std::uint64_t> size = stream->size();
		if (!size)
		{
			log::error("io::read: cannot determine size: {}", uri);
			return std::nullopt;
		}

		memory::Buffer buffer{static_cast<std::size_t>(*size)};
		if (*size > 0)
		{
			const std::optional<std::size_t> got = stream->read(buffer.data(), buffer.size());
			if (!got || *got != buffer.size())
			{
				log::error("io::read: short read ({} of {} bytes): {}",
						   got.value_or(0), buffer.size(), uri);
				return std::nullopt;
			}
		}
		return buffer;
	}

	bool write(const core::Uri& uri, const memory::Buffer& bytes)
	{
		// The whole-asset write is the incremental one used once: create, push, finish.
		// finish() rather than letting the stream go, because this call returns a status and
		// a close that failed is exactly what it must report.
		const std::unique_ptr<WriteStream> stream = createStream(uri);
		if (!stream)
			return false; // reason logged by createStream

		if (!stream->write(bytes.data(), bytes.size()))
			return false; // reason logged by the stream

		return stream->finish();
	}
} // namespace lain::io
