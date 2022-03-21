#include <iostream>
#include<windows.h> 
#include <random>
#include <chrono>
import threadpool;

using namespace std::chrono;

double MonteCarloPi(int N)
{
	std::random_device rd;
	std::default_random_engine gen(rd());
	std::uniform_real_distribution<double> dist(0, 1);
	int inside = 0;
	for (int i = 0; i < N; ++i)
	{
		double x = dist(gen);
		double y = dist(gen);
		inside += x * x + y * y < 1;
	}
	double guess= 4*(double)inside / N;
	
	return guess;
}

double MonteCarloE(int N)
{
	std::random_device rd;
	std::default_random_engine gen(rd());
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
	return guess;
}


int main()
{
	utility::Threadpool pool(6);
	std::cout << "Testing thread pool" << std::endl;

	std::vector<std::pair<std::future<double>,int>> PI;
	std::vector<std::pair<std::future<double>,int>> E;
	auto start = high_resolution_clock::now();

	for (int N = 1e6; N < 1e9; N*=10)
	{
		E.push_back(std::make_pair(pool.submit(MonteCarloE, N),N));
		PI.push_back(std::make_pair(pool.submit(MonteCarloPi, N), N));
	}
	
	for (auto& fut : E)
	{
		std::cout << fut.first.get() << std::endl;
	}
	for (auto& fut : PI)
	{
		std::cout << fut.first.get() << std::endl;
	}

	auto stop = high_resolution_clock::now();
	auto duration = duration_cast<milliseconds>(stop - start);

	std::cout << "Time taken by function: "
		<< duration.count() << " milliseconds" << std::endl;
	
	return 0;

}