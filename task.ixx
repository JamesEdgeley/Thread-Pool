module;
#include <coroutine>
export module task;

namespace utility
{
	export class Task
	{
	private:

	public:

		Task(std::coroutine_handle<>){}
	};
}
