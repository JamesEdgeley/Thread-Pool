export module threadpool;

import <memory>;
import <atomic>;
import <thread>;
import <future>;
import <semaphore>;
import <functional>;
import <optional>;
import <coroutine>;
import <iostream>;
import <random>;

import wsdeque;
import semaphore;
import task;

namespace utility
{
	export class Threadpool
	{
	private:
		struct workdeque
		{
			utility::fast_semaphore permit;
			container::WSDeque<std::function<void()>> work;
		};

		std::vector<workdeque> work_load; //Local work queues
		std::atomic<std::uint64_t> jobs_left; //Number of jobs in the pool
		std::uint64_t count = 0;
		std::vector<std::jthread> threads;

		template <typename Task>
		void schedule(Task&& task)									//Puts task into work_load
		{
			std::size_t qID = count++ % work_load.size();			//Gets index of work_deque least recently executed on

			jobs_left.fetch_add(1, std::memory_order::relaxed);		//Adds 1 to the work_left counter
			work_load[qID].work.emplace(std::forward<Task>(task));	//Adds the function to the queue
			work_load[qID].permit.release();						//Increment semaphore so task can execute
		}

	public:
		// Construct threadpool (default number of threads is max allowed by hardware).
		Threadpool(const std::size_t& threadN = std::thread::hardware_concurrency()) : work_load(threadN)
		{
			for (std::size_t tID = 0; tID < threadN; ++tID)
			{
				thread_local std::uniform_int_distribution<size_t> dist(0, threadN);
				thread_local std::minstd_rand gen(std::random_device{}());

				threads.emplace_back
				(
					[&, qID = tID](std::stop_token stop_token)
					{

						do {
							work_load[qID].permit.acquire();															//Wait until work permit
							do {
								const std::size_t newqID = work_load[qID].work.empty() ? dist(gen) : qID;				//Choose random new queue ID iff old queue is empty
								if (std::optional<std::function<void()>> task = work_load[newqID].work.steal())			//If the queue can be stolen from... 
								{
									std::invoke(std::move(*task));														//...then execute that function
									jobs_left.fetch_sub(1, std::memory_order::release);									//Decrement the work counter

								}
							} while (jobs_left.load(std::memory_order::acquire));										//Continue to attempt stealing while there are jobs left
						} while (!stop_token.stop_requested());
					}
				);
			}
		}

		~Threadpool() noexcept
		{
			//Stop all threads working upon destruction (they are jthreads so automatically rejoin)
			for (auto& thread : threads)
			{
				thread.request_stop();
			}
			//Unblock all work_deques
			for (auto& work_stream : work_load)
			{
				work_stream.permit.release();
			}
		}

		// Submit a non-void function and returns the associated future
		template <typename Func, typename... Args, typename Ret = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>>
			requires(std::invocable<Func, Args...>)
		[[nodiscard]] std::future<Ret> submit(Func&& f, Args&&... args)
		{
			auto shared_promise = std::make_shared<std::promise<Ret>>();

			auto task = [f = std::forward<Func>(f), ... args = std::forward<Args>(args), promise = shared_promise]()
			{
				if constexpr (std::is_void_v<Ret>)
				{
					f(args...);
					promise->set_value();
				}
				else
				{
					promise->set_value(f(args...));
				}
			};
			std::future<Ret> future = shared_promise->get_future();
			schedule(std::move(task));

			return future;
		}


		[[nodiscard]] size_t jobs_remaining()
		{
			return jobs_left;
		}

		[[nodiscard]] size_t threads_running()
		{
			return threads.size();
		}
	};
	//end ThreadPool

}
//end utility