#pragma once

#include <string>
#if defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS_) || defined(WIN32)
#include <Mstcpip.h> // tcp_keepalive struct
#endif

#include "base/error.hpp"

namespace net {
	// SOCKETTYPE : tcp or udp or websocket
	template<class SOCKETTYPE>
	class Socket {
	public:
		using socket_type = std::remove_cv_t<std::remove_reference_t<SOCKETTYPE>>;

		template<class ...Args>
		explicit Socket(Args&&... args) : socket_(std::forward<Args>(args)...) {
		}

		~Socket() = default;

	public:
		inline socket_type & socket() {
			return this->socket_;
		}
		inline socket_type & stream() {
			return this->socket_;
		}

		inline std::string local_address() {
			try {
				return this->socket_.lowest_layer().local_endpoint().address().to_string();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return std::string();
		}

		inline unsigned short local_port() {
			try {
				return this->socket_.lowest_layer().local_endpoint().port();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return static_cast<unsigned short>(0);
		}

		inline std::string remote_address() {
			try {
				return this->socket_.lowest_layer().remote_endpoint().address().to_string();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return std::string();
		}

		inline unsigned short remote_port() {
			try {
				return this->socket_.lowest_layer().remote_endpoint().port();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return static_cast<unsigned short>(0);
		}

		void close() {
			this->socket_.lowest_layer().shutdown(asio::socket_base::shutdown_both, ec_ignore);
			this->socket_.lowest_layer().close(ec_ignore);
		}

	public:
		inline void sndbuf_size(int val) {
			try {
				this->socket_.lowest_layer().set_option(asio::socket_base::send_buffer_size(val));
			}
			catch (system_error & e) {
				set_last_error(e);
			}
		}
		inline int sndbuf_size() const {
			try {
				asio::socket_base::send_buffer_size option;
				this->socket_.lowest_layer().get_option(option);
				return option.value();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return (-1);
		}

		inline void rcvbuf_size(int val) {
			try {
				this->socket_.lowest_layer().set_option(asio::socket_base::receive_buffer_size(val));
			}
			catch (system_error & e) {
				set_last_error(e);
			}
		}
		inline int rcvbuf_size() const {
			try {
				asio::socket_base::receive_buffer_size option;
				this->socket_.lowest_layer().get_option(option);
				return option.value();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return (-1);
		}

		inline void reuse_address(bool val) {
			try {
				this->socket_.lowest_layer().set_option(asio::socket_base::reuse_address(val));
			}
			catch (system_error & e) {
				set_last_error(e);
			}
		}

		inline bool reuse_address() const {
			try {
				asio::socket_base::reuse_address option;
				this->socket_.lowest_layer().get_option(option);
				return option.value();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return false;
		}

		inline void no_delay(bool val) {
			try {
				if constexpr (std::is_same_v<typename socket_type::protocol_type, asio::ip::tcp>) {
					this->socket_.lowest_layer().set_option(asio::ip::tcp::no_delay(val));
				}
				else {
					std::ignore = true;
					static_assert(false, "Only tcp socket has the no_delay option");
				}
			}
			catch (system_error & e) {
				set_last_error(e);
			}
		}
		inline bool no_delay() const {
			try {
				if constexpr (std::is_same_v<typename socket_type::protocol_type, asio::ip::tcp>) {
					asio::ip::tcp::no_delay option;
					this->socket_.lowest_layer().get_option(option);
					return option.value();
				}
				else {
					std::ignore = true;
					static_assert(false, "Only tcp socket has the no_delay option");
				}
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return false;
		}

		inline void keep_alive(bool val) {
			try {
				this->socket_.lowest_layer().set_option(asio::socket_base::keep_alive(val));
			}
			catch (system_error & e) {
				set_last_error(e);
			}
		}
		inline bool keep_alive() const {
			try {
				asio::socket_base::keep_alive option;
				this->socket_.lowest_layer().get_option(option);
				return option.value();
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return false;
		}

		/**
		 * @function : set tcp socket keep alive options
		 * @param 	 :
		 * 			 onoff    - Turn keepalive on or off
		 * 			 idle     - How many seconds after the connection is idle, start sending keepalives
		 * 			 interval - How many seconds later to send again when no reply is received
		 *  		 count    - How many times to resend when no reply is received
		 */
		bool keep_alive_options(
			bool         onoff = true,
			unsigned int idle = 60,
			unsigned int interval = 3,
			unsigned int count = 3
		) 
		{
			if constexpr (!std::is_same_v<typename socket_type::protocol_type, asio::ip::tcp>) {
				return false;
			}
			try {
				std::ignore = count;

				auto & socket = this->socket_.lowest_layer();
				if (!socket.is_open()) {
					set_last_error(asio::error::not_connected);
					return false;
				}

				this->keep_alive(onoff);

				auto native_fd = socket.native_handle();

#if defined(__unix__) || defined(__linux__)
				// For *n*x systems
				int ret_keepidle = setsockopt(native_fd, SOL_TCP, TCP_KEEPIDLE, (void*)&idle, sizeof(unsigned int));
				int ret_keepintvl = setsockopt(native_fd, SOL_TCP, TCP_KEEPINTVL, (void*)&interval, sizeof(unsigned int));
				int ret_keepinit = setsockopt(native_fd, SOL_TCP, TCP_KEEPCNT, (void*)&count, sizeof(unsigned int));

				if (ret_keepidle || ret_keepintvl || ret_keepinit) {
					set_last_error(errno);
					return false;
				}
#elif defined(__OSX__)
				//// Set the timeout before the first keep alive message
				//int ret_tcpkeepalive = setsockopt(native_fd, IPPROTO_TCP, TCP_KEEPALIVE, (void*)&idle, sizeof(unsigned int));
				//int ret_tcpkeepintvl = setsockopt(native_fd, IPPROTO_TCP, TCP_CONNECTIONTIMEOUT, (void*)&interval, sizeof(unsigned int));

				//if (ret_tcpkeepalive || ret_tcpkeepintvl) {
				//	set_last_error(errno);
				//	return false;
				//}
#elif defined(_WIN32) || defined(_WIN64) || defined(_WINDOWS_) || defined(WIN32)
				// Partially supported on windows
				tcp_keepalive keepalive_options;
				keepalive_options.onoff = onoff;
				keepalive_options.keepalivetime = idle * 1000; // Keep Alive in milliseconds.
				keepalive_options.keepaliveinterval = interval * 1000; // Resend if No-Reply 

				DWORD bytes_returned = 0;

				if (SOCKET_ERROR == ::WSAIoctl(native_fd, SIO_KEEPALIVE_VALS, (LPVOID)&keepalive_options, (DWORD)sizeof(keepalive_options),
					nullptr, 0, (LPDWORD)&bytes_returned, nullptr, nullptr)) {
					if (::WSAGetLastError() != WSAEWOULDBLOCK) {
						set_last_error(::WSAGetLastError());
						return false;
					}
				}
#endif
				return true;
			}
			catch (system_error & e) {
				set_last_error(e);
			}
			return false;
		}

	protected:
		SOCKETTYPE socket_;
	};
}

