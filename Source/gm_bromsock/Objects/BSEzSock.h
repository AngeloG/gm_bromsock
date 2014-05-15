#ifndef BSEzSock_H
#define BSEzSock_H

#define _WINSOCKAPI_

#include <sstream>
#include <vector>
#include <fcntl.h>
#include <ctype.h>

#ifdef _MSC_VER
#include <winsock2.h>
#else
#include <netinet/in.h>
#endif

namespace BromScript{
	class Packet;

	using namespace std;

	class EzSock {
	public:
		enum SockState{
			skDISCONNECTED = 0, 
			skUNDEF1, //Not implemented
			skLISTENING, 
			skUNDEF3, //Not implemented
			skUNDEF4, //Not implemented
			skUNDEF5, //Not implemented
			skUNDEF6, //Not implemented
			skCONNECTED, 
			skERROR 
		};

		bool blocking;
		bool Valid;

		struct sockaddr_in addr;
		struct sockaddr_in fromAddr;
		unsigned long fromAddr_len;

		SockState state;

		int lastCode;

		EzSock();
		~EzSock();

		bool create();
		bool create(int Protocol);
		bool create(int Protocol, int Type);
		bool bind(unsigned short port);
		bool listen();
		bool accept(EzSock* socket);
		int connect(const char* host, unsigned short port);
		void close();

		long uAddr();
		bool IsError();

		bool CanRead();

		int sock;
		int Receive(unsigned char* buffer, int size, int spos = 0);
		int SendRaw(unsigned char* data, int dataSize);
		int SendUDP(unsigned char* buffer, int size, sockaddr* to);
		int ReceiveUDP(unsigned char* buffer, int size, sockaddr* from);

	private:
#ifdef _MSC_VER
		WSADATA wsda;
#endif
		int MAXCON;

		fd_set  *scks;
		timeval *times;

		unsigned int totaldata;
		bool check();
	};
}

#endif
