#pragma once

#include <chrono>
#include <functional>

#include "base/iopool.hpp"
#include "base/error.hpp"

namespace net {
	inline int64_t get_mill_time() {
  		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	typedef std::function<bool(const error_code & ec)> TimerHandler;
	class Timer {
	public:
		explicit Timer(NIO & io)
			: cio_(io)
			, timer_(io.context())
		{
		}

		~Timer() {
			stop();
		}

		inline void handle_timer(const error_code & ec) {
			if (ec == asio::error::operation_aborted || this->timer_canceled_.test_and_set()) return;

			if (!timerout_handler_) {
				return;
			}

			auto result = timerout_handler_(ec);
			if (result) {
				post_timer();
			}
		}

		// fn返回true循环执行，否则单次执行
		inline void post_timer(int64_t interval, TimerHandler&& fn) {
			if (interval <= 0 || !fn) {
				return;
			}
			interval_ = interval;
			timerout_handler_ = std::forward<TimerHandler>(fn);

			post_timer();
		}

		inline void post_timer() {
			auto now         = get_mill_time();
  			auto expiredTime = ++tickCount_ * interval_ - (now - beginTime_);
  			expiredTime      = expiredTime <= 0 ? 10 : expiredTime;
  			std::chrono::milliseconds duration{expiredTime};

			this->timer_.expires_after(duration);
			this->timer_.async_wait(asio::bind_executor(this->cio_.strand(),
				[this](const error_code & ec) mutable {
				this->handle_timer(ec);
				this->timer_canceled_.clear();
			}));
		}

		/*// 执行一次，等待计时器回调
		template<class Fn>
		inline std::future<error_code> post_timer_async_once(int64_t interval, Fn&& fn) {
			auto now         = get_mill_time();
  			auto expiredTime = ++tickCount_ * interval_ - (now - beginTime_);
  			expiredTime      = expiredTime <= 0 ? 10 : expiredTime;
  			std::chrono::milliseconds duration{expiredTime};

			this->timer_.expires_after(duration);
			this->timer_.async_wait(asio::bind_executor(this->cio_.strand(),
				[this, f = std::forward<Fn>(fn)]
			(const error_code & ec) mutable {
				if (ec == asio::error::operation_aborted || this->timer_canceled_.test_and_set()) return;
				f(ec);
				p.set_value(ec);
				this->timer_canceled_.clear();
			}));

			return future;
		}

		// 执行一次，等待计时器回调
		template<class Fn>
		inline std::future<error_code> post_timer_sync_once(int64_t interval, Fn&& fn) {
			std::promise<error_code> promise;
			std::future<error_code> future = promise.get_future();

			auto now         = get_mill_time();
  			auto expiredTime = ++tickCount_ * interval_ - (now - beginTime_);
  			expiredTime      = expiredTime <= 0 ? 10 : expiredTime;
  			std::chrono::milliseconds duration{expiredTime};

			this->timer_.expires_after(duration);
			this->timer_.async_wait(asio::bind_executor(this->cio_.strand(),
				[this, p = std::move(promise), f = std::forward<Fn>(fn)]
			(const error_code & ec) mutable {
				if (ec == asio::error::operation_aborted || this->timer_canceled_.test_and_set()) return;
				f(ec);
				p.set_value(ec);
				this->timer_canceled_.clear();
			}));

			return future;
		}*/

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

		inline void stop() {
			this->timer_canceled_.test_and_set();
			try {
				this->timer_.cancel();
			}
			catch (system_error &) {}
			catch (std::exception &) {}
		}

		template<class Future>
		inline void wait_timeout_timer(Future&& future) {
			if (!this->cio_.strand().running_in_this_thread())
				future.wait();
		}

	protected:
		NIO & cio_;
		asio::steady_timer timer_;
		std::atomic_flag timer_canceled_ = ATOMIC_FLAG_INIT;

		int64_t beginTime_ = get_mill_time();
		int64_t interval_ = 5;
		int64_t tickCount_ = 0;

		TimerHandler timerout_handler_ = nullptr;
	};
}
