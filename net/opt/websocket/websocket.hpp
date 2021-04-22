#pragma once

// http://tools.ietf.org/html/rfc6455#section-5.2  Base Framing Protocol

/*
	  0                   1                   2                   3
	  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	 +-+-+-+-+-------+-+-------------+-------------------------------+
	 |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
	 |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
	 |N|V|V|V|       |S|             |   (if payload len==126/127)   |
	 | |1|2|3|       |K|             |                               |
	 +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
	 |     Extended payload length continued, if payload len == 127  |
	 + - - - - - - - - - - - - - - - +-------------------------------+
	 |                               |Masking-key, if MASK set to 1  |
	 +-------------------------------+-------------------------------+
	 | Masking-key (continued)       |          Payload Data         |
	 +-------------------------------- - - - - - - - - - - - - - - - +
	 :                     Payload Data continued ...                :
	 + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
	 |                     Payload Data continued ...                |
	 +---------------------------------------------------------------+
*/


#include <unordered_map>
//#include <inttypes.h>
#include "opt/common/sha1.hpp"
#include "opt/common/base64.hpp"
#include "opt/common/md5.hpp"
#include "tool/bytebuffer.hpp"

namespace net {
#define MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
typedef std::unordered_map<std::string, std::string> HEADER_MAP;

	struct WebSocketMark {
		std::uint8_t fin : 1;
		std::uint8_t rsv1 : 1;
		std::uint8_t rsv2 : 1;
		std::uint8_t rsv3 : 1;
		std::uint8_t opcode : 4;
		std::uint8_t mask : 1;
		std::uint8_t payloadlen : 7;

		WebSocketMark() {
			reset();
		}

		inline void reset() {
			fin = rsv1 = rsv2 = rsv3 = opcode = mask = payloadlen = 0;
		}
	};

	struct WebSocketHeader {
		WebSocketMark mark;
		std::uint64_t reallength = 0;//数据包长度
		std::uint8_t maskkey[4] = { 0 };
		std::uint32_t headlength = 0;//协议头长度

		inline void reset() {
			mark.reset();
			std::memset(maskkey, 0, 4);
			reallength = 0;
			headlength = 0;
		}
		
		inline void log() {
			std::cout << "===============================接收到的协议包头相关信息=====================================" << std::endl;
			std::cout << "fin: " << (int)this->mark.fin << "\nrsv1: " << (int)this->mark.rsv1 << "\nrsv2: " << (int)this->mark.rsv2 << "\nrsv3: " << (int)this->mark.rsv3
				<< "\nopcode: " << (int)this->mark.opcode << "\nmask: " << (int)this->mark.mask << "\nmask_key: " << (int*)this->maskkey << "\npayloadlen: " << (int)this->mark.payloadlen
				<< "\nheadlength: " << this->headlength << "\nreallength: " << this->reallength << std::endl;
		}
	};

	struct ProtoEnv {
		std::uint8_t fin = 1;
		std::uint8_t opcode = 2;
		std::uint8_t mask = 0;
		inline void reset() {
			fin = 1; opcode = 2; mask = 0;
		}
	};

