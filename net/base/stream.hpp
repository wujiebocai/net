#pragma once

#include <memory>

#include "base/iopool.hpp"
#include "base/error.hpp"
#include "base/socket.hpp"
#include "base/timer.hpp"
#include "opt/kcp/kcp_util.hpp"


namespace net {
	template<class ... Args>
	class NetStream {
	public:
		template<class ... Args>
		explicit NetStream(Args&&... args) {}
	};

	template<class ... Args>
	class StreamType {};
///////////////////////////binary stream//////////////////////////////////////////////////////////////////////////
	template<class DRIVERTYPE, class SOCKETTYPE, class SVRORCLI>
	class StreamType<DRIVERTYPE, SOCKETTYPE, binary_stream_flag, SVRORCLI> : public Socket<SOCKETTYPE>{
	public:
		using socket_type = Socket<SOCKETTYPE>;
	public:
		template<class ...Args>
		explicit StreamType(Args&&... args)
			: socket_type(std::forward<Args>(args)...)
			, derive_(static_cast<DRIVERTYPE&>(*this))
		{
		}
		template<class ...Args>
		explicit StreamType(asio::ip::udp::endpoint& remote_endpoint, Args&&... args)
			: socket_type(std::forward<Args>(args)...)
			, derive_(static_cast<DRIVERTYPE&>(*this))
			, remote_endpoint_(remote_endpoint)
		{
		}

		~StreamType() = default;

		inline auto& stream() { return socket_type::stream(); }
		inline auto& remote_endpoint() { return remote_endpoint_; }
		inline void handle_recv(error_code ec, const std::string& s) {
			this->derive_.cbfunc()->call(Event::recv, this->derive_.self_shared_ptr(), std::move(s));
		}
	protected:
		inline void stream_start(std::shared_ptr<DRIVERTYPE> dptr) {
		}

		inline void stream_stop(std::shared_ptr<DRIVERTYPE> dptr) {
			if constexpr (is_udp_socket_v<SOCKETTYPE> && is_svr_v<SVRORCLI>) {
				return;
			}
			socket_type::close();
		}

		template<typename Fn>
		inline void stream_post_handshake(std::shared_ptr<DRIVERTYPE> dptr, Fn&& fn) {
			fn(ec_ignore);
			if constexpr (is_udp_socket_v<SOCKETTYPE> && is_svr_v<SVRORCLI>) {
				auto& packstr = dptr->get_first_pack();
				this->handle_recv(ec_ignore, std::move(packstr));
			}
		}
	protected:
		DRIVERTYPE& derive_;
		asio::ip::udp::endpoint  remote_endpoint_;
	};
///////////////////ssl stream///////////////////////////////////////////////////////////////////
#if defined(NET_USE_SSL)
	template<class DRIVERTYPE, class SOCKETTYPE, class SVRORCLI>
	class StreamType<DRIVERTYPE, SOCKETTYPE, ssl_stream_flag, SVRORCLI> : public Socket<SOCKETTYPE> {
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
		inline void handle_recv(error_code ec, const std::string& s) {
			this->derive_.cbfunc()->call(Event::recv, this->derive_.self_shared_ptr(), std::move(s));
		}
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
				this->derive_.cbfunc()->call(Event::handshake, dptr, ec);
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
/////////////////kcp stream//////////////////////////////////////////////////////////////////////////////////////////////
	template<class DRIVERTYPE, class SOCKETTYPE, class SVRORCLI>
	class StreamType<DRIVERTYPE, SOCKETTYPE, kcp_stream_flag, SVRORCLI> : public Socket<SOCKETTYPE> {
	public:
		using socket_type = Socket<SOCKETTYPE>;
		using stream_type = StreamType<DRIVERTYPE, SOCKETTYPE, kcp_stream_flag, SVRORCLI>;
	public:
		template<class ...Args>
		explicit StreamType(NIO& io, Args&&... args)
			: socket_type(std::forward<Args>(args)...)
			, derive_(static_cast<DRIVERTYPE&>(*this))
			, kcp_io_(io)
			, kcp_timer_(io)
		{
		}
		template<class ...Args>
		explicit StreamType(asio::ip::udp::endpoint& remote_endpoint, NIO& io, Args&&... args)
			: socket_type(std::forward<Args>(args)...)
			, derive_(static_cast<DRIVERTYPE&>(*this))
			, kcp_io_(io)
			, kcp_timer_(io)
			, remote_endpoint_(remote_endpoint)
		{
		}

		~StreamType() {
			if (this->kcp_) {
				kcp::ikcp_release(this->kcp_);
				this->kcp_ = nullptr;
			}
		}

