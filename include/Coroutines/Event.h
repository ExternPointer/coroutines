#pragma once

#include <atomic>
//#include <coroutine>
#include <experimental/coroutine>
#include "Concepts/Executor.h"


namespace Coroutines {
enum class ResumeOrderPolicy { lifo, fifo };

class Event {
public:
    struct Awaiter {
        Awaiter( const Event& e ) noexcept
            : m_event( e ) {
        }
        auto await_ready() const noexcept -> bool {
            return this->m_event.IsSet();
        }
        auto await_suspend( std::coroutine_handle<> awaitingCoroutine ) noexcept -> bool;
        auto await_resume() noexcept {
        }

        const Event& m_event;
        std::coroutine_handle<> m_awaitingCoroutine;
        Awaiter* m_next { nullptr };
    };

    explicit Event( bool initially_set = false ) noexcept;
    ~Event() = default;

    Event( const Event& ) = delete;
    Event( Event&& ) = delete;
    auto operator=( const Event& ) -> Event& = delete;
    auto operator=( Event&& ) -> Event& = delete;

    auto IsSet() const noexcept -> bool {
        return this->m_state.load( std::memory_order_acquire ) == this;
    }

    auto Set( ResumeOrderPolicy policy = ResumeOrderPolicy::lifo ) noexcept -> void;

    template<Concepts::CExecutor TExecutor>
    auto Set( TExecutor& e, ResumeOrderPolicy policy = ResumeOrderPolicy::lifo ) noexcept -> void {
        void* oldValue = this->m_state.exchange( this, std::memory_order::acq_rel );
        if( oldValue != this ) {
            if( policy == ResumeOrderPolicy::fifo ) {
                oldValue = Reverse( static_cast<Awaiter*>( oldValue ) );
            }

            auto* waiters = static_cast<Awaiter*>( oldValue );
            while( waiters != nullptr ) {
                auto* next = waiters->m_next;
                e.resume( waiters->m_awaitingCoroutine );
                waiters = next;
            }
        }
    }

    auto operator co_await() const noexcept -> Awaiter {
        return Awaiter( *this );
    }

    auto reset() noexcept -> void;

protected:
    friend struct Awaiter;
    mutable std::atomic<void*> m_state;

private:
    auto Reverse( Awaiter* head ) -> Awaiter*;
};

}