	class WebSocket {
	public:
		WebSocket() = default;
		~WebSocket() = default;
	public:
		inline bool get_handshark_pack(std::string& response) {
			std::string server_key;

			HEADER_MAP::iterator itr = header_map_.find("Sec-WebSocket-Key1");
			if (itr != header_map_.end()) {
				auto ret = generate_key_safari(server_key);
				if (!ret) {
					return false;
				}
				const char* handshakeFormat = "HTTP/1.1 101 Web Socket Protocol Handshake\r\n"
					"Upgrade: WebSocket\r\n"
					"Connection: Upgrade\r\n"
					"Sec-WebSocket-Origin: %s\r\n"
					"Sec-WebSocket-Location: ws://%s%s\r\n"
					"Sec-WebSocket-Protocol: %s\r\n\r\n";
				sprintf(response.data(), handshakeFormat, header_map_["Origin"].c_str(), header_map_["Host"].c_str(), header_map_["GET"].c_str(), header_map_["Sec-WebSocket-Protocol"].c_str());
				response.append(server_key);
			}
			else {
				auto ret = generate_key_chrome(server_key);
				if (!ret) {
					return false;
				}
				response.append("HTTP/1.1 101 Switching Protocols\r\n""Upgrade: websocket\r\n");
				response.append("Sec-WebSocket-Accept: " + server_key + "\r\n");
				response.append("Connection: upgrade\r\n\r\n");
			}
			std::cout << "handshark response :" << response.c_str() << std::endl;
			
			return true;
		}
		//生成key(chrome版本)
		inline bool generate_key_chrome(std::string& key) {
			key = header_map_["Sec-WebSocket-Key"];
			if (key.empty()) {
				return false;
			}
			key.append(MAGIC_KEY);
			unsigned char message_digest[20];
			sha1::calc(key.c_str(), key.length(), message_digest);
			key = base64_encode(message_digest, 20);

			return true;
		}
		//生成key(safari版本), 该接口有待真实环境测试
		inline bool generate_key_safari(std::string& key) {
			std::string const& key1 = header_map_["Sec-WebSocket-Key1"];
			std::string const& key2 = header_map_["Sec-WebSocket-Key2"];
			std::string const& key3 = header_map_["Sec-WebSocket-Key3"];
			if (key1.empty() || key2.empty() || key3.size() != 8) {
				return false;
			}

			char key_final[16];
			decode_client_key(key1, &key_final[0]);
			decode_client_key(key2, &key_final[4]);
			std::copy(key3.c_str(),
				key3.c_str() + (std::min)(static_cast<size_t>(8), key3.size()),
				&key_final[8]);
			key = md5::md5_hash_string(std::string(key_final, 16));

			return true;
		}
		inline void decode_client_key(std::string const& key, char* result) const {
			unsigned int spaces = 0;
			std::string digits;
			std::uint32_t num;

			for (size_t i = 0; i < key.size(); i++) {
				if (key[i] == ' ') {
					spaces++;
				}
				else if (key[i] >= '0' && key[i] <= '9') {
					digits += key[i];
				}
			}

			num = static_cast<std::uint32_t>(strtoul(digits.c_str(), NULL, 10));
			if (spaces > 0 && num > 0) {
				num = htonl(num / spaces);
				std::copy(reinterpret_cast<char*>(&num),
					reinterpret_cast<char*>(&num) + 4,
					result);
			}
			else {
				std::fill(result, result + 4, 0);
			}
		}

		inline int parse_http_info(const char* buff) {
			header_map_.clear();
			std::istringstream s(buff);
			std::string request;

			std::getline(s, request);
			if (request[request.size() - 1] == '\r') {
				request.erase(request.end() - 1);
				std::string::size_type pos1;
				std::string::size_type pos2;
				pos1 = request.find("GET ", 0);
				pos1 += sizeof("GET ") - 1;
				pos2 = request.find(0x20, pos1);
				pos2 = pos2 - sizeof(0x20);
				std::string value = request.substr(pos1, pos2);
				header_map_["GET"] = value;
			}
			else {
				return -1;
			}

			std::string header;
			std::string::size_type end;

			while (std::getline(s, header) && header != "\r") {
				if (header[header.size() - 1] != '\r') {
					continue; //end
				}
				else {
					header.erase(header.end() - 1);	//remove last char
				}

				end = header.find(": ", 0);
				if (end != std::string::npos) {
					std::string key = header.substr(0, end);
					std::string value = header.substr(end + 2);
					header_map_[key] = value;
				}
			}

			std::size_t src_len = strlen(buff);
			if (src_len <= 8) {
				return 0;
			}
			char key3[9] = {0}; // 获取 key3，即正文最后的8位字符
			std::memcpy(key3, &buff[src_len - 8], 8);
			header_map_["Sec-WebSocket-Key3"] = key3;

			return 0;
		}

