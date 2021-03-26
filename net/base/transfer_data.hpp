#pragma once

#include "base/iopool.hpp"
#include "base/error.hpp"
#include "tool/bytebuffer.hpp"

namespace net {
	template<class DRIVERTYPE, class SOCKETTYPE, class PROTOCOLTYPE = void>
	class TransferData {
	public:
		TransferData() : derive_(static_cast<DRIVERTYPE&>(*this)) {}

		~TransferData() = default;

		inline bool send(const std::string&& data) {
			if constexpr (is_tcp_socket_v<SOCKETTYPE>) {
				return this->send_t(std::move(data));
			}
			else if constexpr (is_udp_socket_v<SOCKETTYPE>) {
				if (derive_.remote_endpoint().port() <= 0) {
					return this->send_t(std::move(data));
				}
				return this->send_t(derive_.remote_endpoint(), std::move(data));
			}
			return false;
		}

		template<bool isdo = false>
		inline void do_recv() {
			this->do_recv_t<SOCKETTYPE, isdo>();
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
		desc: recv data
		condition:
			transfer_all:直到buff满才返回.
			transfer_at_least:buff满或者至少读取参数设定字节数返回.
			transfer_exactly:buff满或者正好读取参数设定字节数返回.
		*/
		template<class TSOCKETTYPE, bool isdo = false, std::enable_if_t<is_tcp_socket_v<TSOCKETTYPE>, bool> = true>
		inline void do_recv_t() {
			if (!this->derive_.is_started())
				return;
			try {
				asio::async_read(this->derive_.stream(), this->derive_.buffer(), asio::transfer_at_least(1),
					asio::bind_executor(derive_.cio().strand(),
						[this, selfptr = this->derive_.self_shared_ptr()](const error_code& ec, std::size_t bytes_recvd)
				{
					set_last_error(ec);
					if (!ec) {
						this->derive_.handle_recv(ec, std::string(reinterpret_cast<
							std::string::const_pointer>(this->derive_.buffer().data().data()), bytes_recvd));

						this->derive_.buffer().consume(bytes_recvd);

						this->do_recv_t<TSOCKETTYPE, isdo>();
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
		template<class USOCKETTYPE, bool isdo = false, std::enable_if_t<is_udp_socket_v<USOCKETTYPE>, bool> = true>
		inline void do_recv_t() {
			if constexpr (!isdo) {
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
					//this->derived()._handle_recv(ec, bytes_recvd, std::move(self_ptr), condition);
					if (ec == asio::error::operation_aborted) {
						this->derive_.stop(ec);
						return;
					}
					this->ubuffer_.rd_flip(bytes_recvd);
					this->derive_.handle_recv(ec, std::string(reinterpret_cast<
						std::string::const_pointer>(this->ubuffer_.rd_buf()), bytes_recvd));

					this->ubuffer_.reset();

					this->do_recv_t<USOCKETTYPE, isdo>();
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
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

		t_buffer_cmdqueue<> ubuffer_;
		std::size_t init_buffer_size_ = 1024;
	};
}

