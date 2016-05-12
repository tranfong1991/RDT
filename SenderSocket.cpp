/*
	Name: Phong Tran
	Class: CSCE 463 - 500
*/

#include "SenderSocket.h"

SenderSocket::SenderSocket()
{
	stats.senderBase = seq;
}

SenderSocket::~SenderSocket()
{
	closesocket(sock);
}

char* SenderSocket::makePacket(int size, DWORD seqNum, char* data, int bytes)
{
	char* pkt = new char[size];

	//create sdh at a specific memory address (i.e. beginning of pkt)
	SenderDataHeader* sdh = new (pkt)SenderDataHeader();
	sdh->seq = seqNum;

	char* dataPtr = pkt + sizeof(SenderDataHeader);
	Utils::copy(dataPtr, data, bytes);

	return pkt;
}

//by default, lp is NULL, only used in case of SYN
int SenderSocket::handleSYNFIN(bool SYN, clock_t startTime, LinkProperties* lp)
{
	SenderSynHeader ssh;
	SenderDataHeader sdh;
	void* ptr;
	int size;
	int maxCount;

	if (SYN){
		lp->bufferSize = senderWindow + MAX_RETX_ATTEMPS;
		ssh.lp = *lp;
		ssh.sdh.flags.SYN = 1;
		ssh.sdh.seq = seq;

		rto = max(1, 2 * lp->RTT);
		ptr = (void*)&ssh;
		size = sizeof(ssh);
		maxCount = MAX_SYN_ATTEMPS;
	}
	else {
		sdh.flags.FIN = 1;
		sdh.seq = seq;

		ptr = (void*)&sdh;
		size = sizeof(sdh);
		maxCount = MAX_RETX_ATTEMPS;
	}

	FD_SET fd;
	struct timeval timeout;
	int count = 1;
	float tempRTO = rto;

	while (count <= maxCount){
		timeout.tv_sec = floor(tempRTO);
		timeout.tv_usec = (tempRTO - floor(tempRTO)) * 1e6;

		clock_t startTransmit = clock();
		if (sendto(sock, (char*)ptr, size, 0, (struct sockaddr*)&receiver, sizeof(receiver)) == SOCKET_ERROR){
			printf("[%5.2f]	--> failed sendto with %d\n", Utils::duration(startTime, startTransmit) / 1000.0, WSAGetLastError());
			return FAILED_SEND;
		}

		FD_ZERO(&fd);
		FD_SET(sock, &fd);

		bool successRecv = false;
		clock_t startSelect = clock();
		while (select(0, &fd, 0, 0, &timeout) > 0){
			clock_t endSelect = clock();
			ReceiverHeader rh;

			int len = sizeof(receiver);
			if (recvfrom(sock, (char*)&rh, sizeof(rh), 0, (struct sockaddr*)&receiver, &len) == SOCKET_ERROR){
				printf("[%5.2f]	<-- failed recvfrom with %d\n", Utils::duration(startTime, clock()) / 1000.0, WSAGetLastError());
				return FAILED_RECV;
			}
			clock_t endTransmit = clock();

			if (SYN && rh.flags.SYN == 1 && rh.flags.ACK == 1){
				rto = 3 * Utils::duration(startTransmit, endTransmit) / 1000.0;
				stats.effectiveWindowSize = min(senderWindow, rh.recvWnd);
				successRecv = true;
				break;
			}
			else if (!SYN && rh.flags.FIN == 1 && rh.flags.ACK == 1){
				printf("[%5.2f]	<-- FIN-ACK %d window %.8X\n",
					Utils::duration(startTime, endTransmit) / 1000.0, rh.ackSeq, rh.recvWnd);
				successRecv = true;
				break;
			}
			
			//wait the rest of the timeout
			FD_ZERO(&fd);
			FD_SET(sock, &fd);
			float remains = tempRTO - Utils::duration(startSelect, endSelect) / 1000.0;
			timeout.tv_sec = floor(remains);
			timeout.tv_usec = (remains - floor(remains)) * 1e6;
		}
		if (successRecv)
			break;

		count++;
		tempRTO *= 2;
	}

	if (count > maxCount)
		return TIMEOUT;

	opened = SYN ? true : false;
	closed = SYN ? false : true;

	return STATUS_OK;
}

bool SenderSocket::setupSocket()
{
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == INVALID_SOCKET){
		printf("socket error %d\n\n", WSAGetLastError());
		return false;
	}

	int kernelBuffer = 2e6;
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&kernelBuffer, sizeof(int)) == SOCKET_ERROR){
		printf("socket error %d\n\n", WSAGetLastError());
		return false;
	}

	kernelBuffer = 20e6;
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&kernelBuffer, sizeof(int)) == SOCKET_ERROR){
		printf("socket error %d\n\n", WSAGetLastError());
		return false;
	}

	u_long imode = 1;
	if (ioctlsocket(sock, FIONBIO, &imode) == SOCKET_ERROR){
		printf("socket error %d\n\n", WSAGetLastError());
		return false;
	}

	struct sockaddr_in local;
	memset(&local, 0, sizeof(local));
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(0);
	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR){
		printf("socket error %d\n\n", WSAGetLastError());
		return false;
	}
	return true;
}

