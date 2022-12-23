#include "Coroutines/Semaphore.h"


namespace Coroutines {
Semaphore::Semaphore( std::ptrdiff_t least_max_value_and_starting_value )
    : Semaphore( least_max_value_and_starting_value, least_max_value_and_starting_value ) {
}

Semaphore::Semaphore( std::ptrdiff_t least_max_value, std::ptrdiff_t starting_value )
    : m_leastMaxValue( least_max_value )
    , m_counter( starting_value <= least_max_value ? starting_value : least_max_value ) {
}

Semaphore::~Semaphore() {
    StopSignalNotifyWaiters();
}

Semaphore::AcquireOperation::AcquireOperation( Semaphore& s )
    : m_semaphore( s ) {
}

auto Semaphore::AcquireOperation::await_ready() const noexcept -> bool {
    if( this->m_semaphore.m_notify_all_set.load( std::memory_order::relaxed ) ) {
        return true;
    }
    return this->m_semaphore.TryAcquire();
}

auto Semaphore::AcquireOperation::await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
    std::unique_lock lk { this->m_semaphore.m_waiterMutex };
    if( this->m_semaphore.m_notify_all_set.load( std::memory_order::relaxed ) ) {
        return false;
    }

    if( this->m_semaphore.TryAcquire() ) {
        return false;
    }

    if( this->m_semaphore.m_acquireWaiters == nullptr ) {
        this->m_semaphore.m_acquireWaiters = this;
    } else {
        this->m_next = this->m_semaphore.m_acquireWaiters;
        this->m_semaphore.m_acquireWaiters = this;
    }

    this->m_awaiting_coroutine = awaiting_coroutine;
    return true;
}

auto Semaphore::AcquireOperation::await_resume() const -> void {
    if( this->m_semaphore.m_notify_all_set.load( std::memory_order::relaxed ) ) {
        throw Coroutines::StopSignal {};
    }
}

auto Semaphore::Release() -> void {
    std::unique_lock lk { this->m_waiterMutex };
    if( this->m_acquireWaiters != nullptr ) {
        AcquireOperation* to_resume = this->m_acquireWaiters;
        this->m_acquireWaiters = this->m_acquireWaiters->m_next;
        lk.unlock();
        to_resume->m_awaiting_coroutine.resume();
    } else {
        this->m_counter.fetch_add( 1, std::memory_order::relaxed );
    }
}

auto Semaphore::TryAcquire() -> bool {
    auto previous = this->m_counter.fetch_sub( 1, std::memory_order::acq_rel );
    if( previous <= 0 ) {
        this->m_counter.fetch_add( 1, std::memory_order::release );
        return false;
    }
    return true;
}

auto Semaphore::StopSignalNotifyWaiters() noexcept -> void {
    this->m_notify_all_set.exchange( true, std::memory_order::release );
    while( true ) {
        std::unique_lock lk { this->m_waiterMutex };
        if( this->m_acquireWaiters != nullptr ) {
            AcquireOperation* to_resume = this->m_acquireWaiters;
            this->m_acquireWaiters = this->m_acquireWaiters->m_next;
            lk.unlock();

            to_resume->m_awaiting_coroutine.resume();
        } else {
            break;
        }
    }
}

auto Semaphore::max() const noexcept -> std::ptrdiff_t {
    return this->m_leastMaxValue;
}

auto Semaphore::value() const noexcept -> std::ptrdiff_t {
    return this->m_counter.load( std::memory_order::relaxed );
}

}
