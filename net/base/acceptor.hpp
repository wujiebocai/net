#pragma once

#include "base/iopool.hpp"
#include "base/error.hpp"
#include "base/session.hpp"

namespace net {
	// default
	template<class ... Args>
	class Acceptor {
	public:
		template<class ... Args>
		explicit Acceptor(Args&&... args) {}
	};

	template<class SERVERTYPE, class SESSIONTYPE>
	class Acceptor<SERVERTYPE, SESSIONTYPE, asio::ip::tcp::socket> {
	public:
		Acceptor(NIO& io)
			: cio_(io)
			, acceptor_(this->cio_.context())
			, server_(static_cast<SERVERTYPE&>(*this))
			, acceptor_timer_(this->cio_.context())
		{}

		~Acceptor() {
			this->acceptor_stop();
		}

		inline bool acceptor_start(std::string_view host, std::string_view port) {
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
				//this->acceptor_stop();
				this->server_.stop(e.code());
			}
			return false;
		}

		inline void post_accept() {
			if (!this->server_.is_started())
				return;
			try {
				std::shared_ptr<SESSIONTYPE> session_ptr = this->server_.make_session();

				auto & socket = session_ptr->socket().lowest_layer();
				this->acceptor_.async_accept(socket, asio::bind_executor(this->cio_.strand(),
					[this, session_ptr = std::move(session_ptr)](const error_code & ec)
				{
					set_last_error(ec);
					if (ec == asio::error::operation_aborted) {
						//this->acceptor_stop();
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
						//this->acceptor_stop();
						this->server_.stop(ec);
						return;
					}
					asio::post(this->cio_.strand(), [this]() {
						this->post_accept();
					});
				}));
			}
		}

		inline void acceptor_stop() {
			this->acceptor_timer_.cancel();
			this->acceptor_.close(ec_ignore);
		}

		inline bool is_open() const { return this->acceptor_.is_open(); }

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

	template<class SERVERTYPE, class SESSIONTYPE>
	class Acceptor<SERVERTYPE, SESSIONTYPE, asio::ip::udp::socket&> {
	public:
		explicit Acceptor(NIO& io) 
			: acceptor_(io.context())
			, cio_(io)
			, server_(static_cast<SERVERTYPE&>(*this))
			, remote_endpoint_() {}

		~Acceptor() = default;

		inline bool acceptor_start(std::string_view host, std::string_view port) {
			try {
				clear_last_error();

				this->acceptor_.close(ec_ignore);

				asio::ip::udp::resolver resolver(this->cio_.context());
				asio::ip::udp::endpoint endpoint = *resolver.resolve(host, port,
					asio::ip::resolver_base::flags::passive | asio::ip::resolver_base::flags::address_configured).begin();

				this->acceptor_.open(endpoint.protocol());

				this->acceptor_.set_option(asio::ip::udp::socket::reuse_address(true)); // set port reuse

				//this->acceptor_.set_option(
				//	asio::ip::multicast::join_group(asio::ip::make_address("ff31::8000:1234")));
				//	asio::ip::multicast::join_group(asio::ip::make_address("239.255.0.1")));


				this->acceptor_.bind(endpoint);

				asio::post(this->cio_.strand(), [this]() {
					this->post_recv();
				});

				return true;
			}
			catch (system_error& e) {
				set_last_error(e);
				this->server_.stop(e.code());
			}

			return false;
		}

		inline void acceptor_stop() {
			this->acceptor_.shutdown(asio::socket_base::shutdown_both, ec_ignore);
			this->acceptor_.close(ec_ignore);
		}
	protected:
		inline void post_recv() {
			if (!this->server_.is_started())
				return;

			try {
				this->buffer_.wr_reserve(init_buffer_size_);
				this->acceptor_.async_receive_from(
					asio::mutable_buffer(this->buffer_.wr_buf(), this->buffer_.wr_size()), this->remote_endpoint_,
					asio::bind_executor(this->cio_.strand(), [this](const error_code& ec, std::size_t bytes_recvd) {
					this->handle_recv(ec, bytes_recvd);
				}));
			}
			catch (system_error& e) {
				set_last_error(e);
				this->server_.stop(e.code());
			}
		}

		inline void handle_recv(const error_code& ec, std::size_t bytes_recvd) {
			set_last_error(ec);

			if (ec == asio::error::operation_aborted) {
				this->server_.stop(ec);
				return;
			}

			if (!this->server_.is_started())
				return;

			this->buffer_.rd_flip(bytes_recvd);
			if (!ec) {
				std::string sdata(static_cast<std::string_view::const_pointer>(this->buffer_.rd_buf()), bytes_recvd);

				std::shared_ptr<SESSIONTYPE> session_ptr = this->server_.get_sessions().find(this->remote_endpoint_);
				if (!session_ptr) {
					//std::cout << "udp acceptor: " << remote_endpoint_.data() << ", aa:" << std::hash<asio::ip::udp::endpoint>()(remote_endpoint_) << std::endl;
					std::shared_ptr<SESSIONTYPE> session_ptr = this->server_.make_session();
					session_ptr->set_first_pack(std::move(sdata));
					session_ptr->start(ec);
					//session_ptr->handle_recv(ec, std::move(sdata));
				}
				else
					session_ptr->handle_recv(ec, std::move(sdata));
			}

			this->buffer_.reset();

			this->post_recv();
		}
		inline bool is_open() const { return this->acceptor_.is_open(); }
	protected:
		SERVERTYPE& server_;
		asio::ip::udp::socket acceptor_;
		NIO& cio_;
		asio::ip::udp::endpoint  remote_endpoint_;

		t_buffer_cmdqueue<> buffer_;
		
		std::size_t init_buffer_size_ = 1024;
	};
}
