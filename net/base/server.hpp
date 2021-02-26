#pragma once

#include <string_view>
#include "net/base/iopool.hpp"
#include "net/base/error.hpp"
#include "net/base/listener.hpp"

namespace net {
	template<class SOCKETTYPE, class STREAMTYPE = void, class PROTOCOLTYPE = void>
	class Server : public IoPoolImp
				 , public Listener<Server<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>, Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>>
				 , public std::enable_shared_from_this<Server<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE> > {
	public:
		using server_type = Server<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		using session_type = Session<SOCKETTYPE, STREAMTYPE, PROTOCOLTYPE>;
		using session_ptr_type = std::shared_ptr<session_type>;
		using listener_type = Listener<server_type, session_type>;
		using netstream_type = NetStream<SOCKETTYPE, STREAMTYPE>;
	public:
		explicit Server(std::size_t concurrency = std::thread::hardware_concurrency() * 2, std::size_t max_buffer_size = (std::numeric_limits<std::size_t>::max)())
			: IoPoolImp(concurrency)
			, listener_type(iopool_.get(0))
			, accept_io_(iopool_.get(0))
			, sessions_(accept_io_)
			, max_buffer_size_(max_buffer_size)
			, streamcxt_(svr_place{})
			
		{
			this->iopool_.start();
			this->cbfunc_ = std::make_shared<CBPROXYTYPE>();
		}

		~Server() {
			this->iopool_.stop();
		}

		inline bool start(std::string_view host, std::string_view service) {
			try {
				State expected = State::stopped;
				if (!this->state_.compare_exchange_strong(expected, State::starting)) {
					set_last_error(asio::error::already_started);
					return false;
				}
			
				clear_last_error();

				//cbfunc_->call(Event::init);

				this->listener_start(host, service);

				expected = State::starting;
				if (!this->state_.compare_exchange_strong(expected, State::started)) {
					set_last_error(asio::error::operation_aborted);
					return false;
				}

				return (this->is_started());
			}
			catch (system_error & e) {
				set_last_error(e);
				this->stop(e.code());
			}
			return false;
		}

		inline void stop(error_code ec) {
			//if (!this->accept_io_.strand().running_in_this_thread())
			//	return asio::post(this->accept_io_.strand(), std::bind(&server_type::stop, this, std::move(ec)));
			
			State expected = State::starting;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return this->post_stop(ec, expected);

			expected = State::started;
			if (this->state_.compare_exchange_strong(expected, State::stopping))
				return this->post_stop(ec, expected);

			//this->iopool_.wait_iothreads();
		}

		inline void post_stop(const error_code& ec, State old_state) {
			asio::post(this->accept_io_.strand(), [this, ec, this_ptr = this->shared_from_this(), old_state]() {
				set_last_error(ec);

				this->sessions_.foreach([this, ec](session_ptr_type & session_ptr) {
					session_ptr->stop(ec);
				});

				State expected = State::stopping;
				if (this->state_.compare_exchange_strong(expected, State::stopped)) {
					//cbfunc_->call(Event::stop);
					this->listener_stop();
				}
				else
					NET_ASSERT(false);
			});
		}

		inline bool is_started() const {
			return (this->state_ == State::started);
		}

		inline bool is_stopped() const {
			return (this->state_ == State::stopped);
		}

		//¹ã²¥ËùÓÐsession
		inline void send(const std::string_view && data) {
			this->sessions_.foreach([&data](session_ptr_type& session_ptr) {
				session_ptr->send(data);
			});
		}

		inline session_ptr_type make_session() {
			auto& cio = this->iopool_.get();
			if constexpr (is_ssl_streamtype_v<STREAMTYPE>) {
				return std::make_shared<session_type>(this->sessions_, this->cbfunc_, cio, this->max_buffer_size_
					, cio, streamcxt_, asio::ssl::stream_base::server, cio.context());
			}
			if constexpr (is_binary_streamtype_v<STREAMTYPE>) {
				return std::make_shared<session_type>(this->sessions_, this->cbfunc_, cio, this->max_buffer_size_
					, cio.context());
			}

		}

		inline std::size_t session_count() { return this->sessions_.size(); }

		inline void foreach_session(const std::function<void(session_ptr_type&)> & fn) {
			this->sessions_.foreach(fn);
		}

		inline session_ptr_type find_session_if(const std::function<bool(session_ptr_type&)> & fn) {
			return session_ptr_type(this->sessions_.find_if(fn));
		}

		/*inline void post(std::function<void()>&& task) {
			asio::post(this->io_.strand(), [task=std::move(task)]() { task(); });
		}*/

		template<class ...Args>
		bool bind(Args&&... args) {
			return cbfunc_->bind(std::move(args)...);
		}

		auto& get_iopool() { return iopool_; }
		auto& get_netstream() { return streamcxt_; }
	protected:
		//IoPool iopool_;
		NIO & accept_io_;

		SessionMgr<session_type> sessions_;

		std::atomic<State> state_ = State::stopped;

		std::size_t max_buffer_size_;

		FuncProxyImpPtr cbfunc_;

		netstream_type streamcxt_;
	};
}

