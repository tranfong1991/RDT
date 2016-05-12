#include "CircularArray.h"

CircularArray::CircularArray(int c)
{
	packets = std::vector<Packet>(c);
	capacity = c;
	frontIndex = 0;
	currentIndex = 0;
}

CircularArray::~CircularArray()
{
}

void CircularArray::push(const Packet& p)
{
	if (packets[currentIndex].data != NULL)
		delete[] packets[currentIndex].data;

	packets[currentIndex].data = new char[p.size];
	memcpy(packets[currentIndex].data, p.data, p.size);

	packets[currentIndex].size = p.size;
	packets[currentIndex].isSent = p.isSent;
	if (currentIndex + 1 == capacity)
		currentIndex = 0;
	else
		currentIndex++;
}

void CircularArray::pop()
{
	if (frontIndex + 1 == capacity)
		frontIndex = 0;
	else
		frontIndex++;
}
