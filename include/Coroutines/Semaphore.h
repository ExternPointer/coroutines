#pragma once

#include <atomic>
#include <coroutine>
#include <mutex>

#include "StopSignal.h"


namespace Coroutines {
class Semaphore {
public:
    explicit Semaphore( std::ptrdiff_t least_max_value_and_starting_value );
    explicit Semaphore( std::ptrdiff_t least_max_value, std::ptrdiff_t starting_value );
    ~Semaphore();

    Semaphore( const Semaphore& ) = delete;
    Semaphore( Semaphore&& ) = delete;

    auto operator=( const Semaphore& ) noexcept -> Semaphore& = delete;
    auto operator=( Semaphore&& ) noexcept -> Semaphore& = delete;

    class AcquireOperation {
    public:
        explicit AcquireOperation( Semaphore& s );

        auto await_ready() const noexcept -> bool;
        auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool;
        auto await_resume() const -> void;

    private:
        friend Semaphore;

        Semaphore& m_semaphore;
        std::coroutine_handle<> m_awaiting_coroutine;
        AcquireOperation* m_next { nullptr };
    };

    auto Release() -> void;

    [[nodiscard]] auto Acquire() -> AcquireOperation {
        return AcquireOperation { *this };
    }
    auto TryAcquire() -> bool;
    auto max() const noexcept -> std::ptrdiff_t;
    auto value() const noexcept -> std::ptrdiff_t;
    auto StopSignalNotifyWaiters() noexcept -> void;

private:
    friend class AcquireOperation;

    const std::ptrdiff_t m_leastMaxValue;
    std::atomic<std::ptrdiff_t> m_counter;

    std::mutex m_waiterMutex {};
    AcquireOperation* m_acquireWaiters { nullptr };

    std::atomic<bool> m_notify_all_set { false };
};

}
