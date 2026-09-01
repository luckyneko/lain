#include "localstream.h"

#include <lain/log/log.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ios>
#include <optional>
#include <system_error>
#include <utility>

namespace lain::io
{
	// --- file-local helpers (named static, not an anonymous namespace) ----------

	// A local file opened for incremental reading.
	//
	// The handle is dropped and re-acquired freely: the base owns the logical position and
	// hands it to every onRead, so this class needs no position of its own to be correct.
	// The one it does keep is the PHYSICAL one — where the ifstream's get pointer actually
	// sits — purely so a sequential decode does not pay a seekg per read.
	class LocalReadStream final : public ReadStream
	{
	public:
		LocalReadStream(std::filesystem::path path, std::string uri, std::ifstream file, std::uint64_t size)
			: ReadStream(std::move(uri))
			, m_path(std::move(path))
			, m_file(std::move(file))
			, m_size(size)
		{
		}

		// Fixed at open: a resource that changes size under an open stream is out of
		// contract, so this is cached rather than re-stat'd and can never fail.
		std::optional<std::uint64_t> size() override { return m_size; }

		void release() override
		{
			if (m_file.is_open())
				m_file.close();
			m_file.clear();
		}

	protected:
		std::optional<std::size_t> onRead(std::uint64_t at, std::byte* dst, std::size_t bytes) override
		{
			if (!acquire())
				return std::nullopt;
			if (at >= m_size)
				return std::size_t{0}; // at or past the end

			const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(bytes, m_size - at));
			if (m_physical != at)
			{
				m_file.seekg(static_cast<std::streamoff>(at));
				if (!m_file)
				{
					log::error("io::Stream::read: cannot seek to {}: {}", at, m_path.string());
					release();
					return std::nullopt;
				}
				m_physical = at;
			}

			m_file.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(want));
			const auto got = static_cast<std::size_t>(m_file.gcount());
			m_physical += got;
			if (got != want)
			{
				log::error("io::Stream::read: short read ({} of {} bytes) at {}: {}", got, want, at, m_path.string());
				release(); // a fresh handle is the honest retry; this one's state is unknown
				return std::nullopt;
			}
			return got;
		}

	private:
		// Re-open the file if the handle was released. The position is not restored here —
		// onRead seeks to the absolute position it was given, so a re-acquire needs no
		// memory of where the last one had got to.
		bool acquire()
		{
			if (m_file.is_open())
				return true;

			m_file.open(m_path, std::ios::binary);
			if (!m_file)
			{
				log::error("io::Stream::read: cannot reopen: {}", m_path.string());
				m_file.clear();
				return false;
			}
			m_physical = 0;
			return true;
		}

		std::filesystem::path m_path;
		std::ifstream m_file;
		std::uint64_t m_size{0};
		std::uint64_t m_physical{0};
	};

	// A local file opened for incremental writing.
	//
	// Re-acquiring after a release() must NOT truncate — that would throw away everything
	// written so far — so the first open creates/truncates and every later one reopens for
	// update. The same update mode is what lets a positioned write land back over earlier
	// bytes, which is exactly what a muxer does when it patches its header at the end.
	class LocalWriteStream final : public WriteStream
	{
	public:
		LocalWriteStream(std::filesystem::path path, std::string uri, std::ofstream file)
			: WriteStream(std::move(uri))
			, m_path(std::move(path))
			, m_file(std::move(file))
		{
		}

		~LocalWriteStream() override
		{
			// Letting a write stream go without finishing still flushes and closes it; what
			// it cannot do is report a failure to anyone, which is why finish() exists.
			if (!finished() && !finish())
				log::error("io::Stream: unfinished write stream failed to close: {}", m_path.string());
		}

		// Tracked as it is written rather than stat'd, so it is right while bytes are still
		// buffered. A write past the end extends the resource, hence the max.
		std::optional<std::uint64_t> size() override { return m_size; }

		void release() override
		{
			if (m_file.is_open())
				m_file.close();
			m_file.clear();
		}

	protected:
		bool onWrite(std::uint64_t at, const std::byte* src, std::size_t bytes) override
		{
			if (!acquire())
				return false;

			if (m_physical != at)
			{
				m_file.seekp(static_cast<std::streamoff>(at));
				if (!m_file)
				{
					log::error("io::Stream::write: cannot seek to {}: {}", at, m_path.string());
					release();
					return false;
				}
				m_physical = at;
			}

			m_file.write(reinterpret_cast<const char*>(src), static_cast<std::streamsize>(bytes));
			if (!m_file.good())
			{
				log::error("io::Stream::write: write failed ({} bytes) at {}: {}", bytes, at, m_path.string());
				release();
				return false;
			}

			m_physical += bytes;
			m_size = std::max(m_size, at + bytes);
			return true;
		}

		bool onFinish() override
		{
			if (!m_file.is_open())
				return true; // released; every write that reported success is already on disk

			m_file.flush();
			const bool ok = m_file.good();
			m_file.close();
			if (!ok || m_file.fail())
			{
				log::error("io::Stream::finish: could not flush and close: {}", m_path.string());
				m_file.clear();
				return false;
			}
			return true;
		}

	private:
		bool acquire()
		{
			if (m_file.is_open())
				return true;

			// in|out, NOT trunc: the file already exists (creation happened at open) and
			// everything written before the release must survive.
			m_file.open(m_path, std::ios::binary | std::ios::in | std::ios::out);
			if (!m_file)
			{
				log::error("io::Stream::write: cannot reopen: {}", m_path.string());
				m_file.clear();
				return false;
			}
			m_physical = 0;
			return true;
		}

		std::filesystem::path m_path;
		std::ofstream m_file;
		std::uint64_t m_size{0};
		std::uint64_t m_physical{0};
	};

	// --- factories -------------------------------------------------------------

	std::unique_ptr<ReadStream> openLocalReadStream(const std::filesystem::path& path, std::string uri)
	{
		std::error_code error;
		if (!std::filesystem::is_regular_file(path, error))
		{
			log::warn("io::openStream: not a readable file: {}", path.string());
			return nullptr;
		}

		const std::uintmax_t size = std::filesystem::file_size(path, error);
		if (error)
		{
			log::error("io::openStream: cannot determine size: {}", path.string());
			return nullptr;
		}

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			log::warn("io::openStream: cannot open: {}", path.string());
			return nullptr;
		}

		return std::make_unique<LocalReadStream>(path, std::move(uri), std::move(file), static_cast<std::uint64_t>(size));
	}

	std::unique_ptr<WriteStream> createLocalWriteStream(const std::filesystem::path& path, std::string uri)
	{
		// Created / truncated here and never again: this is the one open that is allowed to
		// destroy what was there, so a later re-acquire cannot be the one that does it.
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			log::warn("io::createStream: cannot open for writing: {}", path.string());
			return nullptr;
		}

		return std::make_unique<LocalWriteStream>(path, std::move(uri), std::move(file));
	}
} // namespace lain::io
