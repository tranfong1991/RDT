/*
	Name: Phong Tran
	Class: CSCE 463 - 500
*/

#pragma once
#include <WinSock2.h>

#define MAGIC_PROTOCOL 0x8311AA
#define FORWARD_PATH 0
#define RETURN_PATH 1 

#pragma pack(push, 1)
struct Flags{
	DWORD reserved : 5;	//must be zero
	DWORD SYN : 1;
	DWORD ACK : 1;
	DWORD FIN : 1;
	DWORD magic : 24;

	Flags(){
		memset(this, 0, sizeof(*this));
		magic = MAGIC_PROTOCOL;
	}
};

struct SenderDataHeader{
	Flags flags;
	DWORD seq;	//start from zero
};

// transfer parameters
struct LinkProperties {
	float RTT;			// propagation RTT (in sec)
	float speed;		// bottleneck bandwidth (in bits/sec)
	float pLoss[2];		// probability of loss in each direction
	DWORD bufferSize;	// buffer size of emulated routers (in packets)

	LinkProperties() 
	{
		memset(this, 0, sizeof(*this));
	}

	LinkProperties& operator=(const LinkProperties& p) 
	{
		RTT = p.RTT;
		speed = p.speed;
		pLoss[FORWARD_PATH] = p.pLoss[FORWARD_PATH];
		pLoss[RETURN_PATH] = p.pLoss[RETURN_PATH];
		bufferSize = p.bufferSize;

		return *this;
	}
};

struct SenderSynHeader {
	SenderDataHeader sdh;
	LinkProperties lp;
};

struct ReceiverHeader {
	Flags flags;
	DWORD recvWnd;		// receiver window for flow control (in pkts)
	DWORD ackSeq;		// ack value = next expected sequence
};
#pragma pack(pop)