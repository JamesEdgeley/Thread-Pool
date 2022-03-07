#include <iostream>
#include<windows.h> 
#include <random>

import threadpool;

double MonteCarloPi(int N)
{
	std::default_random_engine gen;
	std::uniform_real_distribution<double> dist(0, 1);
	int inside = 0;
	for (int i = 0; i < N; ++i)
	{
		double x = dist(gen);
		double y = dist(gen);
		inside += x * x + y * y < 1;
	}
	double guess= 4*(double)inside / N;
	std::cout << guess << std::endl;
	return guess;
}

double MonteCarloE(int N)
{
	std::default_random_engine gen;
	std::uniform_real_distribution<double> dist(0, 1);
	int tries = 0;
	for (int i = 0; i < N; ++i)
	{
		double sum = 0;
		int count = 0;
		while (sum < 1)
		{
			sum += dist(gen);
			count++;
		}
		tries += count;
	}
	double guess= (double)tries / N;
	std::cout << guess << std::endl;
	return guess;
}


int main()
{
	utility::Threadpool pool(2);
	std::cout << "Testing thread pool" << std::endl;

	auto Pi1 = pool.submit(MonteCarloPi, 1000000);
	auto E1 = pool.submit(MonteCarloE, 1000000);


	std::cout << Pi1.get() << std::endl;
	std::cout << E1.get() << std::endl;

	
	return 0;
}