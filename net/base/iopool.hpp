#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <type_traits>

#include "base/define.hpp"

namespace net {
	class NIO {
	public:
		NIO() : context_(1), strand_(context_) {}
		~NIO() = default;

		inline asio::io_context & context() { return this->context_; }
		inline asio::io_context::strand &  strand() { return this->strand_; }

	protected:
		asio::io_context context_;
		asio::io_context::strand strand_;
	};

	class IoPool {
	public:
		explicit IoPool(std::size_t concurrency = std::thread::hardware_concurrency() * 2)
			: ios_(concurrency == 0 ? std::thread::hardware_concurrency() * 2 : concurrency) {
			this->threads_.reserve(this->ios_.size());
			this->works_.reserve(this->ios_.size());
		}

		~IoPool() = default;

		bool start() {
			std::lock_guard<std::mutex> guard(this->mutex_);
			if (!stopped_)
				return false;
			if (!this->works_.empty() || !this->threads_.empty())
				return false;

			for (auto & io : this->ios_) {
				io.context().restart();
			}

			for (auto & io : this->ios_) {
				this->works_.emplace_back(io.context().get_executor());
				// start work thread
				this->threads_.emplace_back([&io]() {
					io.context().run();
				});
			}

			stopped_ = false;

			return true;
		}

		void stop() {
			{
				std::lock_guard<std::mutex> guard(this->mutex_);

				if (stopped_)
					return;

				if (this->works_.empty() && this->threads_.empty())
					return;

				if (this->running_in_iopool_threads())
					return;

				stopped_ = true;
			}
			this->wait_iothreads(); 
			{
				std::lock_guard<std::mutex> guard(this->mutex_);

				for (std::size_t i = 1; i < this->works_.size(); ++i) {
					this->works_[i].reset();
				}

				for (auto & thread : this->threads_) {
					thread.join();
				}

				this->works_.clear();
				this->threads_.clear();
			}
		}

		inline NIO & get(std::size_t index = static_cast<std::size_t>(-1)) {
			return this->ios_[index < this->ios_.size() ? index : ((++(this->next_)) % this->ios_.size())];
		}
		
		inline bool running_in_iopool_threads() {
			std::thread::id curr_tid = std::this_thread::get_id();
			for (auto & thread : this->threads_) {
				if (curr_tid == thread.get_id())
					return true;
			}
			return false;
		}

		//确保所有得io事件完成
		void wait_iothreads() {
			std::lock_guard<std::mutex> guard(this->mutex_);

			if (this->running_in_iopool_threads())
				return;

			if (!this->works_.empty())
				this->works_[0].reset();

			constexpr auto max = std::chrono::milliseconds(10);
			constexpr auto min = std::chrono::milliseconds(1);

			if (!this->ios_.empty()) {
				auto t1 = std::chrono::steady_clock::now();
				asio::io_context & ioc = this->ios_.front().context();
				while (!ioc.stopped()) {
					auto t2 = std::chrono::steady_clock::now();
					std::this_thread::sleep_for(
						(std::max<std::chrono::steady_clock::duration>)(
						(std::min<std::chrono::steady_clock::duration>)(t2 - t1, max), min));
				}
			}

			for (std::size_t i = 1; i < this->works_.size(); ++i) {
				this->works_[i].reset();
			}

			for (std::size_t i = 1; i < this->ios_.size(); ++i) {
				auto t1 = std::chrono::steady_clock::now();
				asio::io_context & ioc = this->ios_.at(i).context();
				while (!ioc.stopped()) {
					auto t2 = std::chrono::steady_clock::now();
					std::this_thread::sleep_for(
						(std::max<std::chrono::steady_clock::duration>)(
						(std::min<std::chrono::steady_clock::duration>)(t2 - t1, max), min));
				}
			}
		}

	protected:
		std::vector<std::thread> threads_;
		std::vector<NIO> ios_;
		std::vector<asio::executor_work_guard<asio::io_context::executor_type>> works_;
		std::mutex  mutex_;
		bool stopped_ = true;
		std::size_t next_ = 0;
	};

	class IoPoolImp
	{
	public:
		IoPoolImp(std::size_t concurrency) : iopool_(concurrency) {}
		~IoPoolImp() = default;

	protected:
		IoPool iopool_;
	};
}
