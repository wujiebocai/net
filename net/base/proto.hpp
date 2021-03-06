#pragma once

/*
* 协议模块：后期需要可以支持外部定义协议.
*/

#include "opt/websocket/websocket.hpp"

namespace net {
	template<class DRIVERTYPE, class PROTOCOLTYPE, class SVRORCLI>
	class NetProto {
	public:
		template<class ... Args>
		explicit NetProto(Args&&... args) : derive_(static_cast<DRIVERTYPE&>(*this)) {}

		inline void parse_proto(error_code ec, const std::string& s) {
			if (ec) {
				std::cout << "parse websocket error: " << ec.message() << std::endl;
				return;
			}
			this->derive_.cbfunc()->call(Event::recv, this->derive_.self_shared_ptr(), std::move(s));
		}
		template<class DATATYPE>
		inline bool pack_proto(DATATYPE&& data) {
			return true;
		}
	protected:
		DRIVERTYPE& derive_;
	};

	// websocket
	template<class DRIVERTYPE>
	class NetProto<DRIVERTYPE, websocket_proto_flag, svr_tab> {
	public:
		template<class ... Args>
		explicit NetProto(Args&&... args) : derive_(static_cast<DRIVERTYPE&>(*this)) {}

		inline void parse_proto(error_code ec, const std::string& s) {
			if (ec) {
				std::cout << "parse websocket error: " << ec.message() << std::endl;
				return;
			}
			if (shared_flag_ == 0) {
				if (std::strstr(s.data(), "Upgrade: websocket") != NULL) {//握手处理
					ws_.parse_http_info(s.c_str());
					std::string respose;
					auto ret = ws_.get_handshark_pack(respose);
					if (ret) {
						this->derive_.send(respose);
						shared_flag_ = 1;
					}
				}
				return;
			}
			ws_.parse(s, [this](int opcode, std::string& data) {
				if (opcode == 8) { //关闭握手
					ws_.close_log(data);
					this->derive_.send(data);
					shared_flag_ = 0;
					return;
				}
				this->derive_.cbfunc()->call(Event::recv, this->derive_.self_shared_ptr(), std::move(data));
			});
		}
		template<class DATATYPE>
		inline bool pack_proto(DATATYPE&& data) {
			if (shared_flag_ <= 0) {
				return true;
			}
			std::string buffer;
			int packlen = ws_.get_pack_data(std::forward<DATATYPE>(data), buffer);
			if (packlen > 0) {
				data = std::move(buffer);
				return true;
			}
			return false;
		}
	protected:
		DRIVERTYPE& derive_;
		WebSocket ws_;
		std::atomic<std::size_t> shared_flag_{ 0 };
	};

	// http(有待实现)
	template<class DRIVERTYPE, class SVRORCLI>
	class NetProto<DRIVERTYPE, http_proto_flag, SVRORCLI> {
	public:
		template<class ... Args>
		explicit NetProto(Args&&... args) : derive_(static_cast<DRIVERTYPE&>(*this)) {}
	protected:
		DRIVERTYPE& derive_;
	};
}