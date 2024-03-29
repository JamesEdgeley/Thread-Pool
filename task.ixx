module;
#include <coroutine>
#include <iostream>
export module task;

namespace utility
{
    struct task_promise;

    export class [[nodiscard]] task
    {
    public:
        using promise_type = task_promise;

        explicit task(std::coroutine_handle<task_promise> handle)
            : m_handle(handle)
        {
        }

    private:
        std::coroutine_handle<task_promise> m_handle;
    };

    struct task_promise
    {
        task get_return_object() noexcept {
            return task{ std::coroutine_handle<task_promise>::from_promise(*this) };
        };

        std::suspend_never initial_suspend() const noexcept { return {}; }
        std::suspend_never final_suspend() const noexcept { return {}; }

        void return_void() noexcept {}

        void unhandled_exception() noexcept {
            std::cerr << "Unhandled exception caught...\n";
            exit(1);
        }
    };
}
