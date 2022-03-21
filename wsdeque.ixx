module;
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>
#include <deque>
export module wsdeque;

namespace container {
	template <typename T>
	concept Simple = std::default_initializable<T> && std::is_destructible_v<T> && std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_constructible_v<T>;

	template <Simple T>
	class RingBuffer
	{
	private:
		std::uint64_t _capacity;
		std::uint64_t _mask;
		std::unique_ptr<T[]> _buff = std::make_unique_for_overwrite<T[]>(_capacity);

	public:
		explicit RingBuffer(std::uint64_t cap) : _capacity{ cap }, _mask{ cap - 1 }
		{
			assert(cap && (!(cap& (cap - 1))));
		}

		std::uint64_t capacity() const noexcept { return _capacity; }

		void store(std::uint64_t i, T&& x) noexcept
		{
			_buff[i & _mask] = std::move(x);
		}

		T load(std::uint64_t i) const noexcept
		{
			return _buff[i & _mask];
		}

		RingBuffer<T>* resize(std::uint64_t b, std::uint64_t t) const
		{
			RingBuffer<T>* ptr = new RingBuffer{ 2 * _capacity };
			for (std::uint64_t i = t; i != b; ++i)
			{
				ptr->store(i, load(i));
			}
			return ptr;
		}
	};//end RingBuffer

	export template <Simple T>
	class WSDeque
	{
	private:
		alignas(std::hardware_destructive_interference_size) std::atomic<std::uint64_t> _top;
		alignas(std::hardware_destructive_interference_size) std::atomic<std::uint64_t> _bottom;
		alignas(std::hardware_destructive_interference_size) std::atomic<RingBuffer<T>*> _buffer;
		std::vector<std::unique_ptr<RingBuffer<T>>> _garbage;

	public:
		explicit WSDeque(std::uint64_t cap = 1024) : _top(0), _bottom(0), _buffer(new RingBuffer<T>{ cap })
		{
			_garbage.reserve(32);
		}

		~WSDeque() noexcept
		{
			delete _buffer.load();
		}

		WSDeque(WSDeque const& deque) = delete;
		WSDeque& operator=(WSDeque const& deque) = delete;

		std::size_t size() const noexcept
		{
			uint64_t b = _bottom.load(std::memory_order::relaxed);
			uint64_t t = _top.load(std::memory_order::relaxed);
			return static_cast<std::size_t>(b >= t ? b - t : 0);
		}

		uint64_t capacity() const noexcept
		{
			return _buffer.load(std::memory_order_relaxed)->capacity();
		}

		bool empty() const noexcept
		{
			return !size();
		}

		template <typename... Args>
		void emplace(Args&&... args)
		{
			std::uint64_t b = _bottom.load(std::memory_order::relaxed);
			std::uint64_t t = _top.load(std::memory_order::acquire);
			RingBuffer<T>* buf = _buffer.load(std::memory_order::relaxed);

			if (buf->capacity() < (b - t) + 1)
			{
				_garbage.emplace_back(std::exchange(buf, buf->resize(b, t)));
				_buffer.store(buf, std::memory_order::relaxed);
			}

			buf->store(b, std::forward<Args>(args)...);

			std::atomic_thread_fence(std::memory_order::release);
			_bottom.store(b + 1, std::memory_order::relaxed);
		}

		std::optional<T> pop() noexcept
		{
			std::uint64_t b = _bottom.load(std::memory_order::relaxed) - 1;
			RingBuffer<T>* buf = _buffer.load(std::memory_order::relaxed);


			std::atomic_thread_fence(std::memory_order::seq_cst);
			std::uint64_t t = _top.load(std::memory_order::relaxed);

			if (t <= b)
			{
				if (t == b)
				{
					if (!_top.compare_exchange_strong(t, t + 1, std::memory_order::seq_cst, std::memory_order::relaxed))
					{
						_bottom.store(b + 1, std::memory_order::relaxed);
						return std::nullopt;
					}
					_bottom.store(b + 1, std::memory_order::relaxed);
				}

				return buf->load(b);

			}
			else
			{
				_bottom.store(b + 1, std::memory_order::relaxed);
				return std::nullopt;
			}
		}

		std::optional<T> steal() noexcept
		{
			std::uint64_t t = _top.load(std::memory_order::acquire);
			std::atomic_thread_fence(std::memory_order::seq_cst);
			std::uint64_t b = _bottom.load(std::memory_order::acquire);

			if (t < b)
			{
				T x = _buffer.load(std::memory_order::consume)->load(t);

				if (!_top.compare_exchange_strong(t, t + 1, std::memory_order::seq_cst, std::memory_order::relaxed))
				{
					return std::nullopt;
				}

				return x;

			}
			else
			{
				return std::nullopt;
			}
		}
	};//end WSdeque

}//end container
