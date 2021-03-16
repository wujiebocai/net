
#include <iostream> 

#include "net.hpp"
using namespace net;

void tcp_test() {
	auto svr = std::make_shared<TcpSvr>(8);

	net::Timer testtimer(svr->get_iopool().get(0));   //测试性能计时器
	std::atomic<std::size_t> count{ 1 };
	testtimer.post_timer(10 * 1000, [&count](const error_code& ec) {
		std::cout << count / 10 << std::endl;
		count = 0;
		return true;
	});

	svr->bind(Event::connect, [](STcpSessionPtr& ptr, error_code ec) {
		std::cout << "connect" << ec.message() << std::endl;
	});
	svr->bind(Event::disconnect, [](STcpSessionPtr& ptr, error_code ec) {
		std::cout << "disconnect" << std::endl;
	});
	svr->bind(Event::recv, [&](STcpSessionPtr& ptr, std::string&& s) {
		//std::cout << s << std::endl;
		ptr->send(std::move(s));
		++count;

	});

	svr->start("0.0.0.0", "8888");

	/////////////////////////////////////////////////////////////////////////////////////////////
	auto cli = std::make_shared<TcpCli>(4);

	cli->start();

	cli->bind(Event::connect, [](CTcpSessionPtr& ptr, error_code ec) {
		if (!ec) {
			ptr->send("a");
		}
		std::cout << "connect client" << ec.message() << std::endl;
	});
	cli->bind(Event::disconnect, [](CTcpSessionPtr& ptr, error_code ec) {
		std::cout << "disconnect client" << std::endl;
	});
	cli->bind(Event::recv, [](CTcpSessionPtr& ptr, std::string&& s) {
		ptr->send(std::move(s));
	});

	for (int i = 0; i < 42; ++i) {
		cli->add("127.0.0.1", "8888");
	}
}

