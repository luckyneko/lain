#include "lain/io/stream.h"

#include <lain/log/log.h>

#include <optional>

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
} // namespace lain::io
