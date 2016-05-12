/*
	Name: Phong Tran
	Class: CSCE 463 - 500
	Acknowledgement: homework handout, course powerpoint slides
*/

#pragma once
#include "SenderSocket.h"
#include "Checksum.h"

struct StatThreadParams
{
	HANDLE eventStatFinished;
	clock_t startConstructSS;
	Statistics* statistics;
};

struct AckThreadParams
{
	HANDLE mutex;
	HANDLE emptySemaphore;
	HANDLE abortSendEvent;
	HANDLE finishAckEvent;
	SenderSocket* senderSocket;
};

static CircularArray* packetQueue = NULL;

DWORD WINAPI statRun(LPVOID param)
{
	StatThreadParams* p = (StatThreadParams*)param;
	clock_t start = p->startConstructSS;
	Statistics* stats = p->statistics;
	float goodPut = 0.0;
	int prevNumPackets = 0;

	//print every 2 seconds
	while (WaitForSingleObject(p->eventStatFinished, 2000) == WAIT_TIMEOUT){
		goodPut = (stats->senderBase - prevNumPackets) * 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) / (1e6 * 2);
		prevNumPackets = stats->senderBase;

		printf("[%5.2f] B %5d (%5.1f MB) N %5d T %d F %d W %d S %.3f Mbps RTT %.3f\n",
			Utils::duration(start, clock()) / 1000.0, stats->senderBase, stats->mbAcked, stats->senderBase + stats->effectiveWindowSize,
			stats->packetTimeouts, stats->fastRetransmit, stats->effectiveWindowSize, goodPut, stats->estRTT);
	}

	return 0;
}

DWORD WINAPI ackRun(LPVOID param)
{
	AckThreadParams* p = (AckThreadParams*)param;
	SenderSocket* ss = p->senderSocket;
	Statistics& stats = ss->getStats();

	while (!ss->isTimeoutPending())
		Sleep(10);

	while (stats.senderBase != ss->getLastSeq()){
		int frontIndex = packetQueue->getFrontIndex();
		for (int i = 0; i < stats.effectiveWindowSize; i++){
			Packet& packet = packetQueue->getElemAt(frontIndex % ss->getSenderWindow());
			if (!packet.isSent){
				packet.isSent = true;
				ss->sendOnePacket(packet.data, packet.size);
			}
			frontIndex++;
		}

		int retransmitCount = 0;
		bool retransmit = false;
		double timerExpires = ss->getTransmitTime() + ss->getRTO();
		while (retransmitCount < MAX_RETX_ATTEMPS){
			double timeout = timerExpires - (clock() / (double)CLOCKS_PER_SEC);

			ReceiverHeader rh;
			int ret = ss->receiveOnePacket(retransmit, &rh, timeout, packetQueue, p->emptySemaphore, p->mutex);

			if (ret == FAILED_RECV){
				SetEvent(p->abortSendEvent);
				return 0;
			}
			else if (ret == TIMEOUT){
				stats.packetTimeouts++;

				Packet& packet = packetQueue->getElemAt(stats.senderBase % ss->getSenderWindow());
				ss->sendOnePacket(packet.data, packet.size);

				double newRTO = (1 << retransmitCount) * ss->getRTO();
				timerExpires = (clock() / (double)CLOCKS_PER_SEC) + newRTO;
				retransmitCount++;
				retransmit = true;
			}
			else if (ret == STATUS_OK){
				break;
			}
		}

		if (retransmitCount == MAX_RETX_ATTEMPS)
			SetEvent(p->abortSendEvent);
	}
	return 0;
}

