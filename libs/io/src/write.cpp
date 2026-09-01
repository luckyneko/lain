#include "lain/io/write.h"

#include "lain/io/stream.h"

#include <lain/log/log.h>

#include <memory>
#include <string>

namespace lain::io
{
	bool write(std::string_view uri, const memory::Buffer& bytes)
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
