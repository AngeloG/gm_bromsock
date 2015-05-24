#ifndef CLASS_PACKET
#define CLASS_PACKET

#include "BSEzSock.h"

#ifndef null
#define null 0
#endif

namespace BromScript{
	class Packet{
	public:
		// 0 == system endian, defaults to this
		// 1 == big endian
		// 2 == little endian
		unsigned char EndianType;

		int RefCount;
		bool Valid;
		EzSock* Sock;
		unsigned int OutPos;
		unsigned int OutSize;
		unsigned int InPos;
		unsigned int InSize;
		unsigned char* InBuffer;
		unsigned char* OutBuffer;

		Packet();
		Packet(EzSock*);
		~Packet();

		void Clear();

		void WriteByte(unsigned char);
		void WriteBool(bool);
		void WriteBytes(unsigned char* arr, int size, bool sendsize);
		void WriteShort(short);
		void WriteUShort(unsigned short);
		void WriteInt(int);
		void WriteUInt(unsigned int);
		void WriteLong(long long);
		void WriteULong(unsigned long long);
		void WriteFloat(float);
		void WriteDouble(double);
		void WriteString(const char*);
		void WriteStringNT(const char*);
		void WriteStringRaw(const char*, unsigned int);
		void WriteLine(const char*);

		unsigned char ReadByte();
		bool ReadBool();
		unsigned char* ReadBytes(int len);
		short ReadShort();
		unsigned short ReadUShort();
		int ReadInt();
		unsigned int ReadUInt();
		long long ReadLong();
		unsigned long long ReadULong();
		float ReadFloat();
		double ReadDouble();
		char* ReadString(int len = -1);
		char* ReadUntil(const char* seq, unsigned int seqsize, unsigned int* outlen);
		char* ReadStringNT(unsigned int* outlen);
		char* ReadStringAll(unsigned int* outlen);
		
		bool CanRead(int len);
		bool CanRead(char* seq);
		bool HasDataLeft();
		int DataLeft();

		void Send();

		void CheckSpaceOut(int);
		void AllocateMoreSpaceOut(int);
		void CheckSpaceIn(int);
		void AllocateMoreSpaceIn(int);
	};
}

#endif