		inline auto& stream() { return socket_type::stream(); }
		inline auto& remote_endpoint() { return remote_endpoint_; }
		inline kcp::ikcpcb* kcp() {
			return this->kcp_;
		}
		
		inline void handle_recv(const error_code& ec, const std::string& s) {
			if (!this->derive_.is_started())
				return;
			if (!kcp_) {
				return;
			}
			if constexpr (is_svr_v<SVRORCLI>) {
				if (s.size() == sizeof(kcp::kcphdr)) {
					if (kcp::is_kcphdr_fin(s)) {
						this->send_fin_ = false;
						this->derive_.stop(asio::error::eof);
					}
					// Check whether the packet is SYN handshake
					// It is possible that the client did not receive the synack package, then the client
					// will resend the syn package, so we just need to reply to the syncack package directly.
					else if (kcp::is_kcphdr_syn(s)) {
						NET_ASSERT(kcp_);
						// step 4 : server send synack to client
						kcp::kcphdr* hdr = (kcp::kcphdr*)(s.data());
						kcp::kcphdr synack = kcp::make_kcphdr_synack(this->seq_, hdr->th_seq);
						error_code ed;
						this->derive_.kcp_send_hdr(synack, ed);
						if (ed)
							this->derive_.stop(ed);
					}
				}
				else
					this->derive_.kcp_do_recv_t(s);
			}
			else {
				if (s.size() == sizeof(kcp::kcphdr)) {
					if (kcp::is_kcphdr_fin(s)) {
						this->send_fin_ = false;
						this->derive_.stop(asio::error::eof);
					}
					else if (kcp::is_kcphdr_synack(s, this->seq_)) {
						//NET_ASSERT(false);
						this->derive_.stop(asio::error::operation_aborted);
					}
				}
				else
					this->derive_.kcp_do_recv_t(s);
			}

		}
	protected:
		/**
		 * @des : just used for kcp mode
		 * default mode : ikcp_nodelay(kcp, 0, 10, 0, 0);
		 * generic mode : ikcp_nodelay(kcp, 0, 10, 0, 1);
		 * fast    mode : ikcp_nodelay(kcp, 1, 10, 2, 1);
		 */
		inline void stream_start(std::shared_ptr<DRIVERTYPE> dptr, std::uint32_t conv) {
			if (this->kcp_) {
				//NET_ASSERT(false);
				return;
			}

			this->kcp_ = kcp::ikcp_create(conv, (void*)this);
			this->kcp_->output = &stream_type::kcp_output;

			kcp::ikcp_nodelay(this->kcp_, 1, 10, 2, 1);
			kcp::ikcp_wndsize(this->kcp_, 128, 512);

			this->post_kcp_timer(std::move(dptr));
		}

		inline void stream_stop(std::shared_ptr<DRIVERTYPE> dptr) {
			std::ignore = dptr;

			error_code ec;
			// if is kcp mode, send FIN handshake before close
			if (this->send_fin_)
				this->derive_.kcp_send_hdr(kcp::make_kcphdr_fin(0), ec);

			this->kcp_timer_.stop();
			if constexpr (is_udp_socket_v<SOCKETTYPE> && is_svr_v<SVRORCLI>) {
				return;
			}
			socket_type::close();
		}

