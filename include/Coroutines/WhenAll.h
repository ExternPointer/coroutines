#pragma once

#include "Concepts/Awaitable.h"
#include "Private/VoidValue.h"

#include <atomic>
#include <coroutine>
#include <ranges>
#include <tuple>
#include <vector>

namespace Coroutines {
namespace Private {
    class WhenAllLatch {
    public:
        WhenAllLatch( std::size_t count ) noexcept
            : m_count( count + 1 ) {
        }

        WhenAllLatch( const WhenAllLatch& ) = delete;
        WhenAllLatch( WhenAllLatch&& other )
            : m_count( other.m_count.load( std::memory_order::acquire ) )
            , m_awaiting_coroutine( std::exchange( other.m_awaiting_coroutine, nullptr ) ) {
        }

        auto operator=( const WhenAllLatch& ) -> WhenAllLatch& = delete;
        auto operator=( WhenAllLatch&& other ) -> WhenAllLatch& {
            if( std::addressof( other ) != this ) {
                m_count.store( other.m_count.load( std::memory_order::acquire ), std::memory_order::relaxed );
                m_awaiting_coroutine = std::exchange( other.m_awaiting_coroutine, nullptr );
            }

            return *this;
        }

        auto is_ready() const noexcept -> bool {
            return m_awaiting_coroutine != nullptr && m_awaiting_coroutine.done();
        }

        auto try_await( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
            m_awaiting_coroutine = awaiting_coroutine;
            return m_count.fetch_sub( 1, std::memory_order::acq_rel ) > 1;
        }

        auto notify_awaitable_completed() noexcept -> void {
            if( m_count.fetch_sub( 1, std::memory_order::acq_rel ) == 1 ) {
                m_awaiting_coroutine.resume();
            }
        }

    private:
        std::atomic<std::size_t> m_count;
        std::coroutine_handle<> m_awaiting_coroutine { nullptr };
    };

    template<typename task_container_type>
    class WhenAllReadyAwaitable;

    template<typename return_type>
    class WhenAllTask;

    template<>
    class WhenAllReadyAwaitable<std::tuple<>> {
    public:
        constexpr WhenAllReadyAwaitable() noexcept {
        }
        explicit constexpr WhenAllReadyAwaitable( std::tuple<> ) noexcept {
        }

        constexpr auto await_ready() const noexcept -> bool {
            return true;
        }
        auto await_suspend( std::coroutine_handle<> ) noexcept -> void {
        }
        auto await_resume() const noexcept -> std::tuple<> {
            return {};
        }
    };

    template<typename... task_types>
    class WhenAllReadyAwaitable<std::tuple<task_types...>> {
    public:
        explicit WhenAllReadyAwaitable( task_types&&... tasks ) noexcept( std::conjunction_v<std::is_nothrow_move_constructible<task_types>...> )
            : m_latch( sizeof...( task_types ) )
            , m_tasks( std::move( tasks )... ) {
        }

        explicit WhenAllReadyAwaitable( std::tuple<task_types...>&& tasks ) noexcept( std::is_nothrow_move_constructible_v<std::tuple<task_types...>> )
            : m_latch( sizeof...( task_types ) )
            , m_tasks( std::move( tasks ) ) {
        }

        WhenAllReadyAwaitable( const WhenAllReadyAwaitable& ) = delete;
        WhenAllReadyAwaitable( WhenAllReadyAwaitable&& other )
            : m_latch( std::move( other.m_latch ) )
            , m_tasks( std::move( other.m_tasks ) ) {
        }

        auto operator=( const WhenAllReadyAwaitable& ) -> WhenAllReadyAwaitable& = delete;
        auto operator=( WhenAllReadyAwaitable&& ) -> WhenAllReadyAwaitable& = delete;

        auto operator co_await() & noexcept {
            struct awaiter {
                explicit awaiter( WhenAllReadyAwaitable& awaitable ) noexcept
                    : m_awaitable( awaitable ) {
                }

                auto await_ready() const noexcept -> bool {
                    return m_awaitable.is_ready();
                }

                auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
                    return m_awaitable.try_await( awaiting_coroutine );
                }

                auto await_resume() noexcept -> std::tuple<task_types...>& {
                    return m_awaitable.m_tasks;
                }

            private:
                WhenAllReadyAwaitable& m_awaitable;
            };

            return awaiter { *this };
        }

        auto operator co_await() && noexcept {
            struct awaiter {
                explicit awaiter( WhenAllReadyAwaitable& awaitable ) noexcept
                    : m_awaitable( awaitable ) {
                }

                auto await_ready() const noexcept -> bool {
                    return m_awaitable.is_ready();
                }

                auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
                    return m_awaitable.try_await( awaiting_coroutine );
                }

                auto await_resume() noexcept -> std::tuple<task_types...>&& {
                    return std::move( m_awaitable.m_tasks );
                }

            private:
                WhenAllReadyAwaitable& m_awaitable;
            };

