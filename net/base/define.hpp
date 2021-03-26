#pragma once

#include <type_traits>

#include "tool/help_type.hpp"
#include "tool/util.hpp"

namespace net {
	enum class State : std::int8_t { stopped, stopping, starting, started };
	// 回调事件
	enum class Event : std::int8_t {
		init,
		connect,
		disconnect,
		recv,
		packet,
		handshake,
		max
	};

	using CBPROXYTYPE = func_proxy_imp<Event>;
	typedef std::shared_ptr<CBPROXYTYPE> FuncProxyImpPtr;

	struct tcp_transfer_place {
	};
	struct udp_transfer_place {
	};
	struct svr_place {
	};
	struct client_place {
	};
	struct http_proto_flag {
	};
	struct websocket_proto_flag {
	};
	struct binary_stream_flag {
	};
	struct ssl_stream_flag {
	};
	struct kcp_stream_flag {
	};

	template<class SOCKETTYPE>
	constexpr bool is_tcp_socket_v = std::is_same_v<typename unqualified_t<SOCKETTYPE>::protocol_type, asio::ip::tcp>;
	template<class SOCKETTYPE>
	constexpr bool is_udp_socket_v = std::is_same_v<typename unqualified_t<SOCKETTYPE>::protocol_type, asio::ip::udp >;

	template<class PROTOCOLTYPE>
	constexpr bool is_http_protocoltype_v = std::is_same_v<PROTOCOLTYPE, http_proto_flag>;
	template<class PROTOCOLTYPE>
	constexpr bool is_websocket_protocoltype_v = std::is_same_v<PROTOCOLTYPE, websocket_proto_flag>;

	template<class STREAMTYPE>
	constexpr bool is_binary_streamtype_v = std::is_same_v<STREAMTYPE, binary_stream_flag>;
	template<class STREAMTYPE>
	constexpr bool is_ssl_streamtype_v = std::is_same_v<STREAMTYPE, ssl_stream_flag>;
	template<class STREAMTYPE>
	constexpr bool is_kcp_streamtype_v = std::is_same_v<STREAMTYPE, kcp_stream_flag>;

}

