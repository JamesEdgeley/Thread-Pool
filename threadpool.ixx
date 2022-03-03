module;
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <future>
#include <semaphore>
#include <functional>
#include <optional>
#include <iostream>
export module threadpool;

import wsdeque;

export class Threadpool
{
private:
	struct workdeque
	{
		std::counting_semaphore<1> sem{ 0 };
		WSDeque<std::function<void()>> work;
	};

	std::vector<workdeque> work_load; //Local work queues
	std::atomic<std::int64_t> jobs_left; //Number of jobs in the pool
	std::size_t count = 0;
	std::vector<std::jthread> threads;

	template <typename Task>
	void load(Task&& task) //Puts packaged task into work_load
	{
		std::size_t i = count++ % work_load.size(); //Gets index of work_deque least recently executed on

		jobs_left.fetch_add(1, std::memory_order::relaxed); //Adds 1 to the work_left counter
		work_load[i].work.emplace([task]() { (*task)(); }); //Adds the function to the queue
		work_load[i].sem.release(); //Increment semaphore so task can execute
	}

public:
	// Construct threadpool (default number of threads is max allowed by hardware).  Must be explicit e.g. Threadpool pool(4)
	explicit Threadpool(std::size_t threadN = std::thread::hardware_concurrency()) : work_load(threadN)
	{
		for (std::size_t tID = 0; tID < threadN; ++tID)
		{
			threads.emplace_back
			(
				[&, qID = tID](std::stop_token stop_token)
				{
					//id = rand() % work_deques.size();  // Get a different random stream
					do {
						work_load[qID].sem.try_acquire();
						std::size_t spin = 0;
						do {
							// Prioritise our work otherwise steal
							std::size_t newqID = spin++ < 100 || !work_load[qID].work.empty() ? qID : rand() % work_load.size(); //Choose random new queue ID iff old queue is empty

							if (std::optional<std::function<void()>> task = work_load[newqID].work.steal())
							{
								jobs_left.fetch_sub(1, std::memory_order::release);
								std::invoke(std::move(*task));
							}
						} while (jobs_left.load(std::memory_order::acquire));
					} while (!stop_token.stop_requested());
				}
			);
		}
	}

	// Submit a non-void function and returns the associated future
	template <typename Func, typename... Args, typename Ret = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>>
	std::future<Ret> submit(Func&& f, Args&&... args) requires(!std::is_void<Ret>::value)
	{
		auto task = std::make_shared<std::packaged_task<Ret()>>(std::bind(std::forward<Func>(f), std::forward<Args>(args)...));
		std::future<Ret> future = task->get_future();
		load(task);

		return future;
	}

	// Submits a void function
	template <typename Func, typename... Args, typename Ret = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>>
	void submit(Func&& f, Args&&... args) requires(std::is_void<Ret>::value)
	{
		auto task = std::make_shared<std::packaged_task<Ret()>>(std::bind(std::forward<Func>(f), std::forward<Args>(args)...));
		load(task);
	}

	~Threadpool()
	{
		//Stop all threads working upon destruction (they are jthreads so automatically rejoin)
		for (auto& thread : threads)
		{
			thread.request_stop();
		}
		//Unblock all work_deques
		for (auto& work_stream : work_load)
		{
			work_stream.sem.release();
		}
	}
};