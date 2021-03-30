#pragma once

#include <memory>
#include <future>
#include <string_view>

#include "base/csession.hpp"

namespace net {
	template<class SOCKETTYPE, class STREAMTYPE = void, class PROTOCOLTYPE = void>
	class Client : public IoPoolImp
				 , public NetStream<SOCKETTYPE, STREAMTYPE>
				 , public std::enable_shared_from_this<Client<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>> {
	public:
		using resolver_type = typename asio::ip::basic_resolver<typename SOCKETTYPE::protocol_type>;
		using endpoints_type = typename resolver_type::results_type;
		using endpoint_type = typename SOCKETTYPE::lowest_layer_type::endpoint_type;
		using endpoints_iterator = typename endpoints_type::iterator;
		using session_type = CSession<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		using session_ptr_type = std::shared_ptr<session_type>;
		using session_weakptr_type = std::weak_ptr<session_type>;
		using netstream_type = NetStream<SOCKETTYPE, STREAMTYPE>;
	public:
		template<class ...Args>
		explicit Client(std::size_t concurrency = std::thread::hardware_concurrency() * 2, std::size_t max_buffer_size = (std::numeric_limits<std::size_t>::max)())
			: IoPoolImp(concurrency)
			, netstream_type(client_place{})
			, cio_(iopool_.get(0))
			, cbfunc_(std::make_shared<CBPROXYTYPE>())
			, sessions_(cio_)
			, max_buffer_size_(max_buffer_size)
		{
			this->iopool_.start();
		}

		~Client() {
			this->iopool_.stop();
		}

		template<bool isAsync = true, bool isKeepAlive = false>
		inline bool add(std::string_view host, std::string_view port) {
			clear_last_error();
			std::shared_ptr<session_type> session_ptr = this->make_session();
			return session_ptr->template start<isAsync, isKeepAlive>(host, port);
		}

		inline bool start() {
			State expected = State::stopped;
			if (!this->state_.compare_exchange_strong(expected, State::starting)) {
				set_last_error(asio::error::already_started);
				return false;
			}

			clear_last_error();

			//cbfunc_->call(Event::init);

			expected = State::starting;
			if (!this->state_.compare_exchange_strong(expected, State::started)) {
				set_last_error(asio::error::operation_aborted);
				return false;
			}

			return (this->is_started());
		}

		inline void stop(const error_code& ec) {
			State expected = State::starting;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return this->post_stop(ec, expected);

			expected = State::started;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return this->post_stop(ec, expected);
		}

		inline void post_stop(const error_code& ec, State old_state) {
			asio::post(this->cio_.strand(), [this, ec, this_ptr = this->shared_from_this(), old_state]() {
				set_last_error(ec);

				this->sessions_.foreach([this, ec](session_ptr_type& session_ptr) {
					session_ptr->stop(ec);
				});

				State expected = State::stopping;
				if (this->state_.compare_exchange_strong(expected, State::stopped)) {
					//cbfunc_->call(Event::stop);
				}
				else
					NET_ASSERT(false);
			});
		}

		inline session_ptr_type make_session() {
			auto& cio = this->iopool_.get();
			if constexpr (is_ssl_streamtype_v<STREAMTYPE>) {
				return std::make_shared<session_type>(this->sessions_, this->cbfunc_, cio, this->max_buffer_size_
					, cio, *this, asio::ssl::stream_base::client, cio.context());
			}
			if constexpr (is_binary_streamtype_v<STREAMTYPE>) {
				return std::make_shared<session_type>(this->sessions_, this->cbfunc_, cio, this->max_buffer_size_
					, cio.context());
			}
			if constexpr (is_kcp_streamtype_v<STREAMTYPE>) {
				return std::make_shared<session_type>(this->sessions_, this->cbfunc_, cio_, this->max_buffer_size_, cio_, cio.context());
			}
		}

		inline bool is_started() const {
			return (this->state_ == State::started);
		}

		inline bool is_stopped() const {
			return (this->state_ == State::stopped);
		}

		template<class ...Args>
		bool bind(Args&&... args) {
			return cbfunc_->bind(std::forward<Args>(args)...);
		}

		template<class ...Args>
		bool call(Args&&... args) {
			return cbfunc_->call(std::forward<Args>(args)...);
		}

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
		SessionMgr<session_type> sessions_;
		NIO & cio_; 

		std::size_t max_buffer_size_;

		std::atomic<State> state_ = State::stopped;

		FuncProxyImpPtr cbfunc_;
	};
}