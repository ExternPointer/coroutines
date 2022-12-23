#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>

#include "Concepts/Awaitable.h"
#include "Concepts/Executor.h"
#include "Task.h"
#include "WhenAll.h"


namespace Coroutines {
namespace Private {
    class SyncWaitEvent {
    public:
        SyncWaitEvent( bool initially_set = false );
        SyncWaitEvent( const SyncWaitEvent& ) = delete;
        SyncWaitEvent( SyncWaitEvent&& ) = delete;
        auto operator=( const SyncWaitEvent& ) -> SyncWaitEvent& = delete;
        auto operator=( SyncWaitEvent&& ) -> SyncWaitEvent& = delete;
        ~SyncWaitEvent() = default;

        auto Set() noexcept -> void;
        auto Reset() noexcept -> void;
        auto Wait() noexcept -> void;

    private:
        std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_set { false };
    };

    class SyncWaitTaskPromiseBase {
    public:
        SyncWaitTaskPromiseBase() noexcept = default;
        virtual ~SyncWaitTaskPromiseBase() = default;

        auto initial_suspend() noexcept -> std::suspend_always {
            return {};
        }
        auto unhandled_exception() -> void {
            this->m_exception = std::current_exception();
        }

    protected:
        SyncWaitEvent* m_event { nullptr };
        std::exception_ptr m_exception;
    };

    template<typename TResult>
    class SyncWaitTaskPromise : public SyncWaitTaskPromiseBase {
    public:
        using TCoroutine = std::coroutine_handle<SyncWaitTaskPromise<TResult>>;

        SyncWaitTaskPromise() noexcept = default;
        ~SyncWaitTaskPromise() override = default;

        auto Start( SyncWaitEvent& event ) {
            this->m_event = &event;
            TCoroutine::from_promise( *this ).resume();
        }

        auto get_return_object() noexcept {
            return TCoroutine::from_promise( *this );
        }

        auto yield_value( TResult&& value ) noexcept {
            this->m_return_value = std::addressof( value );
            return final_suspend();
        }

        auto final_suspend() noexcept {
            struct completion_notifier {
                auto await_ready() const noexcept {
                    return false;
                }
                auto await_suspend( TCoroutine coroutine ) const noexcept {
                    coroutine.promise().m_event->Set();
                }
                auto await_resume() noexcept {};
            };

            return completion_notifier {};
        }

        auto result() -> TResult&& {
            if( this->m_exception ) {
                std::rethrow_exception( this->m_exception );
            }

            return static_cast<TResult&&>( *m_return_value );
        }

    private:
        std::remove_reference_t<TResult>* m_return_value;
    };

    template<>
    class SyncWaitTaskPromise<void> : public SyncWaitTaskPromiseBase {
        using TCoroutine = std::coroutine_handle<SyncWaitTaskPromise<void>>;

    public:
        SyncWaitTaskPromise() noexcept = default;
        ~SyncWaitTaskPromise() override = default;

        auto Start( SyncWaitEvent& event ) {
            this->m_event = &event;
            TCoroutine::from_promise( *this ).resume();
        }

        auto get_return_object() noexcept {
            return TCoroutine::from_promise( *this );
        }

        auto final_suspend() noexcept {
            struct completion_notifier {
                auto await_ready() const noexcept {
                    return false;
                }
                auto await_suspend( TCoroutine coroutine ) const noexcept {
                    coroutine.promise().m_event->Set();
                }
                auto await_resume() noexcept {};
            };

            return completion_notifier {};
        }

        auto return_void() noexcept -> void {
        }

        auto result() -> void {
            if( this->m_exception ) {
                std::rethrow_exception( this->m_exception );
            }
        }
    };

    template<typename TResult>
    class SyncWaitTask {
    public:
        using promise_type = SyncWaitTaskPromise<TResult>;
        using TCoroutine = std::coroutine_handle<promise_type>;

        SyncWaitTask( TCoroutine coroutine ) noexcept
            : m_coroutine( coroutine ) {
        }

        SyncWaitTask( const SyncWaitTask& ) = delete;
        SyncWaitTask( SyncWaitTask&& other ) noexcept
            : m_coroutine( std::exchange( other.m_coroutine, TCoroutine {} ) ) {
        }
        auto operator=( const SyncWaitTask& ) -> SyncWaitTask& = delete;
        auto operator=( SyncWaitTask&& other ) -> SyncWaitTask& {
            if( std::addressof( other ) != this ) {
                this->m_coroutine = std::exchange( other.m_coroutine, TCoroutine {} );
            }

            return *this;
        }

        ~SyncWaitTask() {
            if( this->m_coroutine ) {
                this->m_coroutine.destroy();
            }
        }

        auto start( SyncWaitEvent& event ) noexcept {
            this->m_coroutine.promise().Start( event );
        }

        auto return_value() -> decltype( auto ) {
            if constexpr( std::is_same_v<void, TResult> ) {
                this->m_coroutine.promise().result();
                return;
            } else {
                return this->m_coroutine.promise().result();
            }
        }

    private:
        TCoroutine m_coroutine;
    };

    template<Concepts::CAwaitable TAwaitable, typename TResult = Concepts::CAwaitableTraits<TAwaitable&&>::TAwaiterResult>
    static auto MakeSyncWaitTask( TAwaitable&& a ) -> SyncWaitTask<TResult> {
        if constexpr( std::is_void_v<TResult> ) {
            co_await std::forward<TAwaitable>( a );
            co_return;
        } else {
            co_yield co_await std::forward<TAwaitable>( a );
        }
    }

}

template<Concepts::CAwaitable TAwaitable>
auto SyncWait( TAwaitable&& a ) -> decltype( auto ) {
    Private::SyncWaitEvent e {};
    auto task = Private::MakeSyncWaitTask( std::forward<TAwaitable>( a ) );
    task.start( e );
    e.Wait();

    return task.return_value();
}

template<Concepts::CAwaitable awaitable_type, Concepts::CExecutor TExecutor>
void RunAsync( awaitable_type&& awaitable, std::shared_ptr<TExecutor> executor = nullptr ) {
    auto asyncTask = []( auto awaitable, auto executor ) -> Task<void> {
        if( executor ) {
            co_await executor->Schedule();
        }
        co_await awaitable;
        co_return;
    };

    std::thread( []( auto awaitable ) {
        Private::SyncWaitEvent e {};
        auto task = Private::MakeSyncWaitTask( std::forward<awaitable_type>( awaitable ) );
        task.start( e );
        e.Wait();
    }, asyncTask( std::forward<awaitable_type>( awaitable ), executor ) ).detach();
}

}
