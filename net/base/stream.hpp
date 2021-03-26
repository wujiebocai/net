#pragma once

#include <memory>

#include "base/iopool.hpp"
#include "base/error.hpp"
#include "base/socket.hpp"


namespace net {
	template<class ... Args>
	class NetStream {
	public:
		template<class ... Args>
		explicit NetStream(Args&&... args) {}
	};

	template<class DRIVERTYPE, class SOCKETTYPE, class STREAMTYPE>
	class StreamType {};
///////////////////////////binary stream//////////////////////////////////////////////////////////////////////////
	template<class DRIVERTYPE, class SOCKETTYPE>
	class StreamType<DRIVERTYPE, SOCKETTYPE, binary_stream_flag> : public Socket<SOCKETTYPE>{
	public:
		using socket_type = Socket<SOCKETTYPE>;
	public:
		template<class ...Args>
		explicit StreamType(Args&&... args)
			: socket_type(std::forward<Args>(args)...)
		{
		}
		template<class ...Args>
		explicit StreamType(asio::ip::udp::endpoint& remote_endpoint, Args&&... args)
			: socket_type(std::forward<Args>(args)...)
			, remote_endpoint_(remote_endpoint)
		{
		}

		~StreamType() = default;

		inline auto& stream() { return socket_type::stream(); }
		inline auto& remote_endpoint() { return remote_endpoint_; }
	protected:
		inline void stream_start(std::shared_ptr<DRIVERTYPE> dptr) {
		}

		inline void stream_stop(std::shared_ptr<DRIVERTYPE> dptr) {
			this->socket_.shutdown(asio::socket_base::shutdown_both, ec_ignore);
			this->socket_.close();
		}

		template<typename Fn>
		inline void stream_post_handshake(std::shared_ptr<DRIVERTYPE> dptr, Fn&& fn) {
			fn(ec_ignore);
		}
	protected:
		asio::ip::udp::endpoint  remote_endpoint_;
	};
///////////////////ssl stream///////////////////////////////////////////////////////////////////
#if defined(NET_USE_SSL)
	template<class DRIVERTYPE, class SOCKETTYPE>
	class StreamType<DRIVERTYPE, SOCKETTYPE, ssl_stream_flag> : public Socket<SOCKETTYPE> {
	public:
		using socket_type = Socket<SOCKETTYPE>;
		using stream_type = asio::ssl::stream<SOCKETTYPE&>;
		using handshake_type = typename asio::ssl::stream_base::handshake_type;

	public:
		template<class ...Args>
		explicit StreamType(NIO& io, asio::ssl::context& ctx, handshake_type type, Args&&... args)
			: socket_type(std::forward<Args>(args)...)
			, derive_(static_cast<DRIVERTYPE&>(*this))
			, ssl_io_(io)
			, ssl_stream_(socket_, ctx)
			, ssl_timer_(io.context())
			, ssl_type_(type)
		{
		}

		~StreamType() = default;

		inline stream_type& stream() { return this->ssl_stream_; }

	protected:
		inline void stream_start(std::shared_ptr<DRIVERTYPE> dptr) {
		}

		inline void stream_stop(std::shared_ptr<DRIVERTYPE> dptr) {
			//socket 没有关闭, async_shutdown回调函数也永远不会被调用.
			this->ssl_stream_.async_shutdown(asio::bind_executor(this->ssl_io_.strand(),
				[this, dptr = std::move(dptr)](const error_code& ec) {
				set_last_error(ec);

				// close the ssl timer
				this->ssl_timer_.cancel();
			}));
			this->socket_.close();
		}

		template<typename Fn>
		inline void stream_post_handshake(std::shared_ptr<DRIVERTYPE> dptr, Fn&& fn) {
			this->ssl_stream_.async_handshake(this->ssl_type_,
				asio::bind_executor(this->ssl_io_.strand(),
					[this, dptr = std::move(dptr), fn = std::move(fn)](const error_code& ec)
			{
				this->handle_handshake(ec, std::move(dptr), fn);
			}));
		}

		template<typename Fn>
		inline void handle_handshake(const error_code& ec, std::shared_ptr<DRIVERTYPE> dptr, Fn&& fn) {
			asio::post(this->derive_.cio().strand(), [this, ec, dptr = std::move(dptr), fn = std::forward<Fn>(fn)]() mutable {
				fn(ec);
			});
		}

	protected:
		DRIVERTYPE& derive_;
		NIO& ssl_io_;
		stream_type ssl_stream_;
		asio::steady_timer ssl_timer_;
		handshake_type ssl_type_;
	};
	// ssl
	template<class SOCKETTYPE>
	class NetStream<SOCKETTYPE, ssl_stream_flag> : public asio::ssl::context {
	public:
		explicit NetStream(svr_place, asio::ssl::context::method method = asio::ssl::context::sslv23)
			: asio::ssl::context(method)
			//, ssl_stream_comp(this->io_, this->socket_, *this, asio::ssl::stream_base::client)
		{
			this->set_options(
				asio::ssl::context::default_workarounds |
				asio::ssl::context::no_sslv2 |
				asio::ssl::context::no_sslv3 |
				asio::ssl::context::single_dh_use
			);
		}

		explicit NetStream(client_place, asio::ssl::context::method method = asio::ssl::context::sslv23)
			: asio::ssl::context(method)
		{
		}

		~NetStream() {
		}

		inline void set_cert(const std::string& password, std::string_view certificate, std::string_view key, std::string_view dh) {
			this->set_password_callback([password]
			(std::size_t max_length, asio::ssl::context_base::password_purpose purpose) -> std::string {
				return password;
			});

			this->use_certificate_chain(asio::buffer(certificate));
			this->use_private_key(asio::buffer(key), asio::ssl::context::pem);
			this->use_tmp_dh(asio::buffer(dh));
		}

		inline void set_cert_file(const std::string& password, const std::string& certificate, const std::string& key, const std::string& dh) {
			this->set_password_callback([password]
			(std::size_t max_length, asio::ssl::context_base::password_purpose purpose) -> std::string {
				return password;
			});

			this->use_certificate_chain_file(certificate);
			this->use_private_key_file(key, asio::ssl::context::pem);
			this->use_tmp_dh_file(dh);
		}

		inline void set_cert(std::string_view cert) {
			this->add_certificate_authority(asio::buffer(cert));
		}

		inline void set_cert_file(const std::string& file) {
			this->load_verify_file(file);
		}
	};
#endif
/////////////////kcp stream (具体逻辑有待实现)//////////////////////////////////////////////////////////////////////////////////////////////
	template<class DRIVERTYPE, class SOCKETTYPE>
	class StreamType<DRIVERTYPE, SOCKETTYPE, kcp_stream_flag> : public Socket<SOCKETTYPE> {
	public:
		using socket_type = Socket<SOCKETTYPE>;
	public:
		template<class ...Args>
		explicit StreamType(Args&&... args)
			: socket_type(std::forward<Args>(args)...)
		{
		}

		~StreamType() = default;
	protected:
		inline void stream_start(std::shared_ptr<DRIVERTYPE> dptr) {
		}

		inline void stream_stop(std::shared_ptr<DRIVERTYPE> dptr) {
			this->socket_.close();
		}

		template<typename Fn>
		inline void stream_post_handshake(std::shared_ptr<DRIVERTYPE> dptr, Fn&& fn) {
			fn(ec_ignore);
		}
	};
	template<class SOCKETTYPE>
	class NetStream<SOCKETTYPE, kcp_stream_flag> {
	public:
		NetStream() = default;
		~NetStream() = default;
	};
}
