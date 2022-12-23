#include "Coroutines/AsyncMutex.h"


namespace Coroutines {
AsyncMutexLock::~AsyncMutexLock() {
    Unlock();
}

auto AsyncMutexLock::Unlock() -> void {
    if( this->m_mutex != nullptr ) {
        std::atomic_thread_fence( std::memory_order::release );
        this->m_mutex->Unlock();
        this->m_mutex = nullptr;
    }
}

auto AsyncMutex::LockOperation::await_ready() const noexcept -> bool {
    if( this->m_mutex.TryLock() ) {
        std::atomic_thread_fence( std::memory_order::acquire );
        return true;
    }
    return false;
}

auto AsyncMutex::LockOperation::await_suspend( std::coroutine_handle<> awaitingCoroutine ) noexcept -> bool {
    this->m_awaitingCoroutine = awaitingCoroutine;
    void* current = this->m_mutex.m_state.load( std::memory_order::acquire );
    void* new_value;

    const void* unlockedValue = this->m_mutex.UnlockedValue();
    do {
        if( current == unlockedValue ) {
            new_value = nullptr;
        } else {
            this->m_next = static_cast<LockOperation*>( current );
            new_value = static_cast<void*>( this );
        }
    } while( !m_mutex.m_state.compare_exchange_weak( current, new_value, std::memory_order::acq_rel ) );

    if( current == unlockedValue ) {
        std::atomic_thread_fence( std::memory_order::acquire );
        this->m_awaitingCoroutine = nullptr;
        return false;
    }

    return true;
}

auto AsyncMutex::TryLock() -> bool {
    void* expected = const_cast<void*>( UnlockedValue() );
    return this->m_state.compare_exchange_strong( expected, nullptr, std::memory_order::acq_rel, std::memory_order::relaxed );
}

auto AsyncMutex::Unlock() -> void {
    if( this->m_internalWaiters == nullptr ) {
        void* current = this->m_state.load( std::memory_order::relaxed );
        if( current == nullptr ) {
            if( this->m_state.compare_exchange_strong( current, const_cast<void*>( UnlockedValue() ), std::memory_order::release,
                                                       std::memory_order::relaxed ) ) {
                return;
            }
        }

        this->m_internalWaiters = static_cast<LockOperation*>( this->m_state.exchange( nullptr, std::memory_order::acq_rel ) );
    }
    LockOperation* to_resume = this->m_internalWaiters;
    this->m_internalWaiters = this->m_internalWaiters->m_next;
    to_resume->m_awaitingCoroutine.resume();
}

}
