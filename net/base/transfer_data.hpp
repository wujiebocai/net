#pragma once

#include "base/iopool.hpp"
#include "base/error.hpp"
#include "tool/bytebuffer.hpp"

namespace net {
	template<class DRIVERTYPE, class SOCKETTYPE, class STREAMTYPE, class PROTOCOLTYPE, class SVRORCLI = svr_tab>
	class TransferData {
	public:
		TransferData(std::size_t max_buffer_size) 
			: derive_(static_cast<DRIVERTYPE&>(*this))
			, buffer_(max_buffer_size) {}

		~TransferData() = default;

		inline bool send(const std::string&& data) {
			if constexpr (is_tcp_socket_v<SOCKETTYPE>) { // tcp
				return this->send_t(std::move(data));
			}
			else if constexpr (is_udp_socket_v<SOCKETTYPE>) { //udp
				if constexpr (is_cli_v<SVRORCLI>) {
					return this->send_t(std::move(data));
				}
				else {
					return this->send_t(derive_.remote_endpoint(), std::move(data));
				}
			}
			return false;
		}

		inline void do_recv() {
			this->do_recv_t<SOCKETTYPE>();
		}

	protected:
		inline bool send_t(const std::string&& data) {
			try {
				if (!this->derive_.is_started())
					asio::detail::throw_error(asio::error::not_connected);
				if (data.length() <= 0)
					asio::detail::throw_error(asio::error::invalid_argument);

				return this->send_enqueue([this,
					data = std::move(data)]() mutable {
					return this->do_send<SOCKETTYPE>(data, [](const error_code&, std::size_t) {});
				});
			}
			catch (system_error& e) { set_last_error(e); }
			catch (std::exception&) { set_last_error(asio::error::eof); }
			return false;
		}

		template<class Endpoint, typename = std::enable_if_t<std::is_same_v<unqualified_t<Endpoint>, asio::ip::udp::endpoint>>>
		inline bool send_t(Endpoint&& endpoint, const std::string&& data) {
			try {
				if (!this->derive_.is_started())
					asio::detail::throw_error(asio::error::not_connected);
				if (data.length() <= 0)
					asio::detail::throw_error(asio::error::invalid_argument);

				return this->send_enqueue([this,
					endpoint = std::forward<Endpoint>(endpoint),
					data = std::move(data)]() mutable {
					return this->do_send(endpoint, data, [](const error_code&, std::size_t) {});
				});
			}
			catch (system_error& e) { set_last_error(e); }
			catch (std::exception&) { set_last_error(asio::error::eof); }
			
			return false;
		}

		inline bool send_t(std::string&& host, std::string&& port, const std::string&& data) {
			try {
				if (!this->derive_.is_started())
					asio::detail::throw_error(asio::error::not_connected);
				if (data.length() <= 0)
					asio::detail::throw_error(asio::error::invalid_argument);

				return this->do_resolve_send(std::forward<std::string>(host), std::forward<std::string>(port),
					std::forward<std::string>(data), [](const error_code&, std::size_t) {});
			}
			catch (system_error& e) { set_last_error(e); }
			catch (std::exception&) { set_last_error(asio::error::eof); }

			return false;
		}

