#pragma once

#include <coroutine>
#include <exception>
#include <utility>


namespace Coroutines {
template<typename return_type = void>
class Task;

namespace Private {
    struct PromiseBase {
        friend class FinalAwaitable;
        struct FinalAwaitable {
            auto await_ready() const noexcept -> bool {
                return false;
            }

            template<typename TPromise>
            auto await_suspend( std::coroutine_handle<TPromise> coroutine ) noexcept -> std::coroutine_handle<> {
                auto& promise = coroutine.promise();
                if( promise.m_continuation != nullptr ) {
                    return promise.m_continuation;
                } else {
                    return std::noop_coroutine();
                }
            }

            auto await_resume() noexcept -> void {
            }
        };

        PromiseBase() noexcept = default;
        ~PromiseBase() = default;

        auto initial_suspend() {
            return std::suspend_always {};
        }

        auto final_suspend() noexcept( true ) {
            return FinalAwaitable {};
        }

        auto unhandled_exception() -> void {
            this->m_exception = std::current_exception();
        }

        auto continuation( std::coroutine_handle<> continuation ) noexcept -> void {
            this->m_continuation = continuation;
        }

    protected:
        std::coroutine_handle<> m_continuation { nullptr };
        std::exception_ptr m_exception {};
    };

    template<typename TResult>
    struct Promise final : public PromiseBase {
        using TTask = Task<TResult>;
        using TCoroutineHandle = std::coroutine_handle<Promise<TResult>>;

        Promise() noexcept = default;
        ~Promise() = default;

        auto get_return_object() noexcept -> TTask;

        auto return_value( TResult value ) -> void {
            this->m_result = std::move( value );
        }

        auto result() const& -> const TResult& {
            if( this->m_exception ) {
                std::rethrow_exception( this->m_exception );
            }

            return this->m_result;
        }

        auto result() && -> TResult&& {
            if( this->m_exception ) {
                std::rethrow_exception( this->m_exception );
            }

            return std::move( this->m_result );
        }

    private:
        TResult m_result;
    };

    template<>
    struct Promise<void> : public PromiseBase {
        using TTask = Task<void>;
        using TCoroutineHandle = std::coroutine_handle<Promise<void>>;

        Promise() noexcept = default;
        ~Promise() = default;

        auto get_return_object() noexcept -> TTask;

        auto return_void() noexcept -> void {
        }

        auto result() -> void {
            if( m_exception ) {
                std::rethrow_exception( m_exception );
            }
        }
    };

}

template<typename TResult>
class [[nodiscard]] Task {
public:
    using promise_type = Private::Promise<TResult>;
    using TTask = Task<TResult>;
    using TCoroutineHandle = std::coroutine_handle<promise_type>;

    struct AwaitableBase {
        AwaitableBase( TCoroutineHandle coroutine ) noexcept
            : m_coroutine( coroutine ) {
        }

        auto await_ready() const noexcept -> bool {
            return !this->m_coroutine || this->m_coroutine.done();
        }

        auto await_suspend( std::coroutine_handle<> awaitingCoroutine ) noexcept -> std::coroutine_handle<> {
            this->m_coroutine.promise().continuation( awaitingCoroutine );
            return this->m_coroutine;
        }

        std::coroutine_handle<promise_type> m_coroutine { nullptr };
    };

    Task() noexcept
        : m_coroutine( nullptr ) {
    }

    explicit Task( TCoroutineHandle handle )
        : m_coroutine( handle ) {
    }
    Task( const Task& ) = delete;
    Task( Task&& other ) noexcept
        : m_coroutine( std::exchange( other.m_coroutine, nullptr ) ) {
    }

    ~Task() {
        if( this->m_coroutine ) {
            this->m_coroutine.destroy();
        }
    }

    auto operator=( const Task& ) -> Task& = delete;

    auto operator=( Task&& other ) noexcept -> Task& {
        if( std::addressof( other ) != this ) {
            if( this->m_coroutine ) {
                this->m_coroutine.destroy();
            }

            this->m_coroutine = std::exchange( other.m_coroutine, nullptr );
        }

        return *this;
    }

    auto is_ready() const noexcept -> bool {
        return !this->m_coroutine || this->m_coroutine.done();
    }

    auto resume() -> bool {
        if( !this->m_coroutine.done() ) {
            this->m_coroutine.resume();
        }
        return !this->m_coroutine.done();
    }

    auto destroy() -> bool {
        if( this->m_coroutine ) {
            this->m_coroutine.destroy();
            this->m_coroutine = nullptr;
            return true;
        }

        return false;
    }

    auto operator co_await() const& noexcept {
        struct awaitable : public AwaitableBase {
            auto await_resume() -> decltype( auto ) {
                if constexpr( std::is_same_v<void, TResult> ) {
                    this->m_coroutine.promise().result();
                    return;
                } else {
                    return this->m_coroutine.promise().result();
                }
            }
        };

        return awaitable { this->m_coroutine };
    }

    auto operator co_await() const&& noexcept {
        struct awaitable : public AwaitableBase {
            auto await_resume() -> decltype( auto ) {
                if constexpr( std::is_same_v<void, TResult> ) {
                    this->m_coroutine.promise().result();
                    return;
                } else {
                    return std::move( this->m_coroutine.promise() ).result();
                }
            }
        };

        return awaitable { this->m_coroutine };
    }

    auto promise() & -> promise_type& {
        return this->m_coroutine.promise();
    }

    auto promise() const& -> const promise_type& {
        return this->m_coroutine.promise();
    }
    auto promise() && -> promise_type&& {
        return std::move( this->m_coroutine.promise() );
    }

    auto handle() -> TCoroutineHandle {
        return this->m_coroutine;
    }

private:
    TCoroutineHandle m_coroutine { nullptr };
};

namespace Private {
    template<typename TResult>
    inline auto Promise<TResult>::get_return_object() noexcept -> Task<TResult> {
        return Task<TResult> { TCoroutineHandle::from_promise( *this ) };
    }

    inline auto Promise<void>::get_return_object() noexcept -> Task<> {
        return Task<> { TCoroutineHandle::from_promise( *this ) };
    }

}

}
