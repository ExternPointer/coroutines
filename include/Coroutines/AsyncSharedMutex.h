#pragma once

#include "Concepts/Executor.h"

#include <atomic>
#include <coroutine>
#include <memory>
#include <mutex>

namespace Coroutines {
template<Concepts::CExecutor TExecutor>
class AsyncSharedMutex;

template<Concepts::CExecutor TExecutor>
class SharedScopedLock {
public:
    SharedScopedLock( AsyncSharedMutex<TExecutor>& sm, bool exclusive )
        : m_sharedMutex( &sm )
        , m_exclusive( exclusive ) {
    }
    ~SharedScopedLock() {
        this->Unlock();
    }
    SharedScopedLock( const SharedScopedLock& ) = delete;
    SharedScopedLock( SharedScopedLock&& other )
        : m_sharedMutex( std::exchange( other.m_sharedMutex, nullptr ) )
        , m_exclusive( other.m_exclusive ) {
    }

    auto operator=( const SharedScopedLock& ) -> SharedScopedLock& = delete;
    auto operator=( SharedScopedLock&& other ) noexcept -> SharedScopedLock& {
        if( std::addressof( other ) != this ) {
            this->m_sharedMutex = std::exchange( other.m_sharedMutex, nullptr );
            this->m_exclusive = other.m_exclusive;
        }
        return *this;
    }

    auto Unlock() -> void {
        if( this->m_sharedMutex != nullptr ) {
            if( this->m_exclusive ) {
                this->m_sharedMutex->Unlock();
            } else {
                this->m_sharedMutex->UnlockShared();
            }

            this->m_sharedMutex = nullptr;
        }
    }

private:
    AsyncSharedMutex<TExecutor>* m_sharedMutex { nullptr };
    bool m_exclusive { false };
};

template<Concepts::CExecutor TExecutor>
class AsyncSharedMutex {
public:
    explicit AsyncSharedMutex( std::shared_ptr<TExecutor> e )
        : m_executor( std::move( e ) ) {
        if( this->m_executor == nullptr ) {
            throw std::runtime_error { "SharedMutex cannot have a nullptr CExecutor" };
        }
    }
    ~AsyncSharedMutex() = default;

    AsyncSharedMutex( const AsyncSharedMutex& ) = delete;
    AsyncSharedMutex( AsyncSharedMutex&& ) = delete;
    auto operator=( const AsyncSharedMutex& ) -> AsyncSharedMutex& = delete;
    auto operator=( AsyncSharedMutex&& ) -> AsyncSharedMutex& = delete;

    class LockOperation {
        LockOperation( AsyncSharedMutex& sm, bool exclusive )
            : m_sharedMutex( sm )
            , m_exclusive( exclusive ) {
        }

        auto await_ready() const noexcept -> bool {
            if( this->m_exclusive ) {
                return this->m_sharedMutex.TryLock();
            } else {
                return this->m_sharedMutex.TryLockShared();
            }
        }

        auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
            std::unique_lock lk { this->m_sharedMutex.m_mutex };
            if( this->m_exclusive ) {
                if( this->m_sharedMutex.TryLockLocked( lk ) ) {
                    return false;
                }
            } else {
                if( this->m_sharedMutex.TryLockSharedLocked( lk ) ) {
                    return false;
                }
            }

            if( this->m_sharedMutex.m_tailWaiter == nullptr ) {
                this->m_sharedMutex.m_headWaiter = this;
                this->m_sharedMutex.m_tailWaiter = this;
            } else {
                this->m_sharedMutex.m_tailWaiter->m_next = this;
                this->m_sharedMutex.m_tailWaiter = this;
            }

            if( this->m_exclusive ) {
                ++m_sharedMutex.m_exclusiveWaiters;
            }

            this->m_awaitingCoroutine = awaiting_coroutine;
            return true;
        }
        auto await_resume() noexcept -> SharedScopedLock<TExecutor> {
            return SharedScopedLock { this->m_sharedMutex, this->m_exclusive };
        }

    private:
        friend class AsyncSharedMutex;

        AsyncSharedMutex& m_sharedMutex;
        bool m_exclusive { false };
        std::coroutine_handle<> m_awaitingCoroutine;
        LockOperation* m_next { nullptr };
    };

    [[nodiscard]] auto LockShared() -> LockOperation {
        return LockOperation { *this, false };
    }
    [[nodiscard]] auto Lock() -> LockOperation {
        return LockOperation { *this, true };
    }
    auto TryLockShared() -> bool {
        std::unique_lock lk { this->m_mutex };
        return TryLockSharedLocked( lk );
    }

    auto TryLock() -> bool {
        std::unique_lock lk { this->m_mutex };
        return TryLockLocked( lk );
    }

    auto UnlockShared() -> void {
        std::unique_lock lk { this->m_mutex };
        --m_sharedUsers;

        if( this->m_sharedUsers == 0 ) {
            if( this->m_headWaiter != nullptr ) {
                WakeWaiters( lk );
            } else {
                this->m_state = State::unlocked;
            }
        }
    }

    auto Unlock() -> void {
        std::unique_lock lk { this->m_mutex };
        if( this->m_headWaiter != nullptr ) {
            WakeWaiters( lk );
        } else {
            this->m_state = State::unlocked;
        }
    }

private:
    friend class LockOperation;

    enum class State { unlocked, locked_shared, locked_exclusive };

    std::shared_ptr<TExecutor> m_executor { nullptr };
    std::mutex m_mutex;
    State m_state { State::unlocked };

    uint64_t m_sharedUsers { 0 };
    uint64_t m_exclusiveWaiters { 0 };

    LockOperation* m_headWaiter { nullptr };
    LockOperation* m_tailWaiter { nullptr };

    auto TryLockSharedLocked( std::unique_lock<std::mutex>& lk ) -> bool {
        if( this->m_state == State::unlocked ) {
            this->m_state = State::locked_shared;
            ++m_sharedUsers;
            lk.unlock();
            return true;
        } else if( this->m_state == State::locked_shared && this->m_exclusiveWaiters == 0 ) {
            ++m_sharedUsers;
            lk.unlock();
            return true;
        }

        return false;
    }

    auto TryLockLocked( std::unique_lock<std::mutex>& lk ) -> bool {
        if( this->m_state == State::unlocked ) {
            this->m_state = State::locked_exclusive;
            lk.unlock();
            return true;
        }
        return false;
    }

    auto WakeWaiters( std::unique_lock<std::mutex>& lk ) -> void {
        if( this->m_headWaiter->m_exclusive ) {
            this->m_state = State::locked_exclusive;
            LockOperation* to_resume = this->m_headWaiter;
            this->m_headWaiter = this->m_headWaiter->m_next;
            --this->m_exclusiveWaiters;
            if( this->m_headWaiter == nullptr ) {
                this->m_tailWaiter = nullptr;
            }

            lk.unlock();
            to_resume->m_awaitingCoroutine.resume();
        } else {
            this->m_state = State::locked_shared;
            do {
                LockOperation* to_resume = this->m_headWaiter;
                this->m_headWaiter = this->m_headWaiter->m_next;
                if( this->m_headWaiter == nullptr ) {
                    this->m_tailWaiter = nullptr;
                }
                ++this->m_sharedUsers;

                this->m_executor->resume( to_resume->m_awaitingCoroutine );
            } while( this->m_headWaiter != nullptr && !m_headWaiter->m_exclusive );

            lk.unlock();
        }
    }
};

}