int main(int argc, char* argv[])
{
	try {
		//check for 7 arguments
		if (argc != 8){
			printf("Failed with too few or too many arguments\n");
			return 0;
		}

		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){
			printf("WSAStartup error %d\n", WSAGetLastError());
			WSACleanup();
			return 0;
		}

		char* targetHost = argv[1];
		int power = atoi(argv[2]);
		int senderWindow = atoi(argv[3]);
		float rtt = atof(argv[4]);
		float fLossRate = atof(argv[5]);
		float rLossRate = atof(argv[6]);
		float linkSpeed = atof(argv[7]);
		printf("Main:	sender W = %d, RTT %.3f sec, loss %g / %g, link %.0f Mbps\n", senderWindow, rtt, fLossRate, rLossRate, linkSpeed);

		//initialize buffer
		clock_t startInit = clock();
		UINT64 dwordBufSize = (UINT64)1 << power;
		DWORD* dwordBuf = new DWORD[dwordBufSize];
		for (UINT64 i = 0; i < dwordBufSize; i++)
			dwordBuf[i] = i;
		clock_t endInit = clock();
		printf("Main:	initializing DWORD array with 2^%d elements... done in %.0f ms\n", power, Utils::duration(startInit, endInit));

		LinkProperties lp;
		lp.RTT = rtt;
		lp.speed = 1e6 * linkSpeed;
		lp.pLoss[FORWARD_PATH] = fLossRate;
		lp.pLoss[RETURN_PATH] = rLossRate;

		clock_t startConstructSS = clock();
		SenderSocket ss;
		if (!ss.setupSocket()){
			WSACleanup();
			return 0;
		}
		
		//setup connection
		int status;
		clock_t startOpen = clock();
		if ((status = ss.open(targetHost, MAGIC_PORT, senderWindow, &lp, startConstructSS)) != STATUS_OK){
			printf("Main:	connect failed with status %d\n\n", status);
			WSACleanup();
			return 0;
		}
		clock_t endOpen = clock();
		printf("Main:	connected to %s in %.3f sec, pkt %d bytes\n", targetHost, Utils::duration(startOpen, endOpen) / 1000.0, MAX_PKT_SIZE);

		char* charBuf = (char*)dwordBuf;
		UINT64 byteBufferSize = dwordBufSize << 2;

		HANDLE statThread;
		StatThreadParams statParams;
		statParams.eventStatFinished = CreateEvent(NULL, true, false, NULL);
		statParams.startConstructSS = startConstructSS;
		statParams.statistics = &(ss.getStats());
		statThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statRun, &statParams, 0, NULL);

		HANDLE mutex = CreateMutex(NULL, 0, NULL);
		HANDLE emptySemaphore = CreateSemaphore(NULL, senderWindow, senderWindow, NULL);
		HANDLE abortSendEvent = CreateEvent(NULL, true, false, NULL);
		HANDLE finishAckEvent = CreateEvent(NULL, true, false, NULL);

		SendThreadParams sendParams;
		sendParams.mutex = mutex;
		sendParams.emptySemaphore = emptySemaphore;
		sendParams.abortSendEvent = abortSendEvent;

		HANDLE ackThread;
		AckThreadParams ackParams;
		ackParams.mutex = mutex;
		ackParams.emptySemaphore = emptySemaphore;
		ackParams.abortSendEvent = abortSendEvent;
		ackParams.finishAckEvent = finishAckEvent;
		ackParams.senderSocket = &ss;
		ackThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ackRun, &ackParams, 0, NULL);

		packetQueue = new CircularArray(senderWindow);

		//send data
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
		UINT64 off = 0;
		clock_t startSend = clock();
		while (off < byteBufferSize){
			int bytes = min(byteBufferSize - off, MAX_PKT_SIZE - sizeof(SenderDataHeader));
			if ((status = ss.send(charBuf + off, bytes, packetQueue, &sendParams)) != STATUS_OK){
				printf("Main:	send failed with status %d\n\n", status);
				WSACleanup();
				return 0;
			}
			off += bytes;
		}
		clock_t endSend = clock();

		//end ack thread. NEVER ENDS, FIX THIS
		WaitForSingleObject(ackThread, INFINITE);
		CloseHandle(ackThread);

		//end stat thread
		SetEvent(statParams.eventStatFinished);
		WaitForSingleObject(statThread, INFINITE);
		CloseHandle(statThread);

		//close connection
		double elapsedTime = Utils::duration(startSend, endSend) / 1000.0;
		if ((status = ss.close(startConstructSS)) != STATUS_OK){
			printf("Main:	connect failed with status %d\n\n", status);
			WSACleanup();
			return 0;
		}

		//display checksum
		Checksum cs;
		DWORD checksum = cs.crc32((unsigned char*)charBuf, byteBufferSize);
		const Statistics& stats = ss.getStats();
		printf("Main:	transfer finished in %.3f sec, %.2f Kbps, checksum %.8X\n", 
			elapsedTime, (byteBufferSize * 8) / (1000.0 * elapsedTime), checksum);
		printf("Main:	estRTT %.3f, ideal rate %.2f Kbps\n\n",
			stats.estRTT, (MAX_PKT_SIZE - sizeof(SenderDataHeader)) * 8 * stats.effectiveWindowSize / (1000.0 * stats.estRTT));

		delete packetQueue;
		WSACleanup();
		return 0;
	}
	catch (...){
		printf("Unknown Exception!\n");
		return 0;
	}
}