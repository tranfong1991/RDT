/*
	Name: Phong Tran
	Class: CSCE 463 - 500
*/

#pragma once
#include "CircularArray.h"
#include "PacketHeaders.h"
#include "Utils.h"
#include <iostream>
#include <queue>

#define MAGIC_PORT 22345		//receiver listens on this port
#define MAX_PKT_SIZE (1500-28)	//maximum UDP packet size accepted by receiver
#define MAX_SYN_ATTEMPS 3
#define MAX_RETX_ATTEMPS 50		//including retransmission

//possible status codes from ss.open, ss.send, ss.close
#define STATUS_OK 0			// no error
#define ALREADY_CONNECTED 1 // second call to ss.open() without closing connection
#define NOT_CONNECTED 2		// call to ss.send()/close() without ss.open()
#define INVALID_NAME 3		// ss.open() with targetHost that has no DNS entry
#define FAILED_SEND 4		// sendto() failed in kernel
#define TIMEOUT 5			// timeout
#define FAILED_RECV 6		// recvfrom() failed in kernel
#define INVALID_PKT_SIZE 7	// packet size > MAX_PKT_SIZE
#define MAX

struct SendThreadParams
{
	HANDLE mutex;
	HANDLE emptySemaphore;
	HANDLE abortSendEvent;
};

//statistics to be displayed by the stat thread
struct Statistics
{
	DWORD senderBase = 0;
	float estRTT = 0.0;
	float mbAcked = 0.0;
	int packetTimeouts = 0;
	int fastRetransmit = 0;
	int effectiveWindowSize = 0;
};

class SenderSocket
{
	SOCKET sock;
	DWORD seq = 0;
	Statistics stats;
	sockaddr_in receiver;

	double rto;	
	double estRTT = 0.0;
	double devRTT = 0.0;
	double startTransmit;
	int senderWindow;
	int lastReleased = 0;
	bool timeoutPending = false;

	bool opened = false;
	bool closed = true;

	char* makePacket(int size, DWORD seqNum, char* data, int bytes);
	int handleSYNFIN(bool SYN, clock_t startTime, LinkProperties* lp = NULL);
public:
	SenderSocket();
	~SenderSocket();

	bool setupSocket();

	DWORD getLastSeq() { return seq; }
	double getRTO() { return rto; }
	double getTransmitTime() { return startTransmit; }
	int getSenderWindow(){ return senderWindow; }
	Statistics& getStats() { return stats; }
	bool isTimeoutPending() { return timeoutPending; }

	int sendOnePacket(char* buf, int bytes);
	int receiveOnePacket(bool retransmit, ReceiverHeader* rh, double timeout, CircularArray* packetQueue, HANDLE emptySemaphore, HANDLE mutex);

	int open(char* targetHost, int port, int senderWindow, LinkProperties* lp, clock_t startTime);		//establish connection
	int send(char* data, int bytes, CircularArray* packetQueue, SendThreadParams* param);				//assume producer role
	int close(clock_t startTime);													//close connection
};

