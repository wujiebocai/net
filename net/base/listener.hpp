#pragma once

#include "net/base/iopool.hpp"
#include "net/base/error.hpp"
#include "net/base/session.hpp"

namespace net {
	template<class SERVERTYPE, class SESSIONTYPE>
	class Listener {
	public:
		Listener(NIO& io) 
			: cio_(io)
			, acceptor_(this->cio_.context())
			, server_(static_cast<SERVERTYPE&>(*this))
			, acceptor_timer_(this->cio_.context())
		{}

		~Listener() {
			this->listener_stop();
		}

		inline bool listener_start(std::string_view host, std::string_view port) {
			try {
				clear_last_error();

				this->acceptor_.close(ec_ignore);
				// parse address and port
				asio::ip::tcp::resolver resolver(this->cio_.context());
				asio::ip::tcp::endpoint endpoint = *resolver.resolve(host, port,
					asio::ip::resolver_base::flags::passive | asio::ip::resolver_base::flags::address_configured).begin();

				this->acceptor_.open(endpoint.protocol());
				this->acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true)); // set port reuse
				//this->acceptor_->set_option(asio::ip::tcp::no_delay(true));
				//this->acceptor_->non_blocking(true);

				this->acceptor_.bind(endpoint);
				this->acceptor_.listen();

				asio::post(this->cio_.strand(), [this]() {
					this->post_accept();
				});
				return true;
			}
			catch (system_error & e) {
				set_last_error(e);
				//this->listener_stop();
				this->server_.stop(e.code());
			}
			return false;
		}

		inline void post_accept() {
			if (!this->is_lis_open())
				return;
			try {
				std::shared_ptr<SESSIONTYPE> session_ptr = this->server_.make_session();

				auto & socket = session_ptr->socket().lowest_layer();
				this->acceptor_.async_accept(socket, asio::bind_executor(this->cio_.strand(),
					[this, session_ptr = std::move(session_ptr)](const error_code & ec)
				{
					set_last_error(ec);
					if (ec == asio::error::operation_aborted) {
						//this->listener_stop();
						this->server_.stop(ec);
						return;
					}
					if (!ec) {
						if (this->server_.is_started()) {
							session_ptr->start(ec);
						}
					}
					this->post_accept();
				}));
			}
			catch (system_error & e) {
				set_last_error(e);
				// 处理打开文件太多导致的异常问题
				this->acceptor_timer_.expires_after(std::chrono::seconds(1));
				this->acceptor_timer_.async_wait(asio::bind_executor(this->cio_.strand(),
					[this](const error_code & ec) {
					set_last_error(ec);
					if (ec) {
						//this->listener_stop();
						this->server_.stop(ec);
						return;
					}
					asio::post(this->cio_.strand(), [this]() {
						this->post_accept();
					});
				}));
			}
		}

		inline void listener_stop() {
			this->acceptor_timer_.cancel();
			this->acceptor_.close(ec_ignore);
		}

		inline bool is_lis_open() const { return this->acceptor_.is_open(); }

		inline std::string listen_address() {
			try {
				return this->acceptor_.local_endpoint().address().to_string();
			}
			catch (system_error & e) { set_last_error(e); }
			return std::string();
		}

		inline unsigned short listen_port() {
			try {
				return this->acceptor_.local_endpoint().port();
			}
			catch (system_error & e) { set_last_error(e); }
			return static_cast<unsigned short>(0);
		}

	protected:
		SERVERTYPE & server_;
		NIO & cio_;
		asio::ip::tcp::acceptor acceptor_;
		asio::steady_timer acceptor_timer_;
	};
}
