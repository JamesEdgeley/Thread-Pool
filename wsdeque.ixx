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
export module wsdeque;


//template <typename T>
//concept Simple = std::default_initializable<T> && std::is_trivially_destructible_v<T> && std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_constructible_v<T>;

// Basic wrapper around a c-style array of atomic objects that provides modulo load/stores. Capacity
// must be a power of 2.
template <typename T>
class RingBuff
{
private:
	std::int64_t _capacity;   // Capacity of the buffer
	std::int64_t _mask;  // Bit mask to perform modulo capacity operations
	std::unique_ptr<T[]> _buff = std::make_unique_for_overwrite<T[]>(_capacity);

public:
	explicit RingBuff(std::int64_t cap) : _capacity{ cap }, _mask{ cap - 1 }
	{
		assert(cap && (!(cap& (cap - 1))) && "Capacity must be buf power of 2!");
	}

	std::int64_t capacity() const noexcept { return _capacity; }

	// Store (copy) at modulo index
	void store(std::int64_t i, T&& x) noexcept
	{
		_buff[i & _mask] = std::move(x);
	}

	// Load (copy) at modulo index
	T load(std::int64_t i) const noexcept
	{
		return _buff[i & _mask];
	}

	// Allocates and returns a new ring buffer, copies elements in range [b, t) into the new buffer.
	RingBuff<T>* resize(std::int64_t b, std::int64_t t) const
	{
		RingBuff<T>* ptr = new RingBuff{ 2 * _capacity };
		for (std::int64_t i = t; i != b; ++i)
		{
			ptr->store(i, load(i));
		}
		return ptr;
	}
};

// Lock-free single-producer multiple-consumer deque. Only the deque owner can perform pop and push
// operations where the deque behaves like a stack. Others can (only) steal data from the deque, they see
// a FIFO queue. All threads must have finished using the deque before it is destructed. T must be
// default initializable, trivially destructible and have nothrow move constructor/assignment operators.
export template <typename T>
class WSDeque
{
private:
	alignas(std::hardware_destructive_interference_size) std::atomic<std::int64_t> _top;
	alignas(std::hardware_destructive_interference_size) std::atomic<std::int64_t> _bottom;
	alignas(std::hardware_destructive_interference_size) std::atomic<RingBuff<T>*> _buffer;
	std::vector<std::unique_ptr<RingBuff<T>>> _garbage;  // Store old buffers here.

public:
	// Constructs the deque with a given capacity the capacity of the deque (must be power of 2)
	explicit WSDeque(std::int64_t cap = 1024) : _top(0), _bottom(0), _buffer(new RingBuff<T>{ cap })
	{
		_garbage.reserve(32);
	}

	~WSDeque() noexcept
	{
		delete _buffer.load();
	}

	WSDeque(WSDeque const& other) = delete;
	WSDeque& operator=(WSDeque const& other) = delete;

	//  Query the size at instance of call
	std::size_t size() const noexcept
	{
		int64_t b = _bottom.load(std::memory_order::relaxed);
		int64_t t = _top.load(std::memory_order::relaxed);
		return static_cast<std::size_t>(b >= t ? b - t : 0);
	}

	// Query the capacity at instance of call
	int64_t capacity() const noexcept
	{
		return _buffer.load(std::memory_order_relaxed)->capacity();
	}

	// Test if empty at instance of call
	bool empty() const noexcept
	{
		return !size();
	}

	// Emplace an item to the deque. Only the owner thread can insert an item to the deque. The
	// operation can trigger the deque to resize its cap if more space is required. Provides the
	// strong exception guarantee.
	template <typename... Args>
	void emplace(Args&&... args)
	{
		// Construct before acquiring slot in-case constructor throws
		T object(std::forward<Args>(args)...);

		std::int64_t b = _bottom.load(std::memory_order::relaxed);
		std::int64_t t = _top.load(std::memory_order::acquire);
		RingBuff<T>* buf = _buffer.load(std::memory_order::relaxed);

		if (buf->capacity() < (b - t) + 1) {
			// Queue is full, build a new one
			_garbage.emplace_back(std::exchange(buf, buf->resize(b, t)));
			_buffer.store(buf, std::memory_order::relaxed);
		}

		// Construct new object, this does not have to be atomic as no one can steal this item until after we
		// store the new value of bottom, ordering is maintained by surrounding atomics.
		buf->store(b, std::move(object));

		std::atomic_thread_fence(std::memory_order::release);
		_bottom.store(b + 1, std::memory_order::relaxed);
	}

	// Pops out an item from the deque. Only the owner thread can pop out an item from the deque.
	// The return can be a std::nullopt if this operation fails (empty deque).
	std::optional<T> pop() noexcept
	{
		std::int64_t b = _bottom.load(std::memory_order::relaxed) - 1;
		RingBuff<T>* buf = _buffer.load(std::memory_order::relaxed);

		_bottom.store(b, std::memory_order::relaxed);  // Stealers can no longer steal

		std::atomic_thread_fence(std::memory_order::seq_cst);
		std::int64_t t = _top.load(std::memory_order::relaxed);

		if (t <= b)
		{
			// Non-empty deque
			if (t == b)
			{
				// The last item could get stolen, by a stealer that loaded bottom before our write above
				if (!_top.compare_exchange_strong(t, t + 1, std::memory_order::seq_cst, std::memory_order::relaxed))
				{
					// Failed race, thief got the last item.
					_bottom.store(b + 1, std::memory_order::relaxed);
					return std::nullopt;
				}
				_bottom.store(b + 1, std::memory_order::relaxed);
			}

			// Can delay load until after acquiring slot as only this thread can push(), this load is not
			// required to be atomic as we are the exclusive writer.
			return buf->load(b);

		}
		else
		{
			_bottom.store(b + 1, std::memory_order::relaxed);
			return std::nullopt;
		}
	}

	// Steals an item from the deque Any threads can try to steal an item from the deque. The return
	// can be a std::nullopt if this operation failed (not necessarily empty).
	std::optional<T> steal() noexcept
	{
		std::int64_t t = _top.load(std::memory_order::acquire);
		std::atomic_thread_fence(std::memory_order::seq_cst);
		std::int64_t b = _bottom.load(std::memory_order::acquire);

		if (t < b)
		{
			// Must load *before* acquiring the slot as slot may be overwritten immediately after acquiring.
			// This load is NOT required to be atomic even-though it may race with an overrite as we only
			// return the value if we win the race below garanteeing we had no race during our read. If we
			// loose the race then 'x' could be corrupt due to read-during-write race but as T is trivially
			// destructible this does not matter.
			T x = _buffer.load(std::memory_order::consume)->load(t);

			if (!_top.compare_exchange_strong(t, t + 1, std::memory_order::seq_cst, std::memory_order::relaxed))
			{
				// Failed race.
				return std::nullopt;
			}

			return x;

		}
		else
		{
			// Empty deque.
			return std::nullopt;
		}
	}
};

