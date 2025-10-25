#include <iostream>
#include <list>

#include "MemoryPool_v1/MemoryPool.h"
// #include "MemoryPool_v2/MemoryPool.h"

static void test_v1()
{
	std::cout << "====== test MemoryPool v1 ======\n";
	std::list<int, MemoryPool<int, 123>> l;

	for (int i = 0; i < 10; i++)
		l.push_back(2);
	for (int i = 0; i < 5; i++)
		l.pop_back();
	for (int i = 0; i < 5; i++)
		l.push_back(3);
	
	auto copyList = l;
	for (int i = 0; i < 5; i++)
		copyList.pop_back();
	for (int i = 0; i < 5; i++)
		copyList.push_back(3);
	
}

int main()
{
	test_v1();

	return 0;
}