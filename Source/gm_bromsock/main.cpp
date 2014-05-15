#define GMMODULE

#include "Gmod_Headers/Lua/Interface.h"
#include "Objects/BSEzSock.h"
#include "Objects/BSPacket.h"
#include "Objects/LockObject.h"

#ifdef _MSC_VER
#include <Windows.h>
#else
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#endif

using namespace BromScript;
#define GMOD_FUNCTION(fn) int fn(lua_State* state)
#define GETSOCK(num) (SockWrapper*)((GarrysMod::Lua::UserData*)LUA->GetUserdata(num))->data
#define GETPACK(num) (Packet*)((GarrysMod::Lua::UserData*)LUA->GetUserdata(num))->data
#define ADDFUNC(fn, f) LUA->PushCFunction(f); LUA->SetField(-2, fn);
#define UD_TYPE_SOCKET 122
#define UD_TYPE_PACKET 123

static int PacketRef = 0;
static int SocketRef = 0;

class SockWrapper;
static std::vector<SockWrapper*> AllocatedSockets;
static std::vector<Packet*> AllocatedPackets;

#ifdef _MSC_VER
DWORD WINAPI SockWorker(void* obj);
#else
void* SockWorker(void *obj);
#endif

enum class EventType {
	Connect, Send, Receive, Accept
};

class SockEvent{
public:
	EventType Type;
	void* data1;
	void* data2;
	void* data3;
};

class SockWrapper{
public:
	EzSock* Sock;
	lua_State* state;

	GarrysMod::Lua::UserData* UDPtr;
	int Callback_Receive;
	int Callback_Send;
	int Callback_Connect;
	int Callback_Disconnect;
	int Callback_Accept;
	int CurrentWorkers;
	bool DestoryWorkers;
	bool DidDisconnectCallback;
	
	std::vector<SockEvent*> Todo;
	std::vector<SockEvent*> Callbacks;
	LockObject Mutex;
	
	SockWrapper(lua_State* ls):Callback_Accept(-1),Callback_Receive(-1),Callback_Connect(-1),Callback_Send(-1),Callback_Disconnect(-1),CurrentWorkers(0),DestoryWorkers(false),DidDisconnectCallback(false){
		this->Sock = new EzSock();
		this->state = ls;
		
		// 2 threads, send and receive at the same time, SHOULD be possible.
#ifdef _MSC_VER
		CreateThread(null, null, SockWorker, this, null, null);
		CreateThread(null, null, SockWorker, this, null, null);
#else
		pthread_t uselessshit;
		pthread_create(&uselessshit, NULL, &SockWorker, this);
		pthread_create(&uselessshit, NULL, &SockWorker, this);
#endif
	}

	void PushToStack(lua_State* state){
		GarrysMod::Lua::UserData* ud = (GarrysMod::Lua::UserData*)LUA->NewUserdata(sizeof(GarrysMod::Lua::UserData));
		ud->data = this;
		ud->type = UD_TYPE_SOCKET;

		LUA->ReferencePush(SocketRef);
		LUA->SetMetaTable( -2 );
	}

	void Reset(){
		this->CallDisconnect();
		this->Sock->close();
		
		this->KillWorkers();

		delete this->Sock;
		this->Sock = new EzSock();

		this->DestoryWorkers = false;
#ifdef _MSC_VER
		CreateThread(null, null, SockWorker, this, null, null);
		CreateThread(null, null, SockWorker, this, null, null);
#else
		pthread_t uselessshit;
		pthread_create(&uselessshit, NULL, &SockWorker, this);
		pthread_create(&uselessshit, NULL, &SockWorker, this);
#endif
	}

	void CallDisconnect(){
		if (this->DidDisconnectCallback || this->Callback_Disconnect == -1) return;
		this->DidDisconnectCallback = true;

		LUA->ReferencePush(this->Callback_Disconnect);
		this->PushToStack(state);
		LUA->Call(1, 0);
	}

