#include "Coroutines/Latch.h"


namespace Coroutines {

Latch::Latch( std::ptrdiff_t count ) noexcept
    : m_count( count )
    , m_event( count <= 0 ) {
}

bool Latch::is_ready() const noexcept {
    return this->m_event.IsSet();
}

std::size_t Latch::remaining() const noexcept {
    return this->m_count.load( std::memory_order::acquire );
}

void Latch::CountDown( std::ptrdiff_t n ) noexcept {
    if( this->m_count.fetch_sub( n, std::memory_order::acq_rel ) <= n ) {
        this->m_event.Set();
    }
}

void Latch::CountDown( ThreadPool& tp, std::ptrdiff_t n ) noexcept {
    if( this->m_count.fetch_sub( n, std::memory_order::acq_rel ) <= n ) {
        this->m_event.Set( tp );
    }
}

Event::Awaiter Latch::operator co_await() const noexcept {
    return this->m_event.operator co_await();
}

}