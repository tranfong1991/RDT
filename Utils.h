/*
	Name: Phong Tran
	Class: CSCE 463 - 500
*/

#pragma once
#include <ctime>

class Utils{
public:
	//returns duration in miliseconds
	static double duration(clock_t start, clock_t end)
	{
		return (end - start) * 1000 / (double)(CLOCKS_PER_SEC);
	}

	//copy bytes from source to destination
	static void copy(char* dest, char* source, int num)
	{
		for (int i = 0; i < num; i++)
			dest[i] = source[i];
	}
};