#pragma once

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <functional>
#include <type_traits>

#include "base/iopool.hpp"

namespace net {
	template<class SESSIONTYPE>
	class SessionMgr {
	public:
		using self = SessionMgr<SESSIONTYPE>;
		using key_type = std::size_t;
	public:
		explicit SessionMgr(NIO& io) : cio_(io) {
			this->sessions_.reserve(64);
		}
		~SessionMgr() = default;

		inline bool emplace(std::shared_ptr<SESSIONTYPE> session_ptr) {
			if (!session_ptr)
				return false;

			std::unique_lock<std::shared_mutex> guard(this->mutex_);
			return this->sessions_.try_emplace(session_ptr->hash_key(), session_ptr).second;
		}

		inline bool erase(std::shared_ptr<SESSIONTYPE> session_ptr) {
			if (!session_ptr)
				return false;

			std::unique_lock<std::shared_mutex> guard(this->mutex_);
			return (this->sessions_.erase(session_ptr->hash_key()) > 0);
		}

		inline void foreach(const std::function<void(std::shared_ptr<SESSIONTYPE> &)> & fn) {
			std::shared_lock<std::shared_mutex> guard(this->mutex_);
			for (auto &[k, session_ptr] : this->sessions_) {
				fn(session_ptr);
			}
		}

		inline std::shared_ptr<SESSIONTYPE> find(const key_type & key) {
			std::shared_lock<std::shared_mutex> guard(this->mutex_);
			auto iter = this->sessions_.find(key);
			return (iter == this->sessions_.end() ? std::shared_ptr<SESSIONTYPE>() : iter->second);
		}

		inline std::shared_ptr<SESSIONTYPE> find_if(const std::function<bool(std::shared_ptr<SESSIONTYPE> &)> & fn) {
			std::shared_lock<std::shared_mutex> guard(this->mutex_);
			auto iter = std::find_if(this->sessions_.begin(), this->sessions_.end(),
				[this, &fn](auto &pair)
			{
				return fn(pair.second);
			});
			return (iter == this->sessions_.end() ? std::shared_ptr<SESSIONTYPE>() : iter->second);
		}

		inline std::size_t size() {
			return this->sessions_.size();
		}

		inline bool empty() {
			return this->sessions_.empty();
		}
	protected:
		NIO & cio_;
		std::unordered_map<key_type, std::shared_ptr<SESSIONTYPE>> sessions_;
		std::shared_mutex mutex_;
	};
}
