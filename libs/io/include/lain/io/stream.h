#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lain::io
{
	// Where a seek offset is measured from.
	enum class SeekOrigin
	{
		Begin,
		Current,
		End
	};

	// The incremental, seekable transport — a PEER of read()/write(), for the assets a
	// whole-asset slurp cannot serve. A video container is opened once, its frame table is
	// built by seeking around it, and single frames are decoded on demand for as long as any
	// evaluation retains the sequence; read(uri) -> Buffer cannot express that (WORK.md M10,
	// ADR-0018). Dispatched by uri scheme exactly as read() is, and equally media-agnostic:
	// a Stream moves bytes and never decodes.
	//
	// The contract is stated as much by what a Stream REFUSES as by what it offers, and the
	// four refusals are what let a future LRU HANDLE POOL — a many-source timeline holds one
	// open handle per source — drop in as a pure backend swap, with no caller change:
	//
	//   1. It never hands out an OS handle. There is no fd(), no FILE*, no stream buffer.
	//   2. It owns its uri and its LOGICAL position, so it can reopen and re-seek beneath
	//      the caller. release() drops whatever the backend holds open and the next
	//      operation transparently re-acquires it, resuming exactly where it left off.
	//   3. Every operation may fail, not only open — a pooled handle can be lost at any
	//      point, so read/write/seek/size all report failure rather than only the open.
	//   4. Acquisition is separate from construction. A Stream is not an RAII wrapper whose
	//      handle lives exactly as long as it does. (openStream/createStream still validate
	//      eagerly, so a missing file is reported at open, as read()/write() report it — it
	//      is the coupling of the two LIFETIMES that this refuses, not early validation.)
	//
	// A Stream is NOT thread-safe: its owner serialises access, the way a media::FrameSource
	// already holds the lock around its decoder.
	class Stream
	{
	public:
		virtual ~Stream() = default;

		Stream(const Stream&) = delete;
		Stream& operator=(const Stream&) = delete;

		// The canonical uri this stream reads or writes (io::canonicalUri of what was asked
		// for). Canonical because a stream that reopens by a RELATIVE path is one chdir away
		// from silently touching a different file — and because a media::FrameRef names its
		// source by canonical uri, so one resource must have one name here too.
		const std::string& uri() const { return m_uri; }

		// The logical position of the next read or write, in bytes from the start. Owned
		// here rather than by the backend, which is what makes a reopen invisible.
		std::uint64_t position() const { return m_position; }

		// The size of the resource in bytes, or nullopt when it cannot be established.
		// Non-const: it may have to acquire a handle, and it may fail. For a write stream
		// this grows as the stream is written.
		[[nodiscard]] virtual std::optional<std::uint64_t> size() = 0;

		// Move the logical position; returns the new position, or nullopt when the seek is
		// impossible (before the start of the resource, or from End with an unknown size),
		// in which case the position is unchanged. Seeking PAST the end is allowed: a read
		// there yields 0 bytes, and a write there extends the resource.
		[[nodiscard]] std::optional<std::uint64_t> seek(std::int64_t offset, SeekOrigin from = SeekOrigin::Begin);

		// Drop whatever the backend is holding open. The stream stays fully usable — the
		// next operation re-acquires and resumes at position(). This is the hint a handle
		// pool evicts through, and it is a no-op if nothing is open.
		virtual void release() = 0;

	protected:
		explicit Stream(std::string uri)
			: m_uri(std::move(uri))
		{
		}

		// Record a completed transfer of `bytes` at the current position.
		void advance(std::uint64_t bytes) { m_position += bytes; }

	private:
		std::string m_uri;
		std::uint64_t m_position{0};
	};

	// A Stream opened for reading.
	class ReadStream : public Stream
	{
	public:
		// Read up to `bytes` into `dst`, advancing the position by what was read. Returns
		// the number of bytes actually read, or nullopt on a failure that transferred
		// nothing. EOF and failure are deliberately distinct: a decoder must not treat a
		// lost handle as a clean end.
		//
		// A short count means the end of the resource was reached (0 exactly at it) — or,
		// rarely, that a failure interrupted a partly-completed read, which the next call
		// then reports. Filling the request is enforced HERE rather than promised by each
		// backend, so a transport that can only return what one packet gave it cannot push
		// its partiality onto every caller.
		[[nodiscard]] std::optional<std::size_t> read(std::byte* dst, std::size_t bytes);

		// True when the position has reached the end of the resource. False when the size
		// cannot be established — the read is then the thing that reports the end.
		bool atEnd();

	protected:
		using Stream::Stream;

		// Backend contract: read at an ABSOLUTE position. The position is passed in rather
		// than tracked by the backend, so a re-acquired (or pooled and evicted) handle needs
		// no state of its own to be correct — refusal 2, made structural.
		[[nodiscard]] virtual std::optional<std::size_t> onRead(std::uint64_t at, std::byte* dst, std::size_t bytes) = 0;
	};

	// A Stream opened for writing. The resource is created, or truncated if it exists — the
	// same contract io::write() documents.
	//
	// Writing is open-push-finish and the result is not valid until finished, so finish() is
	// EXPLICIT and returns its status: a failure in a destructor has nowhere to go. Letting
	// the stream go without finishing still flushes and closes (the bytes reach the file);
	// what it cannot do is tell you whether that worked.
	class WriteStream : public Stream
	{
	public:
		// Write `bytes` from `src` at the current position, advancing it. Returns false on
		// failure, reason logged. Writing past the end extends the resource.
		[[nodiscard]] bool write(const std::byte* src, std::size_t bytes);

		// Flush and close. Idempotent — a second call repeats the first one's answer rather
		// than closing twice.
		[[nodiscard]] bool finish();

		// Whether finish() has already been called.
		bool finished() const { return m_finished; }

	protected:
		using Stream::Stream;

		// Backend contract: write at an ABSOLUTE position, and finish once. Positional for
		// the same reason onRead is — the backend holds no position of its own, so dropping
		// and re-acquiring its handle cannot put a write in the wrong place.
		[[nodiscard]] virtual bool onWrite(std::uint64_t at, const std::byte* src, std::size_t bytes) = 0;
		[[nodiscard]] virtual bool onFinish() = 0;

	private:
		bool m_finished{false};
		bool m_finishStatus{true};
	};

	// Open `uri` for incremental reading, or nullptr when it cannot be opened — missing
	// file, not a regular file, or an unsupported scheme. The reason is logged; the caller
	// decides how to surface it, exactly as with read(). Only the local/file scheme is
	// served today; remote/s3 dispatch behind this same function later.
	[[nodiscard]] std::unique_ptr<ReadStream> openStream(std::string_view uri);

	// Create (or truncate) `uri` for incremental writing, or nullptr when it cannot be
	// created — a missing parent directory, no permission, or an unsupported scheme. The
	// reason is logged. Named create rather than open because it is destructive, which is
	// what io::write() already does to an existing file.
	[[nodiscard]] std::unique_ptr<WriteStream> createStream(std::string_view uri);
} // namespace lain::io