void tcp_ssl_test() {
#ifdef NET_USE_SSL
	std::string_view cer =
		"-----BEGIN CERTIFICATE-----\r\n"\
		"MIICcTCCAdoCCQDYl7YrsugMEDANBgkqhkiG9w0BAQsFADB9MQswCQYDVQQGEwJD\r\n"\
		"TjEOMAwGA1UECAwFSEVOQU4xEjAQBgNVBAcMCVpIRU5HWkhPVTENMAsGA1UECgwE\r\n"\
		"SE5aWDENMAsGA1UECwwESE5aWDEMMAoGA1UEAwwDWkhMMR4wHAYJKoZIhvcNAQkB\r\n"\
		"Fg8zNzc5MjczOEBxcS5jb20wHhcNMTcxMDE1MTQzNjI2WhcNMjcxMDEzMTQzNjI2\r\n"\
		"WjB9MQswCQYDVQQGEwJDTjEOMAwGA1UECAwFSEVOQU4xEjAQBgNVBAcMCVpIRU5H\r\n"\
		"WkhPVTENMAsGA1UECgwESE5aWDENMAsGA1UECwwESE5aWDEMMAoGA1UEAwwDWkhM\r\n"\
		"MR4wHAYJKoZIhvcNAQkBFg8zNzc5MjczOEBxcS5jb20wgZ8wDQYJKoZIhvcNAQEB\r\n"\
		"BQADgY0AMIGJAoGBAMc2Svpl4UgxCVKGwoYJBxNWObXvQzw74ksY6Zoiq5tJNJzf\r\n"\
		"q9ZCJigwjx3vAFF7tELRxsgAf6l7AvReu1O6difjdpMkEic0W7acZtldislDjUbu\r\n"\
		"qitfHsWeKTucBu3+3TUawvv+fdeWgeN54jMoL+Oo3CV7d2gFRV2fD5z4tryXAgMB\r\n"\
		"AAEwDQYJKoZIhvcNAQELBQADgYEAwDIC3xYmYJ6kLI8NgmX89re0scSWCcA8VgEZ\r\n"\
		"u8roYjYauCLkp1aXNlZtJFQjwlfo+8FLzgp3dP8Y75YFwQ5zy8fFaLQSQ/0syDbx\r\n"\
		"sftKSVmxDo3S27IklEyJAIdB9eKBTeVvrT96R610j24t1eYENr59Vk6A/fKTWJgU\r\n"\
		"EstmrAs=\r\n"\
		"-----END CERTIFICATE-----\r\n";

	std::string_view key =
		"-----BEGIN RSA PRIVATE KEY-----\r\n"\
		"Proc-Type: 4,ENCRYPTED\r\n"\
		"DEK-Info: DES-EDE3-CBC,EC5314BD06CD5FB6\r\n"\
		"\r\n"\
		"tP93tjR4iOGfOLHjIBQA0aHUE5wQ7EDcUeKacFfuYrtlYbYpbRzhQS+vGtoO1wGg\r\n"\
		"h/s9DbEN1XaiV9aE+N3E54zu2LuVO1lYDtCf3L26cd1Bu6gj0cWiAMco1Vm7RV9j\r\n"\
		"vmgmeOYkqbOiAbiIa4HCmDkEaHY4nCPlW+cdYxrozkAQCAiTpFQR8taRB0lsly0i\r\n"\
		"lUQitYLz3nhEMucLffcwAXN9IOnXFoURVZnLc53CX857iizOXeP9XeNE63UwDZ4v\r\n"\
		"1wnglnGUJA6vCxnxk6KvptF9rSdCD/sz1Y+J5mAVr+2y4vPLO4YOCL6HSFY6285M\r\n"\
		"RyGNVVx3vX0u6FbWJC3qt5yj6tMdVJ4O7U4XgqOKnS5jVLk+fKcTVyNySB5yAT2b\r\n"\
		"qwWCZcRPP2M+qlsSWhgzsucyz0eVOPVJxAJ4Vp/X6saO4xyRPsFV3USbRKlOMS7+\r\n"\
		"SEJ/7ANU9mEgLIQRKEfSKXWpQtm95pCVlajWQ7/3nXNjdV7mNi42ukdItBvOtdv+\r\n"\
		"oUiN8MkP/e+4SsGmJayNT7HvBC9DjoyDQIK6sZOgtsbAu/bDBhPnjnNsZcsgxJ/O\r\n"\
		"ijnj+0HyNS/Vr6emAkxTFgryUdBTuoY7019vcNWTYPDS3ugpe3goRHE0FTOwNdUe\r\n"\
		"dk+KM4bYAa0+1z1QEZTEoNqdT7WYwMD1QzgSWukYHemsWqoAvW5f4PrdoVA21W9D\r\n"\
		"L8I1YZf8ZHBnkuGX0oHi5w/4DkVNOT5BaZRmqXinZgFPwduYGVCh04x7ohuOQ5m0\r\n"\
		"etrTAVwJd2mcI7rDTaKCPT528/QWxZxXpHzggRoDil/5T7fn35ixRg==\r\n"\
		"-----END RSA PRIVATE KEY-----\r\n";

	std::string_view dh =
		"-----BEGIN DH PARAMETERS-----\r\n"\
		"MEYCQQCdoJif7jYqTh5+vLgt3q1FZvG+7WymoAoMKWMNOtqLZ+uFhZH3e9vFhV7z\r\n"\
		"NgWnHCe/vsGJok2wHS4R/laH6MQTAgEC\r\n"\
		"-----END DH PARAMETERS-----\r\n";

	auto svr = std::make_shared<TcpsSvr>(8);

	svr->get_netstream().set_cert("test", cer, key, dh); // 使用字符串测试
	//svr->get_netstream().set_cert_file("test", "server.crt", "server.key", "dh512.pem"); // 使用文件测试

	net::Timer testtimer(svr->get_iopool().get(0));   //测试性能计时器
	std::atomic<std::size_t> count{ 1 };
	testtimer.post_timer(10 * 1000, [&count](const error_code& ec) {
		std::cout << count / 10 << std::endl;
		count = 0;
		return true;
	});

	svr->bind(Event::handshake, [](STcpsSessionPtr& ptr, error_code ec) {
		std::cout << "handshake" << ec.message() << std::endl;
	});
	svr->bind(Event::connect, [](STcpsSessionPtr& ptr, error_code ec) {
		std::cout << "connect" << ec.message() << std::endl;
	});
	svr->bind(Event::disconnect, [](STcpsSessionPtr& ptr, error_code ec) {
		std::cout << "disconnect" << std::endl;
	});
	svr->bind(Event::recv, [&](STcpsSessionPtr& ptr, std::string&& s) {
		//std::cout << s << std::endl;
		ptr->send(std::move(s));
		++count;

	});

	svr->start("0.0.0.0", "8888");

	/////////////////////////////////////////////////////////////////////////////////////////////
	auto cli = std::make_shared<TcpsCli>(4);

	cli->start();

	cli->get_netstream().set_cert(cer); //使用字符串测试
	//cli->get_netstream().set_cert_file("server.crt"); //使用文件测试

	cli->bind(Event::handshake, [](CTcpsSessionPtr& ptr, error_code ec) {
		std::cout << "handshake client" << ec.message() << std::endl;
	});
	cli->bind(Event::connect, [](CTcpsSessionPtr& ptr, error_code ec) {
		if (!ec) {
			ptr->send("a");
		}
		std::cout << "connect client" << ec.message() << std::endl;
	});
	cli->bind(Event::disconnect, [](CTcpsSessionPtr& ptr, error_code ec) {
		std::cout << "disconnect client" << std::endl;
	});
	cli->bind(Event::recv, [](CTcpsSessionPtr& ptr, std::string&& s) {
		ptr->send(std::move(s));
	});

	for (int i = 0; i < 42; ++i) {
		cli->add("127.0.0.1", "8888");
	}
#endif
}