	protected:
		/*
		desc: tcp recv data
		condition:
			transfer_all:直到buff满才返回.
			transfer_at_least:buff满或者至少读取参数设定字节数返回.
			transfer_exactly:buff满或者正好读取参数设定字节数返回.
		*/
		template<class TSOCKETTYPE, std::enable_if_t<is_tcp_socket_v<TSOCKETTYPE>, bool> = true>
		inline void do_recv_t() {
			if (!this->derive_.is_started())
				return;
			try {
				asio::async_read(this->derive_.stream(), this->buffer_, asio::transfer_at_least(1),
					asio::bind_executor(derive_.cio().strand(),
						[this, selfptr = this->derive_.self_shared_ptr()](const error_code& ec, std::size_t bytes_recvd)
				{
					set_last_error(ec);
					if (!ec) {
						this->derive_.handle_recv(ec, std::string(reinterpret_cast<
							std::string::const_pointer>(this->buffer_.data().data()), bytes_recvd));

						this->buffer_.consume(bytes_recvd);

						this->do_recv_t<TSOCKETTYPE>();
					}
					else {
						this->derive_.stop(ec);
					}
				}));
			}
			catch (system_error& e) {
				set_last_error(e);
				this->derive_.stop(e.code());
			}
		}
		/*
		desc: udp recv data
		*/
		template<class USOCKETTYPE, std::enable_if_t<is_udp_socket_v<USOCKETTYPE>, bool> = true>
		inline void do_recv_t() {
			if constexpr (is_svr_v<SVRORCLI>) {
				return;
			}
			if (!this->derive_.is_started())
				return;
			try {
				this->ubuffer_.wr_reserve(init_buffer_size_);
				this->derive_.stream().async_receive(asio::mutable_buffer(this->ubuffer_.wr_buf(), this->ubuffer_.wr_size()),
					asio::bind_executor(derive_.cio().strand(), 
						[this, selfptr = this->derive_.self_shared_ptr()](const error_code& ec, std::size_t bytes_recvd)
				{
					if (ec == asio::error::operation_aborted) {
						this->derive_.stop(ec);
						return;
					}
					this->ubuffer_.wr_flip(bytes_recvd);
					this->derive_.handle_recv(ec, std::string(reinterpret_cast<
						std::string::const_pointer>(this->ubuffer_.rd_buf()), bytes_recvd));

					this->ubuffer_.reset();

					this->do_recv_t<USOCKETTYPE>();
				}));
			}
			catch (system_error& e) {
				set_last_error(e);
				this->derive_.stop(e.code());
			}
			/*asio::post(this->derive_.cio().strand(), [this]()
			{
				
			});*/
		}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/*
		desc: send data
		*/
		template<class TSOCKETTYPE, bool IsAsync = true, class Data, class Callback, std::enable_if_t<is_tcp_socket_v<TSOCKETTYPE>, bool> = true>
		inline bool do_send(Data&& buffer, Callback&& callback) {
			if constexpr (IsAsync) {
				asio::async_write(this->derive_.stream(), asio::buffer(buffer), asio::bind_executor(this->derive_.cio().strand(),
					[this, p = this->derive_.self_shared_ptr(), callback = std::forward<Callback>(callback)]
				(const error_code& ec, std::size_t bytes_sent) mutable {
					set_last_error(ec);
					callback(ec, bytes_sent);
					if (ec) {
						this->derive_.stop(ec);
					}
					this->send_dequeue();
				}));
				return true;
			}
			else {
				error_code ec;
				std::size_t bytes_sent = asio::write(this->derive_.stream(), buffer, ec);
				set_last_error(ec);
				callback(ec, bytes_sent);
				if (ec) {
					this->derive_.stop(ec);
				}
				return (!bool(ec));
			}
		}

