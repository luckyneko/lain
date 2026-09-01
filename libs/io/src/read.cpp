#include "lain/io/read.h"

#include "lain/io/stream.h"

#include <lain/log/log.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace lain::io
{
	std::optional<memory::Buffer> read(std::string_view uri)
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
			log::error("io::read: cannot determine size: {}", std::string(uri));
			return std::nullopt;
		}

		memory::Buffer buffer{static_cast<std::size_t>(*size)};
		if (*size > 0)
		{
			const std::optional<std::size_t> got = stream->read(buffer.data(), buffer.size());
			if (!got || *got != buffer.size())
			{
				log::error("io::read: short read ({} of {} bytes): {}",
						   got.value_or(0), buffer.size(), std::string(uri));
				return std::nullopt;
			}
		}
		return buffer;
	}
} // namespace lain::io