///////////////////协议代理测试///////////////////////////////////////////////////////
struct protodata {
	int a = 1;
	int b = 2;
	int c = 3;
};

void test_struct_proto(protodata pb) {
	std::cout << "func:" << pb.b << std::endl;
}
class test_proto {
public:
	void test_struct_proto(protodata pb) {
		std::cout << "class mem func:" << pb.b << std::endl;
	}
};
void test_msg_proxy() {
	//协议代理测试，支持pb， struct作为协议, 注册得函数原型：除了第一个参数必须为pb或者struct协议，其他参数和返回值都随意.
	net::MsgIdFuncProxyImpPtr msgproxy = std::make_shared<net::MSGIDPROXYTYPE>();
	// 以int作为协议id, 注册协议方式如下：
	//注册协议接口为函数
	msgproxy->bind(1, test_struct_proto);
	//注册为lambd表达式
	msgproxy->bind(2, [](protodata pb) {
		std::cout << "lambda:" << pb.c << std::endl;
	});
	//注册为类成员函数
	test_proto ctp;
	msgproxy->bind(3, &test_proto::test_struct_proto, ctp);
	msgproxy->bind(4, &test_proto::test_struct_proto, &ctp); //ctp也可以为智能指针

	// 以string作为协议id
	net::MsgStrFuncProxyImpPtr msgstrproxy = std::make_shared<net::MSGSTRPROXYTYPE>();
	msgstrproxy->bind("login", test_struct_proto);  //注册协议处理接口
	msgstrproxy->bind("logout", [](protodata pb, int logoutcode) {
		std::cout << "logout:" << logoutcode << std::endl;
	});

	//接收数据进行协议处理
	protodata msgtest;
	auto msglen = sizeof(msgtest);

	msgproxy->call(1, (const char*)&msgtest, msglen);   //处理协议1
	msgproxy->call(2, (const char*)&msgtest, msglen); // 处理协议2

	msgstrproxy->call("login", (const char*)&msgtest, msglen);
	msgstrproxy->call("logout", (const char*)&msgtest, msglen, 12);
}

//#include "help_type1.hpp"
asio::io_context g_context_(1);
asio::io_context::strand g_context_s_(g_context_);
int main(int argc, char * argv[]){
	////因为TcpSvr与TcpCli都使用了std::enable_shared_from_this，所以必须以智能指针方式创建，后面在优化.
	tcp_test();

	//tcp_ssl_test();

	//test_msg_proxy();

	auto io_worker = asio::make_work_guard(g_context_);
	g_context_.run();
	while (std::getchar() != '\n'); 

	//svr.stop();
	return 0; 
} 

