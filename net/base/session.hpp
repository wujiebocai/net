#pragma once

#include <any>
#include <memory>
#include <functional>
#include <queue>
#include <string_view>

#include "base/iopool.hpp"
#include "base/error.hpp"
#include "base/stream.hpp"
#include "base/session_mgr.hpp"
#include "base/transfer_data.hpp"
#include "tool/bytebuffer.hpp"

namespace net {
	template<class SOCKETTYPE, class STREAMTYPE = void, class PROTOCOLTYPE = void>
	class Session : public StreamType<Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, SOCKETTYPE, STREAMTYPE, svr_tab>
				  , public TransferData<Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE, svr_tab>
				  , public std::enable_shared_from_this<Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>> {
	public:
		using session_type = Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		using session_ptr_type = std::shared_ptr<session_type>;
		using stream_type = StreamType<session_type, SOCKETTYPE, STREAMTYPE, svr_tab>;
		using transferdata_type = TransferData<Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE, svr_tab>;
		using sessionmgr_type = SessionMgr<session_type>;
		using key_type = typename std::conditional<is_udp_socket_v<SOCKETTYPE>, asio::ip::udp::endpoint, std::size_t>::type;
	public:
		template<class ...Args>
		explicit Session(sessionmgr_type& sessions, FuncProxyImpPtr & cbfunc, NIO & io,
						std::size_t max_buffer_size, Args&&... args)
			: stream_type(std::forward<Args>(args)...)
			, transferdata_type(max_buffer_size)
			, cio_(io)
			, cbfunc_(cbfunc)
			, sessions_(sessions)
		{
		
		}

		~Session() = default;

		template<bool iskeepalive = false>
		inline void start(error_code ec) {
			//if (!this->cio_.strand().running_in_this_thread())
			//	return asio::post(this->cio_.strand(), std::bind(&session_type::start<iskeepalive>, this, std::move(ec)));
			const auto& dptr = this->shared_from_this();
			this->stream_post_handshake(dptr, [this, dptr = this->shared_from_this()](const error_code& ec) {
				try {
					State expected = State::stopped;
					if (!ec && !this->state_.compare_exchange_strong(expected, State::starting))
						asio::detail::throw_error(asio::error::already_started);
					//cbfunc_->call(Event::accept, dptr, ec);

					if constexpr (iskeepalive && is_tcp_socket_v<SOCKETTYPE>)
						this->keep_alive_options();
					else
						std::ignore = true;

					expected = State::starting;
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

		inline void stop(const error_code& ec) {
			auto handlefunc = [this](session_ptr_type sessionptr, const error_code& ec, State oldstate) {
				asio::post(this->cio_.strand(),
				[this, ec, dptr = std::move(sessionptr), oldstate]() {
					//从sessionmgr移除
					bool isremove = this->sessions_.erase(dptr);
					if (!isremove) {
						return;
					}
					set_last_error(ec);
					State expected = State::stopping;
					if (this->state_.compare_exchange_strong(expected, State::stopped)) {
						if (oldstate == State::started)
							cbfunc_->call(Event::disconnect, dptr, ec);
					}
					else {
						NET_ASSERT(false);
					}
					this->user_data_reset();
					this->stream_stop(dptr);
				});
			};
			State expected = State::starting;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return handlefunc(this->shared_from_this(), ec, expected);

			expected = State::started;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return handlefunc(this->shared_from_this(), ec, expected);
		}

		inline bool is_started() const {
			return (this->state_ == State::started && this->socket_.lowest_layer().is_open());
		}
		inline bool is_stopped() const {
			return (this->state_ == State::stopped && !this->socket_.lowest_layer().is_open());
		}

		inline const key_type hash_key() const {
			if constexpr (is_tcp_socket_v<SOCKETTYPE>) {
				return reinterpret_cast<key_type>(this);
			}
			else if constexpr (is_udp_socket_v<SOCKETTYPE>) {
				return this->remote_endpoint_;
			}
		}

		//imp(stream, self_shared_ptr, buffer, stop, is_started, handle_recv)
		inline auto self_shared_ptr() { return this->shared_from_this(); }
		//inline asio::streambuf& buffer() { return buffer_; }
		inline NIO& cio() { return cio_; }
		inline void handle_recv(error_code ec, std::string&& s) {
			if constexpr (is_kcp_streamtype_v<STREAMTYPE>) {
				return this->kcp_handle_recv(ec, s);
			}
			cbfunc_->call(Event::recv, this->self_shared_ptr(), std::move(s));
		}
		inline void set_first_pack(std::string&& str) { first_pack_ = std::move(str); }
		inline auto& get_first_pack() { return first_pack_; }
		inline t_buffer_cmdqueue<>& rbuffer() { return rbuff_; }
		inline auto& cbfunc() { return cbfunc_; }


		template<class DataT>
		inline void user_data(DataT && data) {
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
		NIO & cio_;

		std::atomic<State> state_ = State::stopped;

		sessionmgr_type& sessions_;

		FuncProxyImpPtr & cbfunc_;

		t_buffer_cmdqueue<> rbuff_;

		std::any user_data_;

		std::string first_pack_;
	};
}