		template<class USOCKETTYPE, bool IsAsync = true, class Data, class Callback, std::enable_if_t<is_udp_socket_v<USOCKETTYPE>, bool> = true>
		inline bool do_send(Data&& data, Callback&& callback) {
			if constexpr (is_kcp_streamtype_v<STREAMTYPE>) {
				return kcp_do_send(data, callback);
			}
			if constexpr (IsAsync) {
				this->derive_.stream().async_send(asio::buffer(std::forward<Data>(data)), asio::bind_executor(this->derive_.cio().strand(),
					[this, p = this->derive_.self_shared_ptr(), callback = std::forward<Callback>(callback)]
				(const error_code& ec, std::size_t bytes_sent) mutable {
					set_last_error(ec);

					callback(ec, bytes_sent);
					
					this->send_dequeue();
				}));
				return true;
			}
			else {
				error_code ec;
				std::size_t bytes_sent = this->derive_.stream().send(asio::buffer(data), 0, ec);
				set_last_error(ec);
				callback(ec, bytes_sent);
				return (!bool(ec));
			}
		}
		template<bool IsAsync = true, class Endpoint, class Data, class Callback, typename = std::enable_if_t<std::is_same_v<unqualified_t<Endpoint>, asio::ip::udp::endpoint>>>
		inline bool do_send(Endpoint& endpoint, Data& data, Callback&& callback) {
			if constexpr (is_kcp_streamtype_v<STREAMTYPE>) {
				return kcp_do_send(data, callback);
			}
			if constexpr (IsAsync) {
				this->derive_.stream().async_send_to(asio::buffer(data), endpoint, asio::bind_executor(this->derive_.cio().strand(),
					[this, p = this->derive_.self_shared_ptr(), callback = std::forward<Callback>(callback)]
				(const error_code& ec, std::size_t bytes_sent) mutable {
					set_last_error(ec);

					callback(ec, bytes_sent);

					this->send_dequeue();
				}));
				return true;
			}
			else {
				error_code ec;
				std::size_t bytes_sent = this->derive_.stream().send_to(asio::buffer(data), endpoint, 0, ec);
				set_last_error(ec);
				callback(ec, bytes_sent);
				return (!bool(ec));
			}
		}
		template<typename Data, typename Callback>
		inline bool do_resolve_send(std::string&& host, std::string&& port, Data&& data, Callback&& callback) {
			using resolver_type = asio::ip::udp::resolver;
			using endpoints_type = typename resolver_type::results_type;

			std::unique_ptr<resolver_type> resolver_ptr = std::make_unique<resolver_type>(this->wio_.context());

			resolver_type* resolver_pointer = resolver_ptr.get();
			resolver_pointer->async_resolve(std::forward<std::string>(host), std::forward<std::string>(port),
				asio::bind_executor(this->derive_.cio().strand(),
					[this, p = this->derive_.self_shared_ptr(), resolver_ptr = std::move(resolver_ptr),
					data = std::forward<Data>(data), callback = std::forward<Callback>(callback)]
			(const error_code& ec, const endpoints_type& endpoints) mutable {
				set_last_error(ec);

				if (ec) {
					callback(ec, 0);
				}
				else {
					decltype(endpoints.size()) i = 1;
					for (auto iter = endpoints.begin(); iter != endpoints.end(); ++iter, ++i)
					{
						this->send_enqueue([this, endpoint = iter->endpoint(),
							data = (endpoints.size() == i ? std::move(data) : data),
							callback = (endpoints.size() == i ? std::move(callback) : callback)]() mutable {
							return this->do_send(endpoint, data, std::move(callback));
						});
					}
				}
			}));
			return true;
		}
////////////////////////////////////KCP////////////////////////////////////////////////////////////////////////
	public:
		inline auto& ubuffer() { return ubuffer_; }
		template<bool IsAsync = true, class Data, class Callback>
		inline bool kcp_do_send(Data& data, Callback&& callback) {
			auto pkcp = this->derive_.kcp();
			if (!pkcp) {
				return false;
			}
			auto buffer = asio::buffer(data);

			int ret = kcp::ikcp_send(pkcp, (const char*)buffer.data(), (int)buffer.size());
			set_last_error(ret);
			if (ret == 0)
				kcp::ikcp_flush(pkcp);
			callback(get_last_error(), ret < 0 ? 0 : buffer.size());

			if constexpr (IsAsync) {
				this->send_queue_.pop();
			}

			return (ret == 0);
		}
		inline std::size_t kcp_send_hdr(kcp::kcphdr hdr, error_code ec) {
			std::size_t sent_bytes = 0;
			if constexpr (is_svr_v<SVRORCLI>)
				sent_bytes = this->derive_.stream().send_to(
					asio::buffer((const void*)&hdr, sizeof(kcp::kcphdr)),
					this->derive_.remote_endpoint(), 0, ec);
			else
				sent_bytes = this->derive_.stream().send(
					asio::buffer((const void*)&hdr, sizeof(kcp::kcphdr)), 0, ec);
			return sent_bytes;
		}
		inline void kcp_do_recv_t(const std::string& s) {
			auto pkcp = this->derive_.kcp();
			if (!pkcp) {
				return;
			}
			int len = kcp::ikcp_input(pkcp, s.c_str(), (long)s.size());
			ubuffer_.reset();
			if (len != 0) {
				set_last_error(asio::error::no_data);
				this->derive_.stop(asio::error::no_data);
				return;
			}
			for (;;) {
				len = kcp::ikcp_recv(pkcp, (char*)ubuffer_.wr_buf(), ubuffer_.wr_size());
				if (len >= 0) {
					ubuffer_.wr_flip(len);
					/*this->derive_.handle_recv(ec_ignore, std::string(reinterpret_cast<
						std::string::const_pointer>(ubuffer_.rd_buf()), len));*/
					this->derive_.cbfunc()->call(Event::recv, this->derive_.self_shared_ptr(), std::string(reinterpret_cast<
						std::string::const_pointer>(ubuffer_.rd_buf()), len));
					ubuffer_.rd_flip(len);
				}
				else if (len == -3) {
					ubuffer_.wr_reserve(init_buffer_size_);
				}
				else break;
			}
			kcp::ikcp_flush(pkcp);
		}
		/*inline void kcp_handle_recv(const error_code& ec, const std::string& s) {
			if (ec || !this->derive_.is_started())
				return;
			auto pkcp = this->derive_.kcp();
			if (!pkcp) {
				return;
			}
			if constexpr (is_svr_v<SVRORCLI>) {
				if (s.size() == sizeof(kcp::kcphdr)) {
					if (kcp::is_kcphdr_fin(s)) {
						this->derive_.set_send_fin(false);
						this->derive_.stop(asio::error::eof);
					}
					// Check whether the packet is SYN handshake
					// It is possible that the client did not receive the synack package, then the client
					// will resend the syn package, so we just need to reply to the syncack package directly.
					else if (kcp::is_kcphdr_syn(s)) {
						NET_ASSERT(pkcp);
						// step 4 : server send synack to client
						kcp::kcphdr* hdr = (kcp::kcphdr*)(s.data());
						kcp::kcphdr synack = kcp::make_kcphdr_synack(this->derive_.kcp_seq(), hdr->th_seq);
						error_code ed;
						this->kcp_send_hdr(synack, ed);
						if (ed)
							this->derive_.stop(ed);
					}
				}
				else
					this->kcp_do_recv_t(s);
			}
			else {
				if (s.size() == sizeof(kcp::kcphdr)) {
					if (kcp::is_kcphdr_fin(s)) {
						this->derive_.set_send_fin(false);
						this->derive_.stop(asio::error::eof);
					}
					else if (kcp::is_kcphdr_synack(s, this->derive_.kcp_seq())) {
						//NET_ASSERT(false);
						this->derive_.stop(asio::error::operation_aborted);
					}
				}
				else
					this->kcp_do_recv_t(s);
			}
			
		}*/
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
	protected:
		//线程安全，保证执行得时序性.
		template<bool IsAsync = true, class Callback>
		inline bool send_enqueue(Callback&& f) {
			if constexpr (IsAsync) {
				if (this->derive_.cio().strand().running_in_this_thread()) {
					bool empty = this->send_queue_.empty();
					this->send_queue_.emplace(std::forward<Callback>(f));
					if (empty) {
						return (this->send_queue_.front())();
					}
					return true;
				}
				asio::post(this->derive_.cio().strand(),
					[this, p = this->derive_.self_shared_ptr(), f = std::forward<Callback>(f)]() mutable {
					bool empty = this->send_queue_.empty();
					this->send_queue_.emplace(std::move(f));
					if (empty) {
						(this->send_queue_.front())();
					}
				});
				return true;
			}
			else {
				if (this->derive_.cio().strand().running_in_this_thread()) {
					return f();
				}
				asio::post(this->derive_.cio().strand(),
					[this, p = this->derive_.self_shared_ptr(), f = std::forward<Callback>(f)]() mutable
				{
					f();
				});
				return true;
			}
		}
		//非线程安全
		inline void send_dequeue() {
			NET_ASSERT(this->derive_.cio().strand().running_in_this_thread());
			if (!this->send_queue_.empty()) {
				this->send_queue_.pop();
				if (!this->send_queue_.empty()) {
					(this->send_queue_.front())();
				}
			}
		}

	protected:
		DRIVERTYPE& derive_;
		std::queue<std::function<bool()>>  send_queue_;

		asio::streambuf buffer_;

		t_buffer_cmdqueue<> ubuffer_;
		std::size_t init_buffer_size_ = 1024;
	};
}

