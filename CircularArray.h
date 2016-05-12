/*
Name: Phong Tran
Class: CSCE 463 - 500
*/

#pragma once
#include <vector>
#include "Utils.h"

struct Packet{
	char* data;
	int size;
	bool isSent;

	Packet(){ data = NULL; size = 0; isSent = false; }
	Packet(char* d, int s){ data = d; size = s; isSent = false; }
	~Packet(){ if (data != NULL) delete[] data; }
};

class CircularArray
{
	std::vector<Packet> packets;
	int capacity;
	int frontIndex;
	int currentIndex;
public:
	CircularArray(int c);
	~CircularArray();

	int getFrontIndex(){ return frontIndex; }
	Packet& getFrontElem() { return packets[frontIndex]; }
	Packet& getElemAt(int index) { return packets[index]; }

	void push(const Packet& p);
	void pop();
};

