#pragma once

//辅助asio进行相关错误处理.

#if !defined(NDEBUG) && !defined(_DEBUG) && !defined(DEBUG)
#define NDEBUG
#endif

#include <cerrno>
#include <cassert>
#include <string>
#include <system_error>

namespace net {

	#ifdef NET_ASSERT
		static_assert(false, "Unknown NET_ASSERT definition will affect the relevant functions of this program.");
	#else
		#if defined(_DEBUG) || defined(DEBUG)
			#define NET_ASSERT(x) assert(x);
		#else
			#define NET_ASSERT(x) (void)0;
		#endif
	#endif

	namespace {
		using error_code = ::asio::error_code;
		using system_error = ::asio::system_error;
		
		thread_local static error_code ec_last;

		inline error_code & get_last_error() {
			return ec_last;
		}

		inline void set_last_error(int ec) {
			ec_last.assign(ec, asio::error::get_system_category());
		}

		template<typename T>
		inline void set_last_error(int ec, const T& ecat) {
			ec_last.assign(ec, ecat);
		}

		inline void set_last_error(const error_code & ec) {
			ec_last = ec;
		}

		inline void set_last_error(const system_error & e) {
			ec_last = e.code();
		}

		inline void clear_last_error() {
			ec_last.clear();
		}

		inline auto last_error_val() {
			return ec_last.value();
		}

		inline auto last_error_msg() {
			return ec_last.message();
		}

	}

	//error_code的线程局部变量，仅用于占位符。
	thread_local static error_code ec_ignore;
}
