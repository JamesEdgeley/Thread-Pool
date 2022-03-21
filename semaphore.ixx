module;
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cassert>


#if defined(_WIN32)
//---------------------------------------------------------
// Semaphore (Windows)
//---------------------------------------------------------

#include <windows.h>
#undef min
#undef max

class Semaphore
{
private:
    HANDLE m_hSema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

public:
    Semaphore(int initialCount = 0)
    {
        assert(initialCount >= 0);
        m_hSema = CreateSemaphore(NULL, initialCount, MAXLONG, NULL);
    }

    ~Semaphore()
    {
        CloseHandle(m_hSema);
    }

    void wait()
    {
        WaitForSingleObject(m_hSema, INFINITE);
    }

    void signal(int count = 1)
    {
        ReleaseSemaphore(m_hSema, count, NULL);
    }
};


#elif defined(__MACH__)
//---------------------------------------------------------
// Semaphore (Apple iOS and OSX)
// Can't use POSIX semaphores due to http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
//---------------------------------------------------------

#include <mach/mach.h>

class Semaphore
{
private:
    semaphore_t m_sema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

public:
    Semaphore(int initialCount = 0)
    {
        assert(initialCount >= 0);
        semaphore_create(mach_task_self(), &m_sema, SYNC_POLICY_FIFO, initialCount);
    }

    ~Semaphore()
    {
        semaphore_destroy(mach_task_self(), m_sema);
    }

    void wait()
    {
        semaphore_wait(m_sema);
    }

    void signal()
    {
        semaphore_signal(m_sema);
    }

    void signal(int count)
    {
        while (count-- > 0)
        {
            semaphore_signal(m_sema);
        }
    }
};


#elif defined(__unix__)
//---------------------------------------------------------
// Semaphore (POSIX, Linux)
//---------------------------------------------------------

#include <semaphore.h>

class Semaphore
{
private:
    sem_t m_sema;

    Semaphore(const Semaphore& other) = delete;
    Semaphore& operator=(const Semaphore& other) = delete;

public:
    Semaphore(int initialCount = 0)
    {
        assert(initialCount >= 0);
        sem_init(&m_sema, 0, initialCount);
    }

    ~Semaphore()
    {
        sem_destroy(&m_sema);
    }

    void wait()
    {
        int rc;
        do
        {
            rc = sem_wait(&m_sema);
        } while (rc == -1 && errno == EINTR);
    }

    void signal()
    {
        sem_post(&m_sema);
    }

    void signal(int count)
    {
        while (count-- > 0)
        {
            sem_post(&m_sema);
        }
    }
};


#else

class Semaphore
{
private:
    ptrdiff_t m_count;
    std::mutex m_mutex;
    std::condition_variable m_cv;

public:
    semaphore(const ptrdiff_t count = 0) noexcept : m_count(count)
    {
        assert(count >= 0);
    }

    void signal() noexcept
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            ++m_count;
        }
        m_cv.notify_one();
    }

    void wait() noexcept
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&]() { return m_count != 0; });
        --m_count;
    }


};

#endif

export module semaphore;


namespace utility
{
    export class fast_semaphore
    {
    private:
        std::atomic<ptrdiff_t> m_count;
        Semaphore m_semaphore;

    public:
        fast_semaphore(const ptrdiff_t count = 0) noexcept: m_count(count), m_semaphore() {}


        void acquire()
        {
            ptrdiff_t oldCount = m_count.load(std::memory_order::relaxed);
            if (oldCount > 0 && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order::acquire))
            {
                return;
            }

            int spin = 10000;
            while (spin--)
            {
                oldCount = m_count.load(std::memory_order::relaxed);
                if (oldCount > 0 && m_count.compare_exchange_strong(oldCount, oldCount - 1, std::memory_order::acquire))
                {
                    return;
                }
                std::atomic_signal_fence(std::memory_order::acquire);
            }

            if (m_count.fetch_sub(1, std::memory_order::acquire) <= 0)
            {
                m_semaphore.wait();
            }
        }

        void release()
        {
            if (m_count.fetch_add(1, std::memory_order::release) < 0)
            {
                m_semaphore.signal();
            }
        }
    };
}