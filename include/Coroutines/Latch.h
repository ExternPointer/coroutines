#pragma once

#include <atomic>

#include "Event.h"
#include "ThreadPool.h"


namespace Coroutines {

class Latch {
public:
    Latch( std::ptrdiff_t count ) noexcept;

    Latch( const Latch& ) = delete;
    Latch( Latch&& ) = delete;
    auto operator=( const Latch& ) -> Latch& = delete;
    auto operator=( Latch&& ) -> Latch& = delete;

    bool is_ready() const noexcept;
    std::size_t remaining() const noexcept;
    void CountDown( std::ptrdiff_t n = 1 ) noexcept;
    void CountDown( Coroutines::ThreadPool& tp, std::ptrdiff_t n = 1 ) noexcept;
    Event::Awaiter operator co_await() const noexcept;

private:
    std::atomic<std::ptrdiff_t> m_count;
    Event m_event;
};

}
