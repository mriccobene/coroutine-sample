// This is a toy program to explore the c++20 coroutine feature
// and it has educational only purposes
// Author: Michelangelo Riccobene - 7 gen 2021

#include <iostream>
#include <chrono>
#include <coroutine>
#include <thread>
#include <future>
#include <stack>
#include "thread_safe_queue.h"
#include <assert.h>

#include <type_traits>

using namespace std::chrono;

// ----------------------------------------------------------------------------------------------------------------
// (toy) coroutine scheduler

class co_scheduler {
public:
	static co_scheduler instance;

	using co_address = void*;
	using co_handle_base = std::coroutine_handle<void>;

	void add(co_handle_base const& h) {
		queue_.push(h.address());
	}

	void run_loop() {
		while (1) {
			co_address addr;
			queue_.wait_and_pop(addr);
			co_handle_base h = co_handle_base::from_address(addr);
     		h.resume();	// pushes the coroutine forward
			// if (h.done()) h.destroy(); ???
		}
	}

private:
	thread_safe_queue<co_address> queue_;
};

co_scheduler co_scheduler::instance;

// ----------------------------------------------------------------------------------------------------------------

class broken_promise : public std::logic_error
{
public:
	broken_promise(): std::logic_error("broken promise") {}
};

// ----------------------------------------------------------------------------------------------------------------
// (toy) coroutine 

template <typename T>
struct promise_type;

template <typename T>
struct co_task {
	using promise_type = promise_type<T>;
	using co_handle = std::coroutine_handle<promise_type>;	// we can use a trait to define this

#if !defined(CO_AWAIT_OVERLOADING)
	bool await_ready() noexcept 
	{ 
		return !co_handle_ || co_handle_.done(); 
	}

	void await_suspend(std::coroutine_handle<> caller_co_handle) noexcept
	{
		co_handle_.promise().caller_co_handle_ = caller_co_handle;  // save caller coroutine handle, it will be resumed at the end of this coroutine
		co_handle_.resume();
	}

	decltype(auto) await_resume() 
	{
		if (!this->co_handle_) throw broken_promise{};
		return this->co_handle_.promise().result();
	}
#else

	// First form:
	// class awaiter { /* ... */ };  
	// awaiter operator co_await() && noexcept { return awaitable{ co_handle_ }; }

	// Second form:
	auto operator co_await() const& noexcept
	{
		struct awaitable 
		{
			co_handle co_handle_;

			awaitable(co_handle h) noexcept : co_handle_(h) {}

			bool await_ready() const noexcept { return !co_handle_ || co_handle_.done(); }

			bool await_suspend(std::coroutine_handle<> awaitingCoroutine) noexcept
			{
				co_handle_.resume();
				return co_handle_.promise().try_set_continuation(awaitingCoroutine);
			}

			decltype(auto) await_resume()
			{
				if (!this->co_handle_)
				{
					throw broken_promise{};
				}

				return this->co_handle_.promise().result();
			}
		};

		return awaitable{ co_handle_ };
	}

	// improvement: define 
	// auto operator co_await() const& noexcept
	// auto operator co_await() const&& noexcept

#endif

	T exec_sync() { if (!co_handle_.done()) co_handle_.resume(); return this->co_handle_.promise().result(); }
	void exec_async() { if (!co_handle_.done()) co_scheduler::instance.add(co_handle_); }

	explicit co_task(co_handle h) noexcept: co_handle_(h) {}
	co_task(co_task&& t) noexcept:             co_handle_(std::exchange(t.co_handle_, {})) {}
	~co_task()                              { if (co_handle_) co_handle_.destroy();	}

	co_handle co_handle_;
};

// ----------------------------------------------------------------------------------------------------------------
// (toy) promise_type

struct promise_type_base {

	auto initial_suspend() { return std::suspend_always{}; }
	auto final_suspend() noexcept {
		struct Awaiter {
			promise_type_base* self;
			bool await_ready() noexcept { return false; }
			void await_suspend(std::coroutine_handle<>) noexcept { if (self->caller_co_handle_) self->caller_co_handle_.resume(); }	// resume caller
			void await_resume() noexcept {}
		};
		return Awaiter{ this };
	}

