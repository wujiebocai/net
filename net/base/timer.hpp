#pragma once

#include <chrono>
#include <functional>

#include "base/iopool.hpp"
#include "base/error.hpp"

namespace net {
	class Timer {
	public:
		explicit Timer(NIO& tio)
			: cio_(tio)
			, timer_(cio_.context()) {
		}

		~Timer() {
			stop();
		}

	public:

		inline std::chrono::milliseconds get_interval() const {
			return this->interval_;
		}

		template<class Rep, class Period>
		inline void set_interval(std::chrono::duration<Rep, Period> duration) {
			this->interval_ = duration;
		}

		inline void reset_active_time() {
			this->active_time_ = std::chrono::system_clock::now();
		}

		// interval:毫秒
		template<bool isloop = true, class Fn>
		inline void post_timer(int64_t interval, Fn&& f) {
			this->interval_ = std::chrono::milliseconds(interval);
			this->timer_canceled_.clear();
			return post_timer<isloop>(this->interval_, std::move(f));
		}

		template<class Rep, class Period, class Fn>
		inline std::future<error_code> post_timeout_timer_test(std::chrono::duration<Rep, Period> duration, Fn&& fn) {
			std::promise<error_code> promise;
			std::future<error_code> future = promise.get_future();

			this->timer_.expires_after(duration);
			this->timer_.async_wait(asio::bind_executor(this->cio_.strand(),
				[this, p = std::move(promise), f = std::forward<Fn>(fn)]
			(const error_code& ec) mutable
			{
				f(ec);
				p.set_value(ec);
				this->timer_canceled_.clear();
			}));

			return future;
		}

		template<class Future>
		inline void wait_timeout_timer(Future&& future) {
			if (!this->cio_.strand().running_in_this_thread())
				future.wait();
		}

		inline void stop() {
			this->timer_canceled_.test_and_set();
			try {
				this->timer_.cancel();
			}
			catch (system_error&) {}
			catch (std::exception&) {}
		}

	protected:
		template<bool isloop = true, class Fn, class Rep, class Period>
		inline void post_timer(std::chrono::duration<Rep, Period> duration, Fn&& f) {
			if (duration >= std::chrono::milliseconds(0)) {
				this->timer_.expires_after(duration);
				this->timer_.async_wait(asio::bind_executor(this->cio_.strand(),
					[this, f = std::move(f)](const error_code& ec) mutable
				{
					handle_timer<isloop>(ec, f);
					this->timer_canceled_.clear();
				}));
			}
		}

		template<bool isloop = true, class Fn>
		inline void handle_timer(const error_code& ec, Fn&& f) {
			if (ec == asio::error::operation_aborted || this->timer_canceled_.test_and_set()) return;
			f(ec);

			if constexpr (isloop) {
				reset_active_time();
			}

			if (cal_duration() < this->interval_) {
				post_timer<isloop>(this->interval_ - cal_duration(), std::move(f));
			}
			/*else {
				set_last_error(asio::error::timed_out);
				//do_stop(asio::error::timed_out);
			}*/
		}

		inline std::chrono::milliseconds cal_duration() const {
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now() - this->active_time_);
		}

	protected:
		NIO& cio_;

		asio::steady_timer timer_;

		std::atomic_flag timer_canceled_ = ATOMIC_FLAG_INIT;

		std::chrono::milliseconds interval_ = std::chrono::minutes(60);

		std::chrono::time_point<std::chrono::system_clock> active_time_ = std::chrono::system_clock::now();
	};
}
