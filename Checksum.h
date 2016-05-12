/*
	Name: Phong Tran
	Class: CSCE 463 - 500
*/

#pragma once
#include <WinSock2.h>

class Checksum
{
	DWORD crcTable[256];
public:
	Checksum();
	~Checksum();

	DWORD crc32(unsigned char* buf, size_t len);
};