int SenderSocket::open(char* targetHost, int port, int sw, LinkProperties* lp, clock_t startTime)
{
	//check if close() has been called for the previous connection
	if (!closed)
		return ALREADY_CONNECTED;

	senderWindow = sw;
	DWORD ip = inet_addr(targetHost);
	char* host = targetHost;

	//get dns name
	if (ip == INADDR_NONE){
		struct hostent* remote = gethostbyname(targetHost);
		if (remote == NULL){
			printf("[%.3f] target %s is invalid\n", Utils::duration(startTime, clock()) / 1000.0, targetHost);
			return INVALID_NAME;
		}
		host = inet_ntoa(*((struct in_addr *) remote->h_addr));
	}

	//configure receiver
	memset(&receiver, 0, sizeof(receiver));
	receiver.sin_family = AF_INET;
	receiver.sin_addr.s_addr = inet_addr(host);
	receiver.sin_port = htons(port);

	return handleSYNFIN(true, startTime, lp);
}

//pessimistic: select then sendto
int SenderSocket::sendOnePacket(char* data, int size)
{
	FD_SET fd;
	FD_ZERO(&fd);
	FD_SET(sock, &fd);
	
	if (select(0, NULL, &fd, 0, NULL) > 0){
		if (sendto(sock, data, size, 0, (struct sockaddr*)&receiver, sizeof(receiver)) == SOCKET_ERROR){
			printf("Failed sendto with %d\n", WSAGetLastError());
			return FAILED_SEND;
		}
	}
	return STATUS_OK;
}

int SenderSocket::receiveOnePacket(bool retransmit, ReceiverHeader* rh, double time_out, CircularArray* packetQueue, HANDLE emptySemaphore, HANDLE mutex)
{
	FD_SET fd;
	int dupCount = 0;
	struct timeval timeout;

	FD_ZERO(&fd);
	FD_SET(sock, &fd);
	timeout.tv_sec = floor(time_out);
	timeout.tv_usec = (time_out - floor(time_out)) * 1e6;

	clock_t startSelect = clock();
	while (select(0, &fd, 0, 0, &timeout) > 0){
		clock_t endSelect = clock();
		
		int len = sizeof(receiver);
		if (recvfrom(sock, (char*)rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&receiver, &len) == SOCKET_ERROR)
			return FAILED_RECV;

		//check for valid ACK
		if (rh->flags.ACK == 1 && rh->ackSeq >= stats.senderBase && rh->ackSeq <= (stats.senderBase + stats.effectiveWindowSize)){
			if (rh->ackSeq > stats.senderBase){
				if (!retransmit){
					double endTransmit = clock() / (double)CLOCKS_PER_SEC;
					double sampleRTT = endTransmit - startTransmit;

					estRTT = estRTT == 0.0 ? sampleRTT : (1 - 0.125) * estRTT + 0.125 * sampleRTT;
					devRTT = devRTT == 0.0 ? abs(sampleRTT - estRTT) : (1 - 0.25) * devRTT + 0.25 * abs(sampleRTT - estRTT);
					rto = estRTT + 4 * max(devRTT, 0.010);
				}

				stats.estRTT = estRTT;
				stats.mbAcked += MAX_PKT_SIZE / 1e6;

				//start timer
				startTransmit = clock() / (double)CLOCKS_PER_SEC;

				DWORD slot = rh->ackSeq - stats.senderBase;
				stats.senderBase = rh->ackSeq;
				stats.effectiveWindowSize = min(senderWindow, rh->recvWnd);
				ReleaseSemaphore(emptySemaphore, slot, NULL);

				WaitForSingleObject(mutex, INFINITE);
				for (DWORD i = 0; i < slot; i++)
					packetQueue->pop();
				ReleaseMutex(mutex);

				return STATUS_OK;
			}
			else if (rh->ackSeq == stats.senderBase){
				//handle dupACK
				if (++dupCount == 3){
					stats.fastRetransmit++;
					break;
				}
			}
		}

		//wait for the rest of the time out
		FD_ZERO(&fd);
		FD_SET(sock, &fd);
		double remains = time_out - (Utils::duration(startSelect, endSelect) / 1000.0);
		timeout.tv_sec = floor(remains);
		timeout.tv_usec = (remains - floor(remains)) * 1e6;
	}
	return TIMEOUT;
}

//Producer role, produce packet into the shared buffer
int SenderSocket::send(char* data, int bytes, CircularArray* packetQueue, SendThreadParams* param)
{
	if (WaitForSingleObject(param->abortSendEvent, 10) != WAIT_TIMEOUT)
		return FAILED_SEND;

	//decrements number of empty slots
	WaitForSingleObject(param->emptySemaphore, INFINITE);
	WaitForSingleObject(param->mutex, INFINITE);

	//create data packet
	int size = sizeof(SenderDataHeader) + bytes;
	char* packet = makePacket(size, seq, data, bytes);
	packetQueue->push(Packet(packet, size));

	//first packet of the window
	if (seq == stats.senderBase){
		startTransmit = clock() / (double)CLOCKS_PER_SEC;
	}

	seq++;
	timeoutPending = true;
	ReleaseMutex(param->mutex);

	return STATUS_OK;
}

int SenderSocket::close(clock_t startTime)
{
	//check if open() has been called
	if (!opened)
		return NOT_CONNECTED;

	return handleSYNFIN(false, startTime);
}
