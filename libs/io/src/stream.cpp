#include "lain/io/stream.h"

#include "lain/io/uri.h"
#include "localstream.h" // openLocalReadStream / createLocalWriteStream
#include "scheme.h"		 // parseUri / isLocalScheme (shared with read.cpp / write.cpp)

#include <lain/log/log.h>

#include <filesystem>
#include <optional>
#include <string>

namespace lain::io
{
	// --- Stream ----------------------------------------------------------------

	std::optional<std::uint64_t> Stream::seek(std::int64_t offset, SeekOrigin from)
	{
		std::int64_t base = 0;
		switch (from)
		{
			case SeekOrigin::Begin:
				base = 0;
				break;
			case SeekOrigin::Current:
				base = static_cast<std::int64_t>(position());
				break;
			case SeekOrigin::End:
			{
				const std::optional<std::uint64_t> end = size();
				if (!end)
				{
					log::warn("io::Stream::seek: size unknown, cannot seek from the end: {}", uri());
					return std::nullopt;
				}
				base = static_cast<std::int64_t>(*end);
				break;
			}
		}

		const std::int64_t target = base + offset;
		if (target < 0)
		{
			log::warn("io::Stream::seek: position {} is before the start: {}", target, uri());
			return std::nullopt;
		}

		// The position is the base's own state, so no backend is touched here at all: a seek
		// costs nothing until something is actually transferred, and a handle that was
		// released stays released.
		m_position = static_cast<std::uint64_t>(target);
		return m_position;
	}

	// --- ReadStream ------------------------------------------------------------

	std::optional<std::size_t> ReadStream::read(std::byte* dst, std::size_t bytes)
	{
		std::size_t done = 0;
		while (done < bytes)
		{
			const std::optional<std::size_t> got = onRead(position(), dst + done, bytes - done);
			if (!got)
				return done > 0 ? std::optional<std::size_t>{done} : std::nullopt;
			if (*got == 0)
				break; // the end of the resource

			done += *got;
			advance(*got);
		}
		return done;
	}

	bool ReadStream::atEnd()
	{
		const std::optional<std::uint64_t> end = size();
		if (!end)
			return false; // unknowable — let the read report the end instead
		return position() >= *end;
	}

	// --- WriteStream -----------------------------------------------------------

	bool WriteStream::write(const std::byte* src, std::size_t bytes)
	{
		if (bytes == 0)
			return true;
		if (!onWrite(position(), src, bytes))
			return false;

		advance(bytes);
		return true;
	}

	bool WriteStream::finish()
	{
		if (m_finished)
			return m_finishStatus;

		m_finished = true;
		m_finishStatus = onFinish();
		return m_finishStatus;
	}

	// --- scheme dispatch -------------------------------------------------------
	//
	// The scheme is asked about ONCE, by io::localPath: a uri it can turn into a path is one
	// this library can serve, and one it cannot is the unsupported-scheme branch. Splitting
	// that into a separate isLocalScheme test would be a second place to decide the same
	// thing, which is exactly the shape this hoist removed. canonicalUri leaves a non-local
	// uri untouched, so canonicalising first costs nothing and keeps one name per resource.

	std::unique_ptr<ReadStream> openStream(std::string_view uri)
	{
		std::string canonical = canonicalUri(uri);
		const std::optional<std::filesystem::path> path = localPath(canonical);
		if (!path)
		{
			log::warn("io::openStream: unsupported scheme '{}' in uri: {}",
					  std::string(parseUri(uri).scheme), std::string(uri));
			return nullptr;
		}
		return openLocalReadStream(*path, std::move(canonical));
	}

	std::unique_ptr<WriteStream> createStream(std::string_view uri)
	{
		std::string canonical = canonicalUri(uri);
		const std::optional<std::filesystem::path> path = localPath(canonical);
		if (!path)
		{
			log::warn("io::createStream: unsupported scheme '{}' in uri: {}",
					  std::string(parseUri(uri).scheme), std::string(uri));
			return nullptr;
		}
		return createLocalWriteStream(*path, std::move(canonical));
	}
} // namespace lain::io
