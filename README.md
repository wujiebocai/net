## 环境配置：

1. 使用了c++17特性。
2. cmake 3.16.1版本及以上。

## 使用说明：

​	包含net.hpp文件即可使用。

## 简单用法：

1. tcp,  udp,  kcp, websocket等用法具体看demo：

   ```c++
   1.tcp用法：
   	// svr
   	SvrProxy<TcpSvr> tcpsvr(8);
   	tcpsvr.start("0.0.0.0", "8888");
   	// client
   	CliProxy<TcpCli> tcpcli(4);
   	tcpcli.start();
   	for (int i = 0; i < 42; ++i) {
   		tcpcli.add("127.0.0.1", "8888");
   	}
   2.udp用法：
   	// svr
   	SvrProxy<UdpSvr> udpsvr(1);
   	udpsvr.start("0.0.0.0", "8888");
   	// client
   	CliProxy<UdpCli> udpcli(1);
   	udpcli.start();
   	for (int i = 0; i < 42; ++i) {
   		udpcli.add("127.0.0.1", "8888");
   	}
   3.kcp用法：
   	// svr
   	SvrProxy<KcpSvr> kcpsvr(1);
   	kcpsvr.start("0.0.0.0", "8888");
   	// client
   	CliProxy<KcpCli> kcpcli(1);
   	kcpcli.start();
   	for (int i = 0; i < 42; ++i) {
   		kcpcli.add("127.0.0.1", "8888");
   	}
   4.websocket用法：
   	SvrProxy<WebsocketSvr> wssvr(4);
   	wssvr.start("0.0.0.0", "8889");
   5.tcps用法
   	// svr
   	SvrProxy<TcpsSvr> sslsvr(8);
   	sslsvr.set_cert("test", cer, key, dh); 
   	
   	sslsvr.start("0.0.0.0", "8888");
   
   	// client
   	CliProxy<TcpsCli> sslcli(4);
   	sslcli.start();
   	sslcli.set_cert(cer); 
   	for (int i = 0; i < 42; ++i) {
   		sslcli.add("127.0.0.1", "8888");
   	}
   ```


注：具体用法和使用细节，请查看具体代码，demo中写了一些用法；里面一些其他小组件的用法看具体实列。