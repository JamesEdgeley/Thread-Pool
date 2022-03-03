#include <iostream>
#include<windows.h> 

import threadpool;

int slowfunc(int x)
{
	Sleep(1000);
	return x;
}

int quickfunc(int x)
{
	return 10*x;
}


int main()
{
	Threadpool pool(2);
	std::cout << "Testing thread pool" << std::endl;

	auto slowresult1 = pool.submit(slowfunc, 1);
	auto slowresult2 = pool.submit(slowfunc, 2);
	auto slowresult3 = pool.submit(slowfunc, 3);
	auto slowresult4 = pool.submit(slowfunc, 4);
	auto quickresult = pool.submit(quickfunc,1);

	std::cout << quickresult.get() << std::endl;
	std::cout << slowresult1.get() << std::endl;
	std::cout << slowresult2.get() << std::endl;
	std::cout << slowresult3.get() << std::endl;
	std::cout << slowresult4.get() << std::endl;
	
	return 0;
}