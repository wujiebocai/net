#pragma once

#include "base/iopool.hpp"
#include "base/error.hpp"

namespace net {
	template<class DRIVERTYPE, class SOCKETTYPE, class PROTOCOLTYPE = void>
	class TransferData {
	public:
		TransferData() : derive_(static_cast<DRIVERTYPE&>(*this)) {}

		~TransferData() = default;

		inline bool send(const std::string_view&& data) {
			try {
				if (!this->derive_.is_started())
					asio::detail::throw_error(asio::error::not_connected);

				return this->send_enqueue([this,
					data = std::move(data)]() mutable {
					return this->do_send(data, [](const error_code&, std::size_t) {});
				});
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
		inline typename std::enable_if_t<is_tcp_socket_v<SOCKETTYPE>> 
		do_recv() {
			if (!this->derive_.is_started())
				return;
			try {
				asio::async_read(this->derive_.stream(), this->derive_.buffer(), asio::transfer_at_least(1),
					asio::bind_executor(derive_.cio().strand(),
						[this, selfptr = this->derive_.self_shared_ptr()](const error_code& ec, std::size_t bytes_recvd)
				{
					set_last_error(ec);
					if (!ec) {
						this->derive_.handle_recv(selfptr, std::string_view(reinterpret_cast<
							std::string_view::const_pointer>(this->derive_.buffer().data().data()), bytes_recvd));

						this->derive_.buffer().consume(bytes_recvd);

						this->do_recv();
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
		desc: send data
		*/
		template<bool IsAsync = true, class Data, class Callback, typename = std::enable_if_t<is_tcp_socket_v<SOCKETTYPE>>>
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
	};
}

