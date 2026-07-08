#pragma once

// Template method definitions for lain::core::Factory (see factory.h).

namespace lain::core
{
	template <typename Base>
	bool Factory<Base>::registerType(std::string key, Creator creator)
	{
		// .second is true when a new key was inserted, false when one was replaced.
		return m_creators.insert_or_assign(std::move(key), std::move(creator)).second;
	}

	template <typename Base>
	template <typename T, typename... Args>
	bool Factory<Base>::registerType(std::string key, Args&&... args)
	{
		static_assert(std::is_base_of<Base, T>::value, "T must derive from Base");
		return registerType(std::move(key), [args...]()
							{ return std::make_unique<T>(args...); });
	}

	template <typename Base>
	std::unique_ptr<Base> Factory<Base>::create(const std::string& key) const
	{
		const auto it = m_creators.find(key);
		return it != m_creators.end() ? it->second() : nullptr;
	}

	template <typename Base>
	bool Factory<Base>::contains(const std::string& key) const
	{
		return m_creators.count(key) != 0;
	}

	template <typename Base>
	std::size_t Factory<Base>::size() const
	{
		return m_creators.size();
	}

	template <typename Base>
	std::vector<std::string> Factory<Base>::keys() const
	{
		std::vector<std::string> out;
		out.reserve(m_creators.size());
		for (const auto& entry : m_creators)
			out.push_back(entry.first);
		return out;
	}
} // namespace lain::core
