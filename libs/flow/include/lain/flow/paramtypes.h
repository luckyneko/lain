#pragma once

#include <string>
#include <utility>

// Param helper value types — pure data (UI-free), declared by nodes and rendered by the
// adapter's type-keyed editor registry (ADR-0005). The TYPE carries the widget intent, so
// a specialised editor is a new type here, not a metadata flag. They live in `flow` for
// now; a non-node caller could promote one to lain::core/io later. (Choice, Range<T> join
// here when their first caller appears — an enum param, a bounded slider.)
namespace lain::flow
{
	// A filesystem path. A param of this type gets a file PICKER in the adapter, where a
	// plain std::string param gets a text field — the strong type is the only difference.
	class FilePath
	{
	public:
		FilePath() = default;
		explicit FilePath(std::string path)
			: m_path(std::move(path))
		{
		}

		const std::string& string() const { return m_path; }
		bool empty() const { return m_path.empty(); }

		std::string toString() const { return m_path; } // Param/Port describe() text

		bool operator==(const FilePath& other) const { return m_path == other.m_path; }
		bool operator!=(const FilePath& other) const { return !(*this == other); }

	private:
		std::string m_path;
	};
} // namespace lain::flow