	void KillWorkers(){
		this->DestoryWorkers = true;
		bool isdone = false;
		while(!isdone){
#ifdef _MSC_VER
			Sleep(1);
#else
			sleep(1);
#endif
			
			this->Mutex.Lock();
			isdone = this->CurrentWorkers == 0;
			this->Mutex.Unlock();
		}
	}

	~SockWrapper(){
		if (this->Callback_Accept != -1) LUA->ReferenceFree(this->Callback_Accept);
		if (this->Callback_Connect != -1) LUA->ReferenceFree(this->Callback_Connect);
		if (this->Callback_Receive != -1) LUA->ReferenceFree(this->Callback_Receive);
		if (this->Callback_Send != -1) LUA->ReferenceFree(this->Callback_Send);
		if (this->Callback_Disconnect != -1) LUA->ReferenceFree(this->Callback_Disconnect);
		
		this->Sock->close();
		
		this->KillWorkers();

		delete this->Sock;
	}
};

#ifdef _MSC_VER
DWORD WINAPI SockWorker(void* obj){
#else
void* SockWorker(void *obj){
#endif

	SockWrapper* sock = (SockWrapper*)obj;
	sock->Mutex.Lock();
	sock->CurrentWorkers++;
	sock->Mutex.Unlock();

	while(true){
		if (sock->DestoryWorkers){
			sock->Mutex.Lock();
			sock->CurrentWorkers--;
			sock->Mutex.Unlock();
			return 0;
		}

		SockEvent* cur = null;
			
		sock->Mutex.Lock();
		if (sock->Todo.size() > 0){
			cur = sock->Todo[0];
			sock->Todo.erase(sock->Todo.begin());
		}
		sock->Mutex.Unlock();

		if (cur == null){
#ifdef _MSC_VER
			Sleep(1);
#else
			sleep(1);
#endif
			continue;
		}

		SockEvent* ne = new SockEvent();

		switch(cur->Type){
		case EventType::Accept:{
			SockWrapper* nsock = new SockWrapper(sock->state);
			if(!sock->Sock->accept(nsock->Sock)){
				delete nsock;

				ne->data1 = null;
			}else{
				ne->data1 = nsock;
			}

			ne->Type = EventType::Accept;
		}break;

		case EventType::Connect:{
			char* ip = (char*)cur->data1;
			unsigned short* port = (unsigned short*)cur->data2;

			bool ret = sock->Sock->connect(ip, *port) == 0;

			ne->Type = EventType::Connect;
			ne->data1 = new bool(ret);
			ne->data2 = ip;
			ne->data3 = port;
		}break;

		case EventType::Receive:{
			int* len = (int*)cur->data1;
			Packet* p = new Packet(sock->Sock);

			if ((*len == -1 && (*len = p->ReadInt()) == 0) || !p->CanRead(*len)){
				delete p;
				
				ne->data1 = null;
			}else{
				ne->data1 = p;
			}

			ne->Type = EventType::Receive;
		}break;

		case EventType::Send:{
			Packet* p = (Packet*)cur->data1;
			bool* sendsize = (bool*)cur->data2;
			int sent = 0;

			if (sendsize){
				sent = p->OutPos + 4;
				p->Sock = sock->Sock;
				p->Send();
			}else{
				int curpos = 0;
				while(curpos != p->OutPos){
					int ret = sock->Sock->SendRaw(p->OutBuffer + curpos, p->OutPos - curpos);
					if (ret <= 0){
						sock->Sock->Valid = false;
						break;
					}

					curpos += ret;
				}
				
				sent = curpos;
				p->Clear();
			}
			
			ne->data1 = new bool(sock->Sock->Valid);
			ne->data2 = new int(sent);
			ne->Type = EventType::Send;
		}break;

		}
		
		sock->Mutex.Lock();
		sock->Callbacks.push_back(ne);
		sock->Mutex.Unlock();
		delete cur;
	}

	return 0;
}

GMOD_FUNCTION(CreatePacket){
	GarrysMod::Lua::UserData* ud = (GarrysMod::Lua::UserData*)LUA->NewUserdata(sizeof(GarrysMod::Lua::UserData));
	ud->data = new Packet();
	ud->type = UD_TYPE_PACKET;

	if (LUA->IsType(1, UD_TYPE_SOCKET))
		((Packet*)ud->data)->Sock = (GETSOCK(1))->Sock;

	LUA->ReferencePush(PacketRef);
	LUA->SetMetaTable( -2 );

	AllocatedPackets.push_back((Packet*)ud->data);

	return 1;
}

GMOD_FUNCTION(CreateSocket){
	GarrysMod::Lua::UserData* ud = (GarrysMod::Lua::UserData*)LUA->NewUserdata(sizeof(GarrysMod::Lua::UserData));
	ud->data = new SockWrapper(state);
	ud->type = UD_TYPE_SOCKET;

	LUA->ReferencePush(SocketRef);
	LUA->SetMetaTable( -2 );

	AllocatedSockets.push_back((SockWrapper*)ud->data);
	
	return 1;
}

GMOD_FUNCTION(ThinkHook){
	for (size_t i = 0; i < AllocatedSockets.size(); i++){
		SockWrapper* sw = AllocatedSockets[i];

		while(true){
			SockEvent* se = null;
			sw->Mutex.Lock();
			if (sw->Callbacks.size() > 0){
				se = sw->Callbacks[0];
				sw->Callbacks.erase(sw->Callbacks.begin());
			}
			sw->Mutex.Unlock();

			if (se == null)
				break;

			switch(se->Type){
			case EventType::Connect:{
				bool* ret = (bool*)se->data1;
				char* ip = (char*)se->data2;
				unsigned short* port = (unsigned short*)se->data3;

				if (*ret) sw->DidDisconnectCallback = false;

				LUA->ReferencePush(sw->Callback_Connect);
				sw->PushToStack(state);
				LUA->PushBool(*ret);
				LUA->PushString(ip);
				LUA->PushNumber((double)*port);
				LUA->Call(4, 0);
				
				delete ret;
				delete[] ip;
				delete port;
			}break;

			case EventType::Accept:{
				SockWrapper* nsw = (SockWrapper*)se->data1;

				if (nsw != null){
					AllocatedSockets.push_back(nsw);

					LUA->ReferencePush(sw->Callback_Accept);
					sw->PushToStack(state);
					nsw->PushToStack(state);
					LUA->Call(2, 0);
				}else{
					sw->CallDisconnect();
				}
			}break;

			case EventType::Send:{
				bool* valid = (bool*)se->data1;
				int* size = (int*)se->data2;
				
				if (*valid){
					LUA->ReferencePush(sw->Callback_Send);
					sw->PushToStack(state);
					LUA->PushNumber((double)*size);
					LUA->Call(2, 0);
				}else{
					sw->CallDisconnect();
				}

				delete valid;
				delete size;
			}break;

			case EventType::Receive:{
				Packet* p = (Packet*)se->data1;

				if (p != null){
					AllocatedPackets.push_back(p);
					
					LUA->ReferencePush(sw->Callback_Receive);
					sw->PushToStack(state);

					GarrysMod::Lua::UserData* ud = (GarrysMod::Lua::UserData*)LUA->NewUserdata(sizeof(GarrysMod::Lua::UserData));
					ud->data = p;
					ud->type = UD_TYPE_PACKET;
					LUA->ReferencePush(PacketRef);
					LUA->SetMetaTable( -2 );
					LUA->Call(2, 0);
				}else{
					sw->CallDisconnect();
				}
			}break;
			}

			delete se;
		}
	}

	return 0;
}

GMOD_FUNCTION(SOCK_Connect){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	LUA->CheckType(2, GarrysMod::Lua::Type::STRING);
	LUA->CheckType(3, GarrysMod::Lua::Type::NUMBER);

	SockWrapper* s = GETSOCK(1);
	s->Sock->create();

	if (s->Callback_Connect > -1){
		const char* luaip = LUA->GetString(2);
		int iplen = strlen(luaip) + 1;
		char* ip = new char[iplen];
		memcpy(ip, luaip, iplen);

		SockEvent* se = new SockEvent();
		se->Type = EventType::Connect;
		se->data1 = ip;
		se->data2 = new unsigned short((unsigned short)LUA->GetNumber(3));

		s->Mutex.Lock();
		s->Todo.push_back(se);
		s->Mutex.Unlock();
		return 0;
	}else{
		LUA->PushBool(s->Sock->connect(LUA->GetString(2), (unsigned short)LUA->GetNumber(3)) == 0);
		return 1;
	}
}

GMOD_FUNCTION(SOCK_Listen){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	LUA->CheckType(2, GarrysMod::Lua::Type::NUMBER);

	EzSock* s = (GETSOCK(1))->Sock;
	s->create();

	LUA->PushBool(s->bind((unsigned short)LUA->GetNumber(2)) && s->listen());

	return 1;
}


GMOD_FUNCTION(SOCK_SetBlocking){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	LUA->CheckType(2, GarrysMod::Lua::Type::BOOL);

	(GETSOCK(1))->Sock->blocking = LUA->GetBool(2);

	return 0;
}

GMOD_FUNCTION(SOCK_Disconnect){
	LUA->CheckType(1, UD_TYPE_SOCKET);

	(GETSOCK(1))->Reset();

	return 0;
}

GMOD_FUNCTION(SOCK_Send){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	LUA->CheckType(2, UD_TYPE_PACKET);

	SockWrapper* s = (GETSOCK(1));
	Packet* p = GETPACK(2);
	bool sendsize = !LUA->IsType(3, GarrysMod::Lua::Type::BOOL) || !LUA->GetBool(3);
	
	if (s->Callback_Send > -1){
		SockEvent* se = new SockEvent();
		se->Type = EventType::Send;
		se->data1 = p;
		se->data2 = new bool(sendsize);
		
		s->Mutex.Lock();
		s->Todo.push_back(se);
		s->Mutex.Unlock();
		return 0;
	}else{
		if (sendsize){
			p->Sock = s->Sock;
			p->Send();

			LUA->PushBool(s->Sock->Valid);
			return 1;
		}else{
			int curpos = 0;
			while(curpos != p->OutPos){
				int ret = s->Sock->SendRaw(p->OutBuffer + curpos, p->OutPos - curpos);
				if (ret <= 0){
					s->Sock->Valid = false;
					break;
				}
				
				curpos += ret;
			}
			
			p->Clear();
			
			LUA->PushBool(s->Sock->Valid);
			return 1;
		}
	}

	return 0;
}

GMOD_FUNCTION(SOCK_Receive){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	int toread = LUA->IsType(2, GarrysMod::Lua::Type::NUMBER) ? (int)LUA->GetNumber(2) : -1;
	
	SockWrapper* s = GETSOCK(1);
	if (s->Callback_Receive > -1){
		SockEvent* se = new SockEvent();
		se->Type = EventType::Receive;
		se->data1 = new int(toread);

		s->Mutex.Lock();
		s->Todo.push_back(se);
		s->Mutex.Unlock();
		return 0;
	}else{
		Packet* p = new Packet(s->Sock);
		if (toread == -1){
			toread = p->ReadInt();
		}

		if (!p->CanRead(toread)){
			delete p;
			LUA->PushBool(false);
			return 1;
		}
		
		AllocatedPackets.push_back(p);

		GarrysMod::Lua::UserData* ud = (GarrysMod::Lua::UserData*)LUA->NewUserdata(sizeof(GarrysMod::Lua::UserData));
		ud->data = p;
		ud->type = UD_TYPE_PACKET;

		LUA->ReferencePush(PacketRef);
		LUA->SetMetaTable( -2 );
		return 1;
	}

}

GMOD_FUNCTION(SOCK_CALLBACKSend){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->CheckType(2, GarrysMod::Lua::Type::FUNCTION); LUA->Push(2); (GETSOCK(1))->Callback_Send = LUA->ReferenceCreate(); return 0; }
GMOD_FUNCTION(SOCK_CALLBACKReceive){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->CheckType(2, GarrysMod::Lua::Type::FUNCTION); LUA->Push(2); (GETSOCK(1))->Callback_Receive = LUA->ReferenceCreate(); return 0; }
GMOD_FUNCTION(SOCK_CALLBACKConnect){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->CheckType(2, GarrysMod::Lua::Type::FUNCTION); LUA->Push(2); (GETSOCK(1))->Callback_Connect = LUA->ReferenceCreate(); return 0; }
GMOD_FUNCTION(SOCK_CALLBACKAccept){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->CheckType(2, GarrysMod::Lua::Type::FUNCTION); LUA->Push(2); (GETSOCK(1))->Callback_Accept = LUA->ReferenceCreate(); return 0; }
GMOD_FUNCTION(SOCK_CALLBACKDisconnect){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->CheckType(2, GarrysMod::Lua::Type::FUNCTION); LUA->Push(2); (GETSOCK(1))->Callback_Disconnect = LUA->ReferenceCreate(); return 0; }

GMOD_FUNCTION(SOCK_Accept){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	SockWrapper* s = GETSOCK(1);

	if (s->Callback_Accept > -1){
		SockEvent* se = new SockEvent();
		se->Type = EventType::Accept;

		s->Mutex.Lock();
		s->Todo.push_back(se);
		s->Mutex.Unlock();
		return 0;
	}else{
		SockWrapper* csw = new SockWrapper(state);
		if (!s->Sock->accept(csw->Sock)){
			delete csw;

			LUA->PushBool(false);
			return 0;
		}

		GarrysMod::Lua::UserData* ud = (GarrysMod::Lua::UserData*)LUA->NewUserdata(sizeof(GarrysMod::Lua::UserData));
		ud->data = csw;
		ud->type = UD_TYPE_SOCKET;

		LUA->ReferencePush(SocketRef);
		LUA->SetMetaTable( -2 );

		AllocatedSockets.push_back(csw);

		return 1;
	}
}

GMOD_FUNCTION(SOCK_AddWorker){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	
#ifdef _MSC_VER
		CreateThread(null, null, SockWorker, GETSOCK(1), null, null);
#else
		pthread_t uselessshit;
		pthread_create(&uselessshit, NULL, &SockWorker, GETSOCK(1));
#endif

	return 0;
}

GMOD_FUNCTION(SOCK_SetTimeout){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	LUA->CheckType(2, GarrysMod::Lua::Type::NUMBER);
	SockWrapper* s = GETSOCK(1);
	
#ifdef _MSC_VER
	DWORD dwTime = (DWORD)LUA->GetNumber(2);
	setsockopt(s->Sock->sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&dwTime, sizeof(dwTime));
	setsockopt(s->Sock->sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&dwTime, sizeof(dwTime));
#else
	int dwTime = (int)LUA->GetNumber(2);
	setsockopt(s->Sock->sock, SOL_SOCKET, SO_SNDTIMEO, &dwTime, sizeof(dwTime));
	setsockopt(s->Sock->sock, SOL_SOCKET, SO_RCVTIMEO, &dwTime, sizeof(dwTime));
#endif

	return 0;
}

GMOD_FUNCTION(SOCK__TOSTRING){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	SockWrapper* s = GETSOCK(1);
	
	char* ipstr = inet_ntoa(s->Sock->addr.sin_addr);
	char buff[256];
#ifdef _MSC_VER
	sprintf_s(buff, "bromsock{%s:%d}", ipstr, s->Sock->addr.sin_port);
#else
	sprintf(buff, "bromsock{%s:%d}", ipstr, s->Sock->addr.sin_port);
#endif

	LUA->PushString(buff);

	return 1;
}

GMOD_FUNCTION(SOCK__EQ){
	LUA->CheckType(1, UD_TYPE_SOCKET);
	LUA->CheckType(2, UD_TYPE_SOCKET);

	LUA->PushBool(GETSOCK(1) == GETSOCK(2));

	return 1;
}

GMOD_FUNCTION(PACK__TOSTRING){
	LUA->CheckType(1, UD_TYPE_PACKET);
	Packet* p = GETPACK(1);
	
	char buff[256];
#ifdef _MSC_VER
	sprintf_s(buff, "brompacket{i:%d,o:%d}", p->InSize, p->OutSize);
#else
	sprintf(buff, "brompacket{i:%d,o:%d}", p->InSize, p->OutSize);
#endif

	LUA->PushString(buff);

	return 1;
}

GMOD_FUNCTION(PACK__EQ){
	LUA->CheckType(1, UD_TYPE_PACKET);
	LUA->CheckType(2, UD_TYPE_PACKET);

	LUA->PushBool(GETPACK(1) == GETPACK(2));

	return 1;
}

GMOD_FUNCTION(PACK_WRITEByte){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->WriteByte((unsigned char)LUA->GetNumber(2)); return 0; }
GMOD_FUNCTION(PACK_WRITEShort){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->WriteShort((short)LUA->GetNumber(2)); return 0; }
GMOD_FUNCTION(PACK_WRITEFloat){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->WriteFloat((float)LUA->GetNumber(2)); return 0; }
GMOD_FUNCTION(PACK_WRITEInt){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->WriteInt((int)LUA->GetNumber(2)); return 0; }
GMOD_FUNCTION(PACK_WRITEDouble){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->WriteDouble(LUA->GetNumber(2)); return 0; }
GMOD_FUNCTION(PACK_WRITELong){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->WriteLong((long long)LUA->GetNumber(2)); return 0; }
GMOD_FUNCTION(PACK_WRITEString){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->WriteString(LUA->GetString(2)); return 0; }

GMOD_FUNCTION(PACK_READByte){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->ReadByte()); return 1; }
GMOD_FUNCTION(PACK_READShort){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->ReadShort()); return 1; }
GMOD_FUNCTION(PACK_READFloat){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->ReadFloat()); return 1; }
GMOD_FUNCTION(PACK_READInt){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->ReadInt()); return 1; }
GMOD_FUNCTION(PACK_READDouble){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->ReadDouble()); return 1; }
GMOD_FUNCTION(PACK_READLong){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->ReadLong()); return 1; }
GMOD_FUNCTION(PACK_READString){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushString((GETPACK(1))->ReadString()); return 1; }