		inline bool analysis_msg(const char* ptCmd, const unsigned int nCmdLen) {
			ws_header_.reset();
			
			int pos = 0;
			fetch_base(ptCmd, pos);
			fetch_hasmask(ptCmd, pos);
			fetch_packagelength(ptCmd, pos);
			fetch_maskkey(ptCmd, pos);
			fetch_packagedata(ptCmd, pos);

			// log
			ws_header_.log();

			return check_unpack(nCmdLen);
		}
		inline int fetch_base(const char* msg, int& pos) {
			ws_header_.mark.fin = ((unsigned char)msg[pos] >> 7);
			ws_header_.mark.rsv1 = msg[pos] & 0x40;
			ws_header_.mark.rsv2 = msg[pos] & 0x20;
			ws_header_.mark.rsv3 = msg[pos] & 0x10;
			ws_header_.mark.opcode = msg[pos] & 0x0f;
			pos++;
			return pos;
		}
		inline int fetch_hasmask(const char* msg, int& pos) {
			ws_header_.mark.mask = ((unsigned char)msg[pos] >> 7);
			return pos;
		}
		inline int fetch_packagelength(const char* msg, int& pos) {
			ws_header_.mark.payloadlen = msg[pos] & 0x7f;
			ws_header_.reallength = ws_header_.mark.payloadlen;
			pos++;
			if (ws_header_.mark.payloadlen == 126) {
				std::uint16_t length = 0;
				std::memcpy(&length, msg + pos, 2);
				pos += 2;
				ws_header_.reallength = ntohs(length);
			}
			else if (ws_header_.mark.payloadlen == 127) {
				std::uint64_t length = 0;
				std::memcpy(&length, msg + pos, 8);
				pos += 8;
				ws_header_.reallength = ntohl(length);
			}
			return pos;
		}
		inline int fetch_maskkey(const char* msg, int& pos) {
			if (ws_header_.mark.mask == 0)
				return 0;
			std::memcpy(ws_header_.maskkey, &msg[pos], 4);
			pos += 4;
			return 0;
		}
		inline int fetch_packagedata(const char* msg, int& pos) {
			ws_header_.headlength = pos;
			pos += ws_header_.reallength;
			return 0;
		}

		inline bool mask_dec(char* msg, std::uint64_t len) {
			if (len != ws_header_.reallength) {
				return false;
			}
			if (ws_header_.mark.mask != 0) {
				for (std::uint64_t i = 0; i < len; i++) {
					std::uint64_t j = i % 4;
					msg[i] = msg[i] ^ ws_header_.maskkey[j];
				}
			}
			return true;
		}

		inline bool check_unpack(unsigned int buffLen) {
			constexpr std::size_t marklen = sizeof(WebSocketMark);
			unsigned int headLen = 0;
			if (buffLen < marklen) {
				return false;
			}
			if (ws_header_.mark.payloadlen < 126) {
				headLen = marklen;
			}
			else if (ws_header_.mark.payloadlen == 126) {
				headLen = marklen + 2;
			}
			else if (ws_header_.mark.payloadlen == 127) {
				headLen = marklen + 8;
			}
			if (ws_header_.mark.mask == 1) {
				headLen += 4;
			}
			if (buffLen < headLen) {
				return false;
			}

			auto packLen = ws_header_.headlength + ws_header_.reallength;
			if (buffLen < packLen) {
				return false;
			}

			return true;
		}

