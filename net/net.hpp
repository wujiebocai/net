#pragma once

#define NET_USE_SSL //主要用于测试，后面放到cmake里面去
#include <asio/asio.hpp>
#if defined(NET_USE_SSL)
#include <asio/ssl.hpp>
#endif

#include "base/server.hpp"
#include "base/client.hpp"

#include "tool/bytebuffer.hpp"
#include "tool/msg_proxy.hpp"

/*
net：
socket type: tcp, udp
stream type: binary, ssl, kcp, ...
protocol type: http, websocket, ...
*/

//tcp
using TcpSvr = net::Server<asio::ip::tcp::socket, net::binary_stream_flag>;
using TcpCli = net::Client<asio::ip::tcp::socket, net::binary_stream_flag>;

//tcps
using TcpsSvr = net::Server<asio::ip::tcp::socket, net::ssl_stream_flag>;
using TcpsCli = net::Client<asio::ip::tcp::socket, net::ssl_stream_flag>;

//udp
using UdpSvr = net::Server<asio::ip::udp::socket&, net::binary_stream_flag>;
using UdpCli = net::Client<asio::ip::udp::socket, net::binary_stream_flag>;

//kcp
using KcpSvr = net::Server<asio::ip::udp::socket&, net::kcp_stream_flag>;
using KcpCli = net::Client<asio::ip::udp::socket, net::kcp_stream_flag>;

//下面得都有待实现
//http
using HttpSvr = net::Server<asio::ip::tcp::socket, net::binary_stream_flag, net::http_proto_flag>;
using HttpCli = net::Client<asio::ip::tcp::socket, net::binary_stream_flag, net::http_proto_flag>;

//https
using HttpsSvr = net::Server<asio::ip::tcp::socket, net::ssl_stream_flag, net::http_proto_flag>;
using HttpsCli = net::Client<asio::ip::tcp::socket, net::ssl_stream_flag, net::http_proto_flag>;

//websocket
using WebsocketSvr = net::Server<asio::ip::tcp::socket, net::binary_stream_flag, net::websocket_proto_flag>;
using WebsocketCli = net::Client<asio::ip::tcp::socket, net::binary_stream_flag, net::websocket_proto_flag>;

//websockets
using WebsocketsSvr = net::Server<asio::ip::tcp::socket, net::ssl_stream_flag, net::websocket_proto_flag>;
using WebsocketsCli = net::Client<asio::ip::tcp::socket, net::ssl_stream_flag, net::websocket_proto_flag>;