GMOD_FUNCTION(PACK_InSize){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->InSize); return 1; }
GMOD_FUNCTION(PACK_InPos){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->InPos); return 1; }
GMOD_FUNCTION(PACK_OutSize){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->OutSize); return 1; }
GMOD_FUNCTION(PACK_OutPos){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushNumber((double)(GETPACK(1))->OutPos); return 1; }
GMOD_FUNCTION(PACK_CLEAR){ LUA->CheckType(1, UD_TYPE_PACKET); (GETPACK(1))->Clear(); return 0; }

GMOD_FUNCTION(PACK_IsValid){ LUA->CheckType(1, UD_TYPE_PACKET); LUA->PushBool((GETPACK(1))->Valid); return 1; }
GMOD_FUNCTION(SOCK_IsValid){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->PushBool((GETSOCK(1))->Sock->Valid); return 1; }
GMOD_FUNCTION(SOCK_GetIP){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->PushString(inet_ntoa((GETSOCK(1))->Sock->addr.sin_addr)); return 1; }
GMOD_FUNCTION(SOCK_GetPort){ LUA->CheckType(1, UD_TYPE_SOCKET); LUA->PushNumber((GETSOCK(1))->Sock->addr.sin_port); return 1; }

GMOD_MODULE_OPEN(){
	LUA->CreateTable();
		ADDFUNC("SetBlocking", SOCK_SetBlocking);
		ADDFUNC("Connect", SOCK_Connect);
		ADDFUNC("Close", SOCK_Disconnect);
		ADDFUNC("Disconnect", SOCK_Disconnect);
		ADDFUNC("Listen", SOCK_Listen);
		ADDFUNC("Send", SOCK_Send);
		ADDFUNC("Accept", SOCK_Accept);
		ADDFUNC("Receive", SOCK_Receive);
		ADDFUNC("GetIP", SOCK_GetIP);
		ADDFUNC("GetPort", SOCK_GetPort);
		ADDFUNC("AddWorker", SOCK_AddWorker);
		ADDFUNC("SetTimeout", SOCK_SetTimeout);
		ADDFUNC("SetCallbackReceive", SOCK_CALLBACKReceive);
		ADDFUNC("SetCallbackSend", SOCK_CALLBACKSend);
		ADDFUNC("SetCallbackConnect", SOCK_CALLBACKConnect);
		ADDFUNC("SetCallbackAccept", SOCK_CALLBACKAccept);
		ADDFUNC("SetCallbackDisconnect", SOCK_CALLBACKDisconnect);
	int socktableref = LUA->ReferenceCreate();

	LUA->CreateTable();
		ADDFUNC("WriteByte", PACK_WRITEByte);
		ADDFUNC("WriteShort", PACK_WRITEShort);
		ADDFUNC("WriteFloat", PACK_WRITEFloat);
		ADDFUNC("WriteInt", PACK_WRITEInt);
		ADDFUNC("WriteDouble", PACK_WRITEDouble);
		ADDFUNC("WriteLong", PACK_WRITELong);
		ADDFUNC("WriteString", PACK_WRITEString);
		ADDFUNC("ReadByte", PACK_READByte);
		ADDFUNC("ReadShort", PACK_READShort);
		ADDFUNC("ReadFloat", PACK_READFloat);
		ADDFUNC("ReadInt", PACK_READInt);
		ADDFUNC("ReadDouble", PACK_READDouble);
		ADDFUNC("ReadLong", PACK_READLong);
		ADDFUNC("ReadString", PACK_READString);
		ADDFUNC("InSize", PACK_InSize);
		ADDFUNC("InPos", PACK_InPos);
		ADDFUNC("OutSize", PACK_OutSize);
		ADDFUNC("OutPos", PACK_OutPos);
		ADDFUNC("Clear", PACK_CLEAR);
	int packtableref = LUA->ReferenceCreate();

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->PushString("BromSock");
	LUA->PushCFunction(CreateSocket);
	LUA->SetTable(-3);

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->PushString("BromPacket");
	LUA->PushCFunction(CreatePacket);
	LUA->SetTable(-3);

	LUA->CreateTable();
		LUA->ReferencePush(packtableref);
		LUA->SetField(-2, "__index");
		LUA->ReferencePush(packtableref);
		LUA->SetField(-2, "__newindex");
		ADDFUNC("__tostring", PACK__TOSTRING);
		ADDFUNC("__eq", PACK__EQ);
	PacketRef = LUA->ReferenceCreate();

	LUA->CreateTable();
		LUA->ReferencePush(socktableref);
		LUA->SetField(-2, "__index");
		LUA->ReferencePush(socktableref);
		LUA->SetField(-2, "__newindex");
		ADDFUNC("__tostring", SOCK__TOSTRING);
		ADDFUNC("__eq", SOCK__EQ);

	SocketRef = LUA->ReferenceCreate();

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
	LUA->GetField(-1, "hook");
	LUA->GetField(-1, "Add");
	LUA->PushString("Think");
	LUA->PushString("BS::THINK");
	LUA->PushCFunction(ThinkHook);
	LUA->Call(3, 0);
	LUA->Pop();
	
	// clean up references
	LUA->ReferenceFree(socktableref);
	LUA->ReferenceFree(packtableref);

	return 0;
}

GMOD_MODULE_CLOSE(){
	for(SockWrapper* sw : AllocatedSockets){
		delete sw;
		LUA->ReferenceFree(SocketRef);
	}

	for(Packet* p : AllocatedPackets){
		delete p;
		LUA->ReferenceFree(PacketRef);
	}

	return 0;
}