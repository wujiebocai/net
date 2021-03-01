#pragma once

#include <memory>
#include <future>
#include <string_view>

#include "base/iopool.hpp"
#include "base/session.hpp"
#include "base/error.hpp"
#include "base/timer.hpp"

namespace net {
	template<class SOCKETTYPE, class STREAMTYPE = void, class PROTOCOLTYPE = void>
	class Client : public IoPoolImp
				 , public std::enable_shared_from_this<Client<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>> {
	public:
		//using client_type = Client<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		//using client_type_ptr = std::shared_ptr<client_type>;
		using resolver_type = typename asio::ip::basic_resolver<typename SOCKETTYPE::protocol_type>;
		using endpoints_type = typename resolver_type::results_type;
		using endpoint_type = typename SOCKETTYPE::lowest_layer_type::endpoint_type;
		using endpoints_iterator = typename endpoints_type::iterator;
		using session_type = Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		using session_ptr_type = std::shared_ptr<session_type>;
	public:
		template<class ...Args>
		explicit Client(std::size_t concurrency = std::thread::hardware_concurrency() * 2, std::size_t max_buffer_size = (std::numeric_limits<std::size_t>::max)())
			: IoPoolImp(concurrency)
			, cio_(iopool_.get(0))
			, cbfunc_(std::make_shared<CBPROXYTYPE>())
			, sessions_(cio_)
			, max_buffer_size_(max_buffer_size)
			, ctimer_(cio_)
			, streamcxt_(client_place{})
		{
			this->iopool_.start();
		}

		~Client() {
			this->iopool_.stop();
			this->stop(asio::error::operation_aborted);
		}

		template<bool isAsync = true, bool isKeepAlive = false, typename = std::enable_if_t<is_tcp_socket_v<SOCKETTYPE>>>
		inline bool add(std::string_view host, std::string_view port) {
			clear_last_error();
			return this->template connect<isAsync, isKeepAlive>(host, port);
		}

		inline bool start() {
			return true;
		}

		inline typename std::enable_if_t<is_tcp_socket_v<SOCKETTYPE>>
		stop(const error_code& ec) {
			asio::post(this->cio_.strand(), [this, ec, this_ptr = this->shared_from_this()]() {
				set_last_error(ec);

				this->sessions_.foreach([this, ec](session_ptr_type& session_ptr) {
					session_ptr->stop(ec);
				});
			});
		}

		template<class ...Args>
		bool bind(Args&&... args) {
			return cbfunc_->bind(std::move(args)...);
		}
		auto& get_netstream() { return streamcxt_; }

		//广播所有session
		inline void send(const std::string_view && data) {
			this->sessions_.foreach([&data](session_ptr_type& session_ptr) {
				session_ptr->send(data);
			});
		}

		inline session_ptr_type find_session_if(const std::function<bool(session_ptr_type&)> & fn) {
			return session_ptr_type(this->sessions_.find_if(fn));
		}
	protected:
		// tcp connect
		template<bool isAsync = true, bool isKeepAlive = false, typename = std::enable_if_t<is_tcp_socket_v<SOCKETTYPE>>>
		inline bool connect(const std::string_view& host, const std::string_view& port) {
			std::shared_ptr<session_type> session_ptr = this->make_session();
			try {
				this->host_ = host;
				this->port_ = port;

				auto & socket = session_ptr->socket().lowest_layer();

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

				auto dptr = session_ptr;//this->shared_from_this();

				std::future<error_code> future = ctimer_.post_timeout_timer_test(std::chrono::seconds(5), [this, dptr](error_code ec) mutable {
					if (!ec) {
						ec = asio::error::timed_out;
						this->handle_connect(ec, std::move(dptr));
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
						this->handle_connect(ec, std::move(dptr));
					else
						this->post_connect(ec, this->endpoints_.begin(), std::move(dptr));
				}));

				if constexpr (isAsync)
					return true;
				else {
					ctimer_.wait_timeout_timer(future);
					return true;
				}
			}
			catch (system_error & e) {
				set_last_error(e);
				this->handle_connect(e.code(), session_ptr);
			}
			return false;
		}

		inline void post_connect(error_code ec, endpoints_iterator iter, std::shared_ptr<session_type> dptr) {
			try {
				if (iter == this->endpoints_.end()) {
					this->handle_connect(ec ? ec : asio::error::host_unreachable, std::move(dptr));
					return;
				}
				// Start the asynchronous connect operation.
				dptr->socket().lowest_layer().async_connect(iter->endpoint(),
					asio::bind_executor(cio_.strand(),
					[this, iter, dptr](const error_code & ec) mutable {
					set_last_error(ec);
					if (ec && ec != asio::error::operation_aborted)
						this->post_connect(ec, ++iter, std::move(dptr));
					else
						this->handle_connect(ec, std::move(dptr));
				}));
			}
			catch (system_error & e) {
				set_last_error(e);
				this->handle_connect(ec, std::move(dptr));
			}
		}

		inline void handle_connect(error_code ec, std::shared_ptr<session_type> dptr) {
			try {
				ctimer_.stop();
				
				set_last_error(ec);

				//cbfunc_->call(Event::connect, dptr, ec);

				asio::detail::throw_error(ec);

				dptr->start(ec);
			}
			catch (system_error & e) {
				set_last_error(e);
				dptr->stop(e.code());
			}
		}

		inline session_ptr_type make_session() {
			auto& cio = this->iopool_.get();
			if constexpr (is_ssl_streamtype_v<STREAMTYPE>) {
				return std::make_shared<session_type>(this->sessions_, this->cbfunc_, cio, this->max_buffer_size_
							, cio, streamcxt_, asio::ssl::stream_base::client, cio.context());
			}
			if constexpr (is_binary_streamtype_v<STREAMTYPE>) {
				return std::make_shared<session_type>(this->sessions_, this->cbfunc_, cio, this->max_buffer_size_
							, cio.context());
			}

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
	protected:
		SessionMgr<session_type> sessions_;
		NIO & cio_; //

		endpoint_type endpoint_;

		endpoints_type endpoints_;

		std::string host_, port_;

		std::size_t max_buffer_size_;

		//std::atomic<State> state_ = State::stopped;

		Timer ctimer_; 

		FuncProxyImpPtr cbfunc_;

		NetStream<SOCKETTYPE, STREAMTYPE> streamcxt_;
	};
}