            return awaiter { *this };
        }

    private:
        auto is_ready() const noexcept -> bool {
            return m_latch.is_ready();
        }

        auto try_await( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
            std::apply( [ this ]( auto&&... tasks ) { ( ( tasks.Start( m_latch ) ), ... ); }, m_tasks );
            return m_latch.try_await( awaiting_coroutine );
        }

        WhenAllLatch m_latch;
        std::tuple<task_types...> m_tasks;
    };

    template<typename task_container_type>
    class WhenAllReadyAwaitable {
    public:
        explicit WhenAllReadyAwaitable( task_container_type&& tasks ) noexcept
            : m_latch( std::size( tasks ) )
            , m_tasks( std::forward<task_container_type>( tasks ) ) {
        }

        WhenAllReadyAwaitable( const WhenAllReadyAwaitable& ) = delete;
        WhenAllReadyAwaitable( WhenAllReadyAwaitable&& other ) noexcept( std::is_nothrow_move_constructible_v<task_container_type> )
            : WhenAllLatch( std::move( other.m_latch ) )
            , m_tasks( std::move( m_tasks ) ) {
        }

        auto operator=( const WhenAllReadyAwaitable& ) -> WhenAllReadyAwaitable& = delete;
        auto operator=( WhenAllReadyAwaitable& ) -> WhenAllReadyAwaitable& = delete;

        auto operator co_await() & noexcept {
            struct awaiter {
                awaiter( WhenAllReadyAwaitable& awaitable )
                    : m_awaitable( awaitable ) {
                }

                auto await_ready() const noexcept -> bool {
                    return m_awaitable.is_ready();
                }

                auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
                    return m_awaitable.try_await( awaiting_coroutine );
                }

                auto await_resume() noexcept -> task_container_type& {
                    return m_awaitable.m_tasks;
                }

            private:
                WhenAllReadyAwaitable& m_awaitable;
            };

            return awaiter { *this };
        }

        auto operator co_await() && noexcept {
            struct awaiter {
                awaiter( WhenAllReadyAwaitable& awaitable )
                    : m_awaitable( awaitable ) {
                }

                auto await_ready() const noexcept -> bool {
                    return m_awaitable.is_ready();
                }

                auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
                    return m_awaitable.try_await( awaiting_coroutine );
                }

                auto await_resume() noexcept -> task_container_type&& {
                    return std::move( m_awaitable.m_tasks );
                }

            private:
                WhenAllReadyAwaitable& m_awaitable;
            };

            return awaiter { *this };
        }

    private:
        auto is_ready() const noexcept -> bool {
            return m_latch.is_ready();
        }

        auto try_await( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
            for( auto& task: m_tasks ) {
                task.Start( m_latch );
            }

            return m_latch.try_await( awaiting_coroutine );
        }

        WhenAllLatch m_latch;
        task_container_type m_tasks;
    };

    template<typename return_type>
    class WhenAllTaskPromise {
    public:
        using coroutine_handle_type = std::coroutine_handle<WhenAllTaskPromise<return_type>>;

        WhenAllTaskPromise() noexcept {
        }

        auto get_return_object() noexcept {
            return coroutine_handle_type::from_promise( *this );
        }

        auto initial_suspend() noexcept -> std::suspend_always {
            return {};
        }

        auto final_suspend() noexcept {
            struct completion_notifier {
                auto await_ready() const noexcept -> bool {
                    return false;
                }
                auto await_suspend( coroutine_handle_type coroutine ) const noexcept -> void {
                    coroutine.promise().m_latch->notify_awaitable_completed();
                }
                auto await_resume() const noexcept {
                }
            };

            return completion_notifier {};
        }

        auto unhandled_exception() noexcept {
            m_exception_ptr = std::current_exception();
        }

        auto yield_value( return_type&& value ) noexcept {
            m_return_value = std::addressof( value );
            return final_suspend();
        }

        auto start( WhenAllLatch& latch ) noexcept -> void {
            m_latch = &latch;
            coroutine_handle_type::from_promise( *this ).resume();
        }

        auto return_value() & -> return_type& {
            if( m_exception_ptr ) {
                std::rethrow_exception( m_exception_ptr );
            }
            return *m_return_value;
        }

        auto return_value() && -> return_type&& {
            if( m_exception_ptr ) {
                std::rethrow_exception( m_exception_ptr );
            }
            return std::forward( *m_return_value );
        }

    private:
        WhenAllLatch* m_latch { nullptr };
        std::exception_ptr m_exception_ptr;
        std::add_pointer_t<return_type> m_return_value;
    };

    template<>
    class WhenAllTaskPromise<void> {
    public:
        using coroutine_handle_type = std::coroutine_handle<WhenAllTaskPromise<void>>;

        WhenAllTaskPromise() noexcept {
        }

        auto get_return_object() noexcept {
            return coroutine_handle_type::from_promise( *this );
        }

        auto initial_suspend() noexcept -> std::suspend_always {
            return {};
        }

        auto final_suspend() noexcept {
            struct completion_notifier {
                auto await_ready() const noexcept -> bool {
                    return false;
                }
                auto await_suspend( coroutine_handle_type coroutine ) const noexcept -> void {
                    coroutine.promise().m_latch->notify_awaitable_completed();
                }
                auto await_resume() const noexcept -> void {
                }
            };

            return completion_notifier {};
        }

        auto unhandled_exception() noexcept -> void {
            m_exception_ptr = std::current_exception();
        }

        auto return_void() noexcept -> void {
        }

        auto result() -> void {
            if( m_exception_ptr ) {
                std::rethrow_exception( m_exception_ptr );
            }
        }

        auto start( WhenAllLatch& latch ) -> void {
            m_latch = &latch;
            coroutine_handle_type::from_promise( *this ).resume();
        }

    private:
        WhenAllLatch* m_latch { nullptr };
        std::exception_ptr m_exception_ptr;
    };

    template<typename return_type>
    class WhenAllTask {
    public:
        template<typename task_container_type>
        friend class WhenAllReadyAwaitable;

        using promise_type = WhenAllTaskPromise<return_type>;
        using coroutine_handle_type = typename promise_type::coroutine_handle_type;

        WhenAllTask( coroutine_handle_type coroutine ) noexcept
            : m_coroutine( coroutine ) {
        }

        WhenAllTask( const WhenAllTask& ) = delete;
        WhenAllTask( WhenAllTask&& other ) noexcept
            : m_coroutine( std::exchange( other.m_coroutine, coroutine_handle_type {} ) ) {
        }

        auto operator=( const WhenAllTask& ) -> WhenAllTask& = delete;
        auto operator=( WhenAllTask&& ) -> WhenAllTask& = delete;

        ~WhenAllTask() {
            if( m_coroutine != nullptr ) {
                m_coroutine.destroy();
            }
        }

        auto return_value() & -> decltype( auto ) {
            if constexpr( std::is_void_v<return_type> ) {
                m_coroutine.promise().result();
                return void_value {};
            } else {
                return m_coroutine.promise().return_value();
            }
        }

        auto return_value() const& -> decltype( auto ) {
            if constexpr( std::is_void_v<return_type> ) {
                m_coroutine.promise().result();
                return void_value {};
            } else {
                return m_coroutine.promise().return_value();
            }
        }

        auto return_value() && -> decltype( auto ) {
            if constexpr( std::is_void_v<return_type> ) {
                m_coroutine.promise().result();
                return void_value {};
            } else {
                return m_coroutine.promise().return_value();
            }
        }

    private:
        auto start( WhenAllLatch& latch ) noexcept -> void {
            m_coroutine.promise().start( latch );
        }

        coroutine_handle_type m_coroutine;
    };

    template<Concepts::CAwaitable awaitable, typename return_type = Concepts::CAwaitableTraits<awaitable&&>::TAwaiterResult>
    static auto MakeWhenAllTask( awaitable a ) -> WhenAllTask<return_type> {
        if constexpr( std::is_void_v<return_type> ) {
            co_await static_cast<awaitable&&>( a );
            co_return;
        } else {
            co_yield co_await static_cast<awaitable&&>( a );
        }
    }

} // namespace detail

template<Concepts::CAwaitable... awaitables_type>
[[nodiscard]] auto WhenAll( awaitables_type... awaitables ) {
    return Private::WhenAllReadyAwaitable<std::tuple<Private::WhenAllTask<typename Concepts::CAwaitableTraits<awaitables_type>::TAwaiterResult>...>>(
        std::make_tuple( Private::MakeWhenAllTask( std::move( awaitables ) )... ) );
}

template<std::ranges::range range_type, Concepts::CAwaitable awaitable_type = std::ranges::range_value_t<range_type>,
         typename return_type = Concepts::CAwaitableTraits<awaitable_type>::TAwaiterResult>
[[nodiscard]] auto WhenAll( range_type awaitables ) -> Private::WhenAllReadyAwaitable<std::vector<Private::WhenAllTask<return_type>>> {
    std::vector<Private::WhenAllTask<return_type>> output_tasks;
    if constexpr( std::ranges::sized_range<range_type> ) {
        output_tasks.reserve( std::size( awaitables ) );
    }
    for( auto& a: awaitables ) {
        output_tasks.emplace_back( Private::MakeWhenAllTask( std::move( a ) ) );
    }
    return Private::WhenAllReadyAwaitable( std::move( output_tasks ) );
}

} // namespace Coroutines
