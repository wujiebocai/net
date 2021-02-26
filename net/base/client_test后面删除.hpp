#pragma once

#include <memory>
#include <future>
#include <string_view>
#include <string>

#include "net/base/iopool.hpp"
#include "net/base/error.hpp"
#include "net/base/cb_event.hpp"
#include "net/base/timer.hpp"
#include "net/base/transfer_data.hpp"

namespace net {
	// SOCKETTYPE : tcp or udp
	template<class SOCKETTYPE, class STREAMTYPE = void, class PROTOCOLTYPE = void>
	class Client : public IoPoolImp
				 , public Socket<SOCKETTYPE>
				 , public TransferData<Client<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, SOCKETTYPE, PROTOCOLTYPE>
				 , public std::enable_shared_from_this<Client<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>>
		{
	public:
		using client_type = Client<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		using client_type_ptr = std::shared_ptr<client_type>;
		using socket_type = Socket<SOCKETTYPE>;
		using resolver_type = typename asio::ip::basic_resolver<typename SOCKETTYPE::protocol_type>;
		using endpoints_type = typename resolver_type::results_type;
		using endpoint_type = typename SOCKETTYPE::lowest_layer_type::endpoint_type;
		using endpoints_iterator = typename endpoints_type::iterator;
	public:
		template<class ...Args>
		explicit Client(std::size_t concurrency, std::size_t max_buffer_size, Args&&... args)
			: IoPoolImp(concurrency)
			, socket_type(iopool_.get(0).context(), std::forward<Args>(args)...)
			, cio_(iopool_.get(0))
			, cbfunc_(std::make_shared<CBPROXYTYPE>())
			, buffer_(max_buffer_size)
			, ctimer_(cio_)
		{
			this->iopool_.start();
		}

		~Client() {
			this->iopool_.stop();
			this->stop(asio::error::operation_aborted);
		}

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
			auto handlefunc = [this](const error_code& ec, client_type_ptr self_ptr, State old_state) {
				asio::post(this->cio_.strand(), [this, ec, this_ptr = std::move(self_ptr), old_state]() {
					set_last_error(ec);

					State expected = State::stopping;
					if (this->state_.compare_exchange_strong(expected, State::stopped)) {
						cbfunc_->call(Event::disconnect, this_ptr, ec);
					}
					else {
						NET_ASSERT(false);
					}
					this->user_data_reset();
					this->socket_.close();
				});
			};
			State expected = State::starting;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return handlefunc(ec, this->shared_from_this(), expected);

			expected = State::started;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return handlefunc(ec, this->shared_from_this(), expected);
		}

		// tcp connect
		template<bool isAsync = true, bool isKeepAlive = false, typename = std::enable_if_t<is_tcp_socket_v<SOCKETTYPE>>>
		bool connect(const std::string_view& host, const std::string_view& port) {
			try {
				this->host_ = host;
				this->port_ = port;

				auto & socket = this->socket().lowest_layer();

				socket.close(ec_ignore);
				socket.open(this->endpoint().protocol());
				socket.set_option(typename SOCKETTYPE::reuse_address(true));

				if constexpr (isKeepAlive)
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

				State expected = State::starting;
				if (!ec && !this->state_.compare_exchange_strong(expected, State::started))
					ec = asio::error::operation_aborted;
				
				set_last_error(ec);

				auto dptr = this->shared_from_this();

				cbfunc_->call(Event::connect, dptr, ec);

				asio::detail::throw_error(ec);

				if (!ec) {
					this->do_recv();
				}
			}
			catch (system_error & e) {
				set_last_error(e);
				this->stop(e.code());
			}
		}

		template<class ...Args>
		bool bind(Args&&... args) {
			return cbfunc_->bind(std::move(args)...);
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
		inline asio::streambuf& buffer() { return buffer_; }
		inline NIO& cio() { return cio_; }
		inline void handle_recv(client_type_ptr dptr, std::string_view&& s) {
			cbfunc_->call(Event::recv, dptr, s);
		}
		inline bool is_started() const {
			return (this->state_ == State::started && this->socket_.lowest_layer().is_open());
		}
		inline bool is_stopped() const {
			return (this->state_ == State::stopped && !this->socket_.lowest_layer().is_open());
		}

		template<class DataT>
		inline void user_data(DataT&& data) {
			this->user_data_ = std::forward<DataT>(data);
		}
		template<class DataT>
		inline DataT* user_data() {
			try {
				return &(std::any_cast<DataT>(this->user_data_));
			}
			catch (const std::bad_any_cast&) {}
			return nullptr;
		}
		inline void user_data_reset() {
			this->user_data_.reset();
		}
	protected:
		NIO & cio_; //

		endpoint_type endpoint_;

		endpoints_type endpoints_;

		std::string host_, port_;

		std::atomic<State> state_ = State::stopped;

		asio::streambuf buffer_;

		Timer ctimer_; 

		FuncProxyImpPtr cbfunc_;

		std::any user_data_;
	};
}