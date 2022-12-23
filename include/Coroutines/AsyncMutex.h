#pragma once

#include <atomic>
#include <coroutine>
#include <mutex>

namespace Coroutines {
class AsyncMutex;

class AsyncMutexLock {
    friend class AsyncMutex;

public:
    enum class lock_strategy { adopt };

    explicit AsyncMutexLock( AsyncMutex& m, lock_strategy strategy = lock_strategy::adopt )
        : m_mutex( &m ) {
        (void)strategy;
    }

    ~AsyncMutexLock();

    AsyncMutexLock( const AsyncMutexLock& ) = delete;
    AsyncMutexLock( AsyncMutexLock&& other ) noexcept
        : m_mutex( std::exchange( other.m_mutex, nullptr ) ) {
    }
    auto operator=( const AsyncMutexLock& ) -> AsyncMutexLock& = delete;
    auto operator=( AsyncMutexLock&& other ) noexcept -> AsyncMutexLock& {
        if( std::addressof( other ) != this ) {
            m_mutex = std::exchange( other.m_mutex, nullptr );
        }
        return *this;
    }

    auto Unlock() -> void;

private:
    AsyncMutex* m_mutex { nullptr };
};

class AsyncMutex {
public:
    explicit AsyncMutex() noexcept
        : m_state( const_cast<void*>( UnlockedValue() ) ) {
    }
    ~AsyncMutex() = default;

    AsyncMutex( const AsyncMutex& ) = delete;
    AsyncMutex( AsyncMutex&& ) = delete;
    auto operator=( const AsyncMutex& ) -> AsyncMutex& = delete;
    auto operator=( AsyncMutex&& ) -> AsyncMutex& = delete;

    class LockOperation {
        explicit LockOperation( AsyncMutex& m )
            : m_mutex( m ) {
        }

        auto await_ready() const noexcept -> bool;
        auto await_suspend( std::coroutine_handle<> awaitingCoroutine ) noexcept -> bool;
        auto await_resume() noexcept -> AsyncMutexLock {
            return AsyncMutexLock { this->m_mutex };
        }

    private:
        friend class AsyncMutex;

        AsyncMutex& m_mutex;
        std::coroutine_handle<> m_awaitingCoroutine;
        LockOperation* m_next { nullptr };
    };

    [[nodiscard]] auto Lock() -> LockOperation {
        return LockOperation { *this };
    };

    auto TryLock() -> bool;
    auto Unlock() -> void;

private:
    friend class LockOperation;
    auto UnlockedValue() const noexcept -> const void* {
        return &this->m_state;
    }

    std::atomic<void*> m_state;
    LockOperation* m_internalWaiters { nullptr };
};

}
