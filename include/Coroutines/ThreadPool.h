#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <variant>
#include <vector>

#include "Concepts/RangeOf.h"
#include "Event.h"
#include "Task.h"


namespace Coroutines {

class ThreadPool {
public:
    class Operation {
        friend class ThreadPool;
        explicit Operation( ThreadPool& tp ) noexcept;

    public:
        auto await_ready() noexcept -> bool {
            return false;
        }
        auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> void;
        auto await_resume() noexcept -> void {
        }

    private:
        ThreadPool& m_threadPool;
        std::coroutine_handle<> m_awaitingCoroutine { nullptr };
    };

    struct options {
        uint32_t thread_count = std::thread::hardware_concurrency();
        std::function<void( std::size_t )> on_thread_start_functor = nullptr;
        std::function<void( std::size_t )> on_thread_stop_functor = nullptr;
    };

    explicit ThreadPool( options opts = options {
                             .thread_count = std::thread::hardware_concurrency(), .on_thread_start_functor = nullptr, .on_thread_stop_functor = nullptr } );

    ThreadPool( const ThreadPool& ) = delete;
    ThreadPool( ThreadPool&& ) = delete;
    auto operator=( const ThreadPool& ) -> ThreadPool& = delete;
    auto operator=( ThreadPool&& ) -> ThreadPool& = delete;

    virtual ~ThreadPool();

    auto ThreadCount() const noexcept -> uint32_t {
        return ( uint32_t )m_threads.size();
    }
    [[nodiscard]] auto Schedule() -> Operation;
    template<typename functor, typename... arguments>
    [[nodiscard]] auto Schedule( functor&& f, arguments... args ) -> Task<decltype( f( std::forward<arguments>( args )... ) )> {
        co_await Schedule();

        if constexpr( std::is_same_v<void, decltype( f( std::forward<arguments>( args )... ) )> ) {
            f( std::forward<arguments>( args )... );
            co_return;
        } else {
            co_return f( std::forward<arguments>( args )... );
        }
    }

    auto resume( std::coroutine_handle<> handle ) noexcept -> void;

    template<Coroutines::Concepts::CRangeOf<std::coroutine_handle<>> range_type>
    auto resume( const range_type& handles ) noexcept -> void {
        m_size.fetch_add( std::size( handles ), std::memory_order::release );

        size_t null_handles { 0 };

        {
            std::scoped_lock lk { m_waitMutex };
            for( const auto& handle: handles ) {
                if( handle != nullptr ) [[likely]] {
                    m_queue.emplace_back( handle );
                } else {
                    ++null_handles;
                }
            }
        }

        if( null_handles > 0 ) {
            m_size.fetch_sub( null_handles, std::memory_order::release );
        }

        m_waitCv.notify_one();
    }

    [[nodiscard]] auto yield() -> Operation {
        return Schedule();
    }
    auto shutdown() noexcept -> void;
    auto size() const noexcept -> std::size_t {
        return m_size.load( std::memory_order::acquire );
    }
    auto empty() const noexcept -> bool {
        return size() == 0;
    }
    auto queue_size() const noexcept -> std::size_t {
        std::atomic_thread_fence( std::memory_order::acquire );
        return m_queue.size();
    }

    auto queue_empty() const noexcept -> bool {
        return queue_size() == 0;
    }

private:
    options m_opts;
    std::vector<std::jthread> m_threads;

    std::mutex m_waitMutex;
    std::condition_variable_any m_waitCv;
    std::deque<std::coroutine_handle<>> m_queue;
    auto Executor( std::stop_token stop_token, std::size_t idx ) -> void;
    auto ScheduleImpl( std::coroutine_handle<> handle ) noexcept -> void;
    std::atomic<std::size_t> m_size { 0 };
    std::atomic<bool> m_shutdownRequested { false };
};

}
