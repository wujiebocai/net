#pragma once

#include <memory>
#include <future>
#include <string_view>
#include <string>

#include "base/iopool.hpp"
#include "base/error.hpp"
#include "base/session_mgr.hpp"
#include "base/stream.hpp"
#include "base/timer.hpp"
#include "base/transfer_data.hpp"
#include "tool/bytebuffer.hpp"

namespace net {
	template<class SOCKETTYPE, class STREAMTYPE = void, class PROTOCOLTYPE = void>
	class CSession : public StreamType<CSession<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, SOCKETTYPE, STREAMTYPE, cli_tab>
				   , public TransferData<CSession<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE, cli_tab>
				   , public std::enable_shared_from_this<CSession<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>> {
	public:
		using session_type = CSession<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		using session_ptr_type = std::shared_ptr<session_type>;
		using stream_type = StreamType<session_type, SOCKETTYPE, STREAMTYPE, cli_tab>;
		using transferdata_type = TransferData<CSession<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE, cli_tab>;
		using resolver_type = typename asio::ip::basic_resolver<typename SOCKETTYPE::protocol_type>;
		using endpoints_type = typename resolver_type::results_type;
		using endpoint_type = typename SOCKETTYPE::lowest_layer_type::endpoint_type;
		using endpoints_iterator = typename endpoints_type::iterator;
		using key_type = typename std::conditional<is_udp_socket_v<SOCKETTYPE>, asio::ip::udp::endpoint, std::size_t>::type;
	public:
		template<class ...Args>
		explicit CSession(SessionMgr<session_type>& sessions, FuncProxyImpPtr& cbfunc, NIO& io,
						std::size_t max_buffer_size, Args&&... args)
			: stream_type(std::forward<Args>(args)...)
			, transferdata_type(max_buffer_size)
			, cio_(io)
			, cbfunc_(cbfunc)
			, sessions_(sessions)
			, ctimer_(cio_)
		{
		}

		~CSession() = default;

		template<bool isAsync = true, bool isKeepAlive = false>
		bool start(std::string_view host, std::string_view port) {
			State expected = State::stopped;
			if (!this->state_.compare_exchange_strong(expected, State::starting)) {
				set_last_error(asio::error::already_started);
				return false;
			}
			try {
				clear_last_error();

				return this->template connect<isAsync, isKeepAlive>(host, port);
			}
			catch (system_error& e) {
				set_last_error(e);
				this->stop(e.code());
			}
			return false;
		}

		inline void stop(const error_code& ec) {
			auto handlefunc = [this](const error_code& ec, session_ptr_type sessionptr, State oldstate) {
				asio::post(this->cio_.strand(), [this, ec, dptr = std::move(sessionptr), oldstate]() {
					set_last_error(ec);

					this->user_data_reset();
					this->stream_stop(dptr);
					State expected = State::stopping;
					if (this->state_.compare_exchange_strong(expected, State::stopped)) {
						cbfunc_->call(Event::disconnect, dptr, ec);
					}
					else {
						NET_ASSERT(false);
					}
					//从sessionmgr移除
					bool isremove = this->sessions_.erase(dptr);
					if (!isremove) {
						return;
					}
				});
			};
			State expected = State::starting;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return handlefunc(ec, this->shared_from_this(), expected);

			expected = State::started;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return handlefunc(ec, this->shared_from_this(), expected);
		}

		// 重连机制
		inline bool
		reconn() {
			if (!is_stopped()) {
				return false;
			}
			ctimer_.post_timer<false>(3 * 1000, [this, dptr = this->shared_from_this()](const error_code& ec) {
				auto ret = this->start<false>(this->host_, this->port_);
				//std::cout << "reconn:" << ret << std::endl;
			});
			return true;
		}

		//asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), 1234);
		//asio::ip::udp::endpoint ep(asio::ip::udp::v6(), 9876);
		//asio::ip::tcp::endpoint ep(asio::ip::address_v4::from_string("..."), 1234);
		//asio::ip::udp::endpoint ep(asio::ip::address_v6::from_string("..."), 9876);
		template<typename IP>
		inline void endpoint(const IP& ip, unsigned short port) {
			this->endpoint_ = endpoint_type(ip, port);
		}
		inline endpoint_type& endpoint() { return this->endpoint_; }

		//imp
		inline auto self_shared_ptr() { return this->shared_from_this(); }
		//inline asio::streambuf& buffer() { return buffer_; }
		inline NIO& cio() { return cio_; }
		inline void handle_recv(error_code ec, std::string&& s) {
			if constexpr (is_kcp_streamtype_v<STREAMTYPE>) {
				return this->kcp_handle_recv(ec, s);
			}
			cbfunc_->call(Event::recv, this->self_shared_ptr(), std::move(s));
		}
		inline auto& cbfunc() { return cbfunc_; }
		inline t_buffer_cmdqueue<>& rbuffer() { return rbuff_; }

		inline bool is_started() const {
			return (this->state_ == State::started && this->socket_.lowest_layer().is_open());
		}
		inline bool is_stopped() const {
			return (this->state_ == State::stopped && !this->socket_.lowest_layer().is_open());
		}
		inline const key_type hash_key() {
			if constexpr (is_tcp_socket_v<SOCKETTYPE>) {
				return reinterpret_cast<key_type>(this);
			}
			else if constexpr (is_udp_socket_v<SOCKETTYPE>) {
				//return this->remote_endpoint_;
				return this->stream().lowest_layer().local_endpoint();
			}
		}

		template<class DataT>
		inline void user_data(DataT&& data) {
			this->user_data_ = std::forward<DataT>(data);
		}
		template<class DataT>
		inline DataT* user_data() {
			try {
				return std::any_cast<DataT>(&this->user_data_);
			}
			catch (const std::bad_any_cast&) {}
			return nullptr;
		}
		inline void user_data_reset() {
			this->user_data_.reset();
		}
	protected:
		// tcp connect
		template<bool isAsync = true, bool isKeepAlive = false>
		bool connect(const std::string_view& host, const std::string_view& port) {
			try {
				this->host_ = host;
				this->port_ = port;

				auto & socket = this->socket().lowest_layer();

				socket.close(ec_ignore);
				socket.open(this->endpoint().protocol());
				socket.set_option(typename SOCKETTYPE::reuse_address(true));

				if constexpr (isKeepAlive && is_tcp_socket_v<SOCKETTYPE>)
					this->keep_alive_options();
				else
					std::ignore = true;

				//初始化事件回调
				cbfunc_->call(Event::init);

				socket.bind(this->endpoint());

				auto dptr = this->shared_from_this();

				// 开始连接超时计时器
				std::future<error_code> future = ctimer_.post_timeout_timer_test(std::chrono::seconds(5), [this, dptr](error_code ec) mutable {
					if (!ec) {
						ec = asio::error::timed_out;
						this->handle_connect(ec);
					}
				});

				std::unique_ptr<resolver_type> resolver_ptr = std::make_unique<resolver_type>(cio_.context());
				//在async_resolve执行完成之前，我们必须保留resolver对象。
				//因此，我们将resolver_ptr捕获到了lambda回调函数中。
				resolver_type * resolver_pointer = resolver_ptr.get();
				resolver_pointer->async_resolve(host, port, asio::bind_executor(cio_.strand(),
					[this, dptr, resolver_ptr = std::move(resolver_ptr)]
				(const error_code& ec, const endpoints_type& endpoints) {
					set_last_error(ec);
					this->endpoints_ = endpoints;
					if (ec)
						this->handle_connect(ec);
					else
						this->post_connect(ec, this->endpoints_.begin());
				}));

				if constexpr (isAsync)
					return true;
				else {
					ctimer_.wait_timeout_timer(future);
					return this->is_started();
				}
			}
			catch (system_error & e) {
				set_last_error(e);
				this->handle_connect(e.code());
			}
			return false;
		}

		inline void post_connect(error_code ec, endpoints_iterator iter) {
			try {
				auto dptr = this->shared_from_this();
				if (iter == this->endpoints_.end()) {
					this->handle_connect(ec ? ec : asio::error::host_unreachable);
					return;
				}
				// Start the asynchronous connect operation.
				dptr->socket().lowest_layer().async_connect(iter->endpoint(),
					asio::bind_executor(cio_.strand(),
					[this, iter, dptr](const error_code & ec) mutable {
					set_last_error(ec);
					if (ec && ec != asio::error::operation_aborted)
						this->post_connect(ec, ++iter);
					else
						this->handle_connect(ec);
				}));
			}
			catch (system_error & e) {
				set_last_error(e);
				this->handle_connect(ec);
			}
		}

		inline void handle_connect(error_code ec) {
			try {
				ctimer_.stop();
				asio::detail::throw_error(ec);

				const auto& dptr = this->shared_from_this();
				this->stream_post_handshake(dptr, [this, dptr = this->shared_from_this()](const error_code& ec) {
					try {
						//cbfunc_->call(Event::handshake, dptr, ec);

						State expected = State::starting;
						if (!ec && !this->state_.compare_exchange_strong(expected, State::started))
							asio::detail::throw_error(asio::error::operation_aborted);

						cbfunc_->call(Event::connect, dptr, ec);

						asio::detail::throw_error(ec);

						//加入到sessionmgr
						bool isadd = this->sessions_.emplace(dptr);
						if (isadd)
							this->do_recv();
						else
							this->stop(asio::error::address_in_use);
					}
					catch (system_error& e) {
						set_last_error(e);
						this->stop(e.code());
					}
				});
			}
			catch (system_error & e) {
				set_last_error(e);
				this->stop(e.code());
			}
		}
	protected:
		NIO & cio_;

		endpoint_type endpoint_;
		endpoints_type endpoints_;
		std::string host_, port_;

		std::atomic<State> state_ = State::stopped;

		SessionMgr<session_type>& sessions_;

		FuncProxyImpPtr cbfunc_;

		Timer ctimer_;

		t_buffer_cmdqueue<> rbuff_;

		std::any user_data_;
	};
}