	void unhandled_exception() noexcept { exception_ = std::current_exception(); }

	promise_type_base() : exception_{}, caller_co_handle_{} {}

	std::exception_ptr exception_;
	std::coroutine_handle<> caller_co_handle_;
};

template <typename T>
struct promise_type : public promise_type_base {
	using co_handle = std::coroutine_handle<promise_type>;

	co_task<T> get_return_object() { return co_task{ co_handle::from_promise(*this) }; } // costruisce oggetto ritornato da coroutine

	void return_value(T&& value) noexcept(std::is_nothrow_constructible_v<T, T&&>) { value_ = std::forward<T>(value); }

	T result() { if (exception_) std::rethrow_exception(exception_); else return value_; }
	// T result()&& { if (exception_) std::rethrow_exception(exception_) else return std::move(value_); }

	promise_type() : promise_type_base{}, value_{} {}

	T value_; // requires: default ctor & copy ctor

	// improvement: use union of T and exception_ptr, construct value_ and exception_ in place with
	// ::new (static_cast<void*>(std::addressof(value_))) T(std::forward<VALUE>(value));
	// ::new (static_cast<void*>(std::addressof(exception_))) std::exception_ptr(std::current_exception());
};

template <>
struct promise_type<void> : public promise_type_base {
	using co_handle = std::coroutine_handle<promise_type>;

	co_task<void> get_return_object() { return co_task{ co_handle::from_promise(*this) }; } // build co_routing object

	void return_void() noexcept {}

	void result() { if (exception_) std::rethrow_exception(exception_); }

	promise_type() : promise_type_base{} {}
};

// We can clean the code moving implementations in other files
//template <typename T>
//co_task<T> promise_type<T>::get_return_object() { return co_task{ co_handle::from_promise(*this) }; } // build co_routing object
// co_task<void> promise_type<void>::get_return_object() { return co_task{ co_handle::from_promise(*this) }; } // build co_routing object

// ----------------------------------------------------------------------------------------------------------------
// co_await for chrono::duration
// da https://stackoverflow.com/questions/49640336/implementing-example-from-coroutines-ts-2017
// 

template<class Rep, class Period>
auto operator co_await(std::chrono::duration<Rep, Period> dur)
{
	struct awaiter
	{
		using clock = std::chrono::high_resolution_clock;
		clock::time_point resume_time;

		awaiter(clock::duration dur)
			: resume_time(clock::now() + dur) { }

		bool await_ready() { return resume_time <= clock::now(); }

		void await_suspend(std::coroutine_handle<> co_handle)
		{
			// this is expensive, todo: implement extending co_scheduler with a time aware queue			
			std::thread([=]() {
				std::this_thread::sleep_until(resume_time); // sleep
				std::cout << "timer expired\n";
				co_scheduler::instance.add(co_handle);  // set up continuation on the scheduler thread
				//co_handle.resume();  // resuming here we run the coroutine continuation on this thread
			}).detach(); // detach scares me

		}
		void await_resume() {}
	};

	return awaiter{ dur };
}

// ----------------------------------------------------------------------------------------------------------------
// example

co_task<int> h()
{
	std::cout << "h - started\n";
	co_await 1000ms;
	std::cout << "h - resumed\n";
	co_return 1;
}

co_task<void> g()
{
	std::cout << "g - started\n";

	int x = co_await h();

	std::cout << "g - resumed\n";
}

co_task<void> sample()
{
	std::cout << "sample start\n";

	int x = co_await h();

	std::cout << "sample end\n";
}

co_task<void> sample2()
{
	std::cout << "sample2 start\n";

	co_await g();

	std::cout << "sample2 end\n";
}

int main()
{
	std::cout << "Hello coroutine!\n";

	auto co1 = sample();
	auto co2 = sample2();
	
	co1.exec_async();
	co2.exec_async();

	co_scheduler::instance.run_loop();

	// -------------------------------------------------------------------
	// on cppcoro we must code like this:
	//    cppcoro::sync_wait(sample()); 
	// or:
	//    auto con = std::async([] { cppcoro::sync_wait(sample()); });  
	//    con.get();		
	
	std::cout << "Hello coroutine ended!\n";
}




