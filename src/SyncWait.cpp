#include "Coroutines/SyncWait.h"

namespace Coroutines::Private {
SyncWaitEvent::SyncWaitEvent( bool initially_set )
    : m_set( initially_set ) {
}

auto SyncWaitEvent::Set() noexcept -> void {
    {
        std::lock_guard<std::mutex> g { this->m_mutex };
        this->m_set = true;
    }

    m_cv.notify_all();
}

auto SyncWaitEvent::Reset() noexcept -> void {
    std::lock_guard<std::mutex> g { this->m_mutex };
    this->m_set = false;
}

auto SyncWaitEvent::Wait() noexcept -> void {
    std::unique_lock<std::mutex> lk { this->m_mutex };
    m_cv.wait( lk, [ this ] { return this->m_set; } );
}

}