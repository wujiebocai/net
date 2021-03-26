#pragma once

namespace std
{
	/**
	 * BKDR Hash Function
	 */
	inline std::size_t bkdr_hash(const unsigned char* const p, std::size_t size)
	{
		std::size_t v = 0;
		for (std::size_t i = 0; i < size; ++i)
		{
			v = v * 131 + static_cast<std::size_t>(p[i]);
		}
		return v;
	}
	/**
	 * Fnv1a Hash Function
	 * Reference from Visual c++ implementation, see vc++ std::hash
	 */
	template<typename T>
	inline T fnv1a_hash(const unsigned char* const p, const T size) noexcept
	{
		static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Must be 32 or 64 digits");
		T v;
		if constexpr (sizeof(T) == 4)
			v = 2166136261u;
		else
			v = 14695981039346656037ull;
		for (T i = 0; i < size; ++i)
		{
			v ^= static_cast<T>(p[i]);
			if constexpr (sizeof(T) == 4)
				v *= 16777619u;
			else
				v *= 1099511628211ull;
		}
		return (v);
	}
	template<> struct hash<asio::ip::udp::endpoint>
	{
		typedef asio::ip::udp::endpoint argument_type;
		typedef std::size_t result_type;
		inline result_type operator()(argument_type const& s) const noexcept
		{
			//return std::hash<std::string_view>()(std::string_view{
			//	reinterpret_cast<std::string_view::const_pointer>(&s),sizeof(argument_type) });
			return bkdr_hash((const unsigned char*)(&s), sizeof(argument_type));
		}
	};

	template<> struct hash<asio::ip::tcp::endpoint>
	{
		typedef asio::ip::tcp::endpoint argument_type;
		typedef std::size_t result_type;
		inline result_type operator()(argument_type const& s) const noexcept
		{
			//return std::hash<std::string_view>()(std::string_view{
			//	reinterpret_cast<std::string_view::const_pointer>(&s),sizeof(argument_type) });
			return bkdr_hash((const unsigned char*)(&s), sizeof(argument_type));
		}
	};
}