		inline int pack_data(const std::string& message, std::string& outstr, std::uint8_t fin, std::uint8_t opcode, std::uint8_t mask) {
			int headLen = 0;
			std::size_t msgLen = message.length();
			if (msgLen <= 0) {
				return 0;
			}

			auto slen = msgLen;
			if (msgLen < 126) // 如果不需要扩展长度位, 两个字节存放 fin(1bit) + rsv[3](1bit) + opcode(4bit); mask(1bit) + payloadLength(7bit);  
				slen += 2;
			else if (msgLen < 0xFFFF) // 如果数据长度超过126 并且小于两个字节, 我们再用后面的两个字节(16bit) 表示 uint16  
				slen += 4;
			else // 如果数据更长的话, 我们使用后面的8个字节(64bit)表示 uint64  
				slen += 10;
			
			if (mask & 0x1)
				slen += 4;
		
			outstr.resize(slen);
			char* out = outstr.data();
			//memset(out, 0, slen);
			*out = fin << 7;
			*out = *out | (0xF & opcode);
			*(out + 1) = mask << 7;
			if (msgLen < 126) {
				*(out + 1) = *(out + 1) | msgLen;
				headLen += 2;
			}
			else if (msgLen < 0xFFFF) {
				*(out + 1) = *(out + 1) | 0x7E; 
				std::uint16_t* tmp = (uint16_t*)(out + 2);
				*tmp = htons((std::uint16_t)msgLen);
				headLen += 4;
			}
			else {
				*(out + 1) = *(out + 1) | 0x7F;
				std::uint64_t* tmp = (uint64_t*)(out + 2);
				*tmp = htonl((std::uint64_t)msgLen);
				headLen += 10;
			}
			if (mask & 0x1) {
				std::memcpy(out + headLen, "0101", 4);
				/*for(UINT64 i = 0; i < msgLen; i++){
					int j = i % 4;
					message[i] = message[i] ^ *send[headLen+i];
				}*/
				headLen += 4;
			}
			std::memcpy((out)+headLen, message.c_str(), msgLen);
			*(out + slen) = '\0';
			return slen;
		}

		inline int get_pack_data(const std::string& message, std::string& outstr) {
			penv_.opcode = ws_header_.mark.opcode;
			std::size_t nPackLen = pack_data(message, outstr, penv_.fin, penv_.opcode, penv_.mask);
			penv_.reset();
			if (nPackLen <= 0) {
				return -2;
			}
			return nPackLen;
		}

		inline ProtoEnv* get_pack_env() { return &penv_; }
		inline WebSocketHeader* get_proto_heard() { return &ws_header_; }

		template<class Fn>
		inline void parse(const std::string& s, Fn&& fn) {
			auto slen = s.length();
			if (slen <= 0) {
				return;
			}
			rcv_buffer_.put(s.data(), slen);
			do {
				if (rcv_buffer_.rd_ready()) {
					bool ret = analysis_msg(rcv_buffer_.rd_buf(), rcv_buffer_.rd_size());
					if (!ret) {
						break;
					}
					if (rcv_buffer_.rd_size() >= (ws_header_.headlength + ws_header_.reallength)) {
						std::string sdata(reinterpret_cast<
							std::string::const_pointer>(&rcv_buffer_.rd_buf()[ws_header_.headlength]), ws_header_.reallength);
						if (this->mask_dec(sdata.data(), sdata.length())) {
							fn(ws_header_.mark.opcode, sdata);
							rcv_buffer_.rd_flip(ws_header_.headlength + ws_header_.reallength);
						}
					}
				}
			} while (0);
		}
		// 关闭握手日志：关闭code，关闭reason.
		inline void close_log(const std::string& closedata) {
			constexpr int codelen = sizeof(std::uint16_t);
			if (closedata.length() <= codelen) {
				return;
			}
			std::uint16_t close_code = ntohs(*(std::uint16_t*)(closedata.data()));
			std::string close_reason;
			int reasonLen = ws_header_.reallength - codelen;
			if (reasonLen > 0) {
				close_reason = closedata.substr(codelen, reasonLen);
			}
			std::cout << "websocket_close_handshark closecode:" << close_code << ", close reason:" << close_reason << std::endl;
		}
		
	private:
		HEADER_MAP header_map_;

		WebSocketHeader ws_header_;

		ProtoEnv penv_;

		t_buffer_cmdqueue<> rcv_buffer_;
		t_buffer_cmdqueue<> snd_buffer_;
	};
}

