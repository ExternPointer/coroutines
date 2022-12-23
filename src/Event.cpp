#include "Coroutines/Event.h"


namespace Coroutines {
Event::Event( bool initially_set ) noexcept
    : m_state( ( initially_set ) ? static_cast<void*>( this ) : nullptr ) {
}

auto Event::Set( ResumeOrderPolicy policy ) noexcept -> void {
    void* oldValue = this->m_state.exchange( this, std::memory_order::acq_rel );
    if( oldValue != this ) {
        if( policy == ResumeOrderPolicy::fifo ) {
            oldValue = Reverse( static_cast<Awaiter*>( oldValue ) );
        }

        auto* waiters = static_cast<Awaiter*>( oldValue );
        while( waiters != nullptr ) {
            auto* next = waiters->m_next;
            waiters->m_awaitingCoroutine.resume();
            waiters = next;
        }
    }
}

auto Event::Reverse( Awaiter* head ) -> Awaiter* {
    if( head == nullptr || head->m_next == nullptr ) {
        return head;
    }

    Awaiter* prev = nullptr;
    Awaiter* next = nullptr;
    while( head != nullptr ) {
        next = head->m_next;
        head->m_next = prev;
        prev = head;
        head = next;
    }

    return prev;
}

auto Event::Awaiter::await_suspend( std::coroutine_handle<> awaitingCoroutine ) noexcept -> bool {
    const void* const set_state = &m_event;

    this->m_awaitingCoroutine = awaitingCoroutine;

    void* old_value = this->m_event.m_state.load( std::memory_order::acquire );
    do {
        if( old_value == set_state ) {
            return false;
        }

        this->m_next = static_cast<Awaiter*>( old_value );
    } while( !this->m_event.m_state.compare_exchange_weak( old_value, this, std::memory_order::release, std::memory_order::acquire ) );

    return true;
}

auto Event::reset() noexcept -> void {
    void* old_value = this;
    this->m_state.compare_exchange_strong( old_value, nullptr, std::memory_order::acquire );
}

}