		inline void post_kcp_timer(std::shared_ptr<DRIVERTYPE> dptr) {
			std::uint32_t clock1 = static_cast<std::uint32_t>(std::chrono::duration_cast<
				std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
			std::uint32_t clock2 = kcp::ikcp_check(this->kcp_, clock1);

			kcp_timer_.post_timer<false>((clock2 - clock1), [this, sptr = std::move(dptr)](const error_code& ec) mutable {
				this->handle_kcp_timer(ec, std::move(sptr));
			});
		}

		inline void handle_kcp_timer(const error_code& ec, std::shared_ptr<DRIVERTYPE> dptr) {
			if (ec == asio::error::operation_aborted) return;

			std::uint32_t clock = static_cast<std::uint32_t>(std::chrono::duration_cast<
				std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
			kcp::ikcp_update(this->kcp_, clock);
			if (derive_.is_started())
				//this->kcp_timer_.reset_active_time();
				this->post_kcp_timer(std::move(dptr));
		}

		template<typename Fn>
		inline void stream_post_handshake(std::shared_ptr<DRIVERTYPE> dptr, Fn && fn) {
			try {
				error_code ec;
				if constexpr (is_svr_v<SVRORCLI>) {
					// step 3 : server recvd syn from client (the first_pack_ is the syn)
					// Check whether the first_pack_ packet is SYN handshake
					if (!kcp::is_kcphdr_syn(dptr->get_first_pack())) {
						this->handle_handshake(asio::error::no_protocol_option, std::move(dptr));
						return;
					}

					// step 4 : server send synack to client
					kcp::kcphdr* hdr = (kcp::kcphdr*)(dptr->get_first_pack().data());
					const auto& key = dptr->hash_key();
					std::uint32_t conv = std::fnv1a_hash<std::uint32_t>(
						(const unsigned char* const)&key, std::uint32_t(sizeof(key)));
					this->seq_ = conv;
					kcp::kcphdr synack = kcp::make_kcphdr_synack(this->seq_, hdr->th_seq);
					this->derive_.kcp_send_hdr(synack, ec);
					asio::detail::throw_error(ec);

					this->stream_start(dptr, this->seq_);
					this->handle_handshake(ec, dptr);
					fn(ec_ignore);
				}
				else {
					// step 1 : client send syn to server
					this->seq_ = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch()).count());
					kcp::kcphdr syn = kcp::make_kcphdr_syn(this->seq_);
					this->derive_.kcp_send_hdr(syn, ec);
					asio::detail::throw_error(ec);

					kcp_timer_.post_timer(500, [this, syn](const error_code& ec) mutable {
						if (ec == asio::error::operation_aborted)
							return false;
						this->derive_.kcp_send_hdr(syn, ec);
						if (ec) {
							set_last_error(ec);
							derive_.stop(ec);
							return false;
						}
						return true;
					});

					// step 2 : client wait for recv synack util connect timeout or recvd some data
					derive_.stream().async_receive(asio::mutable_buffer(this->derive_.ubuffer().wr_buf(), this->derive_.ubuffer().wr_size()),
						asio::bind_executor(kcp_io_.strand(), [this, this_ptr = std::move(dptr), fn = std::move(fn)]
						(const error_code& ec, std::size_t bytes_recvd) mutable {
						try {
							kcp_timer_.stop();
						}
						catch (system_error&) {}
						catch (std::exception&) {}

						if (ec) {
							this->handle_handshake(ec, std::move(this_ptr));
							return;
						}

						this->derive_.ubuffer().wr_flip(bytes_recvd);

						std::string s(static_cast<std::string::const_pointer>
							(this->derive_.ubuffer().rd_buf()), bytes_recvd);
						this->derive_.ubuffer().rd_flip(bytes_recvd);

						// Check whether the data is the correct handshake information
						if (kcp::is_kcphdr_synack(s, this->seq_)) {
							std::uint32_t conv = ((kcp::kcphdr*)(s.data()))->th_seq;
							this->stream_start(this_ptr, conv);
							this->handle_handshake(ec, std::move(this_ptr));
							fn(ec_ignore);
						}
						else {
							this->handle_handshake(asio::error::no_protocol_option, std::move(this_ptr));
						}
					}));
				}
			}
			catch (system_error& e) {
				set_last_error(e);
				derive_.stop(e.code());
			}
		}

		inline void handle_handshake(const error_code& ec, std::shared_ptr<DRIVERTYPE> dptr) {
			set_last_error(ec);
			try {
				if constexpr (is_svr_v<SVRORCLI>) {
					this->derive_.cbfunc()->call(Event::handshake, dptr, ec);

					asio::detail::throw_error(ec);

					//this->derive_.handle_recv(ec, std::move(dptr->get_first_pack()));
				}
				else {
					this->derive_.cbfunc()->call(Event::handshake, dptr, ec);
				}
			}
			catch (system_error& e) {
				set_last_error(e);
				derive_.stop(e.code());
			}
		}

		static int kcp_output(const char* buf, int len, kcp::ikcpcb* kcp, void* user) {
			std::ignore = kcp;
			stream_type* zhis = ((stream_type*)user);

			DRIVERTYPE& derive = zhis->derive_;

			error_code ec;
			if constexpr (is_svr_v<SVRORCLI>)
				derive.stream().send_to(asio::buffer(buf, len),
					derive.remote_endpoint_, 0, ec);
			else
				derive.stream().send(asio::buffer(buf, len), 0, ec);

			return 0;
		}

	protected:
		asio::ip::udp::endpoint  remote_endpoint_;

		DRIVERTYPE& derive_;

		NIO& kcp_io_;

		kcp::ikcpcb* kcp_ = nullptr;

		std::uint32_t seq_ = 0;

		bool send_fin_ = true;

		net::Timer kcp_timer_;
	};
}
