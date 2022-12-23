#pragma once

#include <coroutine>
#include <memory>
#include <type_traits>

namespace Coroutines {
template<typename T>
class Generator;

namespace Private {
    template<typename T>
    class GeneratorPromise {
    public:
        using TResult = std::remove_reference_t<T>;
        using TResultRef = std::conditional_t<std::is_reference_v<T>, T, T&>;
        using TResultPtr = TResult*;

        GeneratorPromise() = default;

        auto get_return_object() noexcept -> Generator<T>;

        auto initial_suspend() const {
            return std::suspend_always {};
        }

        auto final_suspend() const noexcept( true ) {
            return std::suspend_always {};
        }

        template<typename U = T, std::enable_if_t<!std::is_rvalue_reference<U>::value, int> = 0>
        auto yield_value( std::remove_reference_t<T>& value ) noexcept {
            this->m_value = std::addressof( value );
            return std::suspend_always {};
        }

        auto yield_value( std::remove_reference_t<T>&& value ) noexcept {
            this->m_value = std::addressof( value );
            return std::suspend_always {};
        }

        auto unhandled_exception() -> void {
            this->m_exception = std::current_exception();
        }

        auto return_void() noexcept -> void {
        }

        auto value() const noexcept -> TResultRef {
            return static_cast<TResultRef>( *m_value );
        }

        template<typename U>
        auto await_transform( U&& value ) -> std::suspend_never = delete;

        auto rethrow_if_exception() -> void {
            if( this->m_exception ) {
                std::rethrow_exception( this->m_exception );
            }
        }

    private:
        TResultPtr m_value { nullptr };
        std::exception_ptr m_exception;
    };

    struct GeneratorSentinel {};

    template<typename T>
    class GeneratorIterator {
        using TCoroutineHandle = std::coroutine_handle<GeneratorPromise<T>>;

    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = typename GeneratorPromise<T>::TResult;
        using reference = typename GeneratorPromise<T>::TResultRef;
        using pointer = typename GeneratorPromise<T>::TResultPtr;

        GeneratorIterator() noexcept {
        }

        explicit GeneratorIterator( TCoroutineHandle coroutine ) noexcept
            : m_coroutine( coroutine ) {
        }

        friend auto operator==( const GeneratorIterator& it, GeneratorSentinel ) noexcept -> bool {
            return it.m_coroutine == nullptr || it.m_coroutine.done();
        }

        friend auto operator!=( const GeneratorIterator& it, GeneratorSentinel s ) noexcept -> bool {
            return it != s;
        }

        friend auto operator==( GeneratorSentinel s, const GeneratorIterator& it ) noexcept -> bool {
            return it == s;
        }

        friend auto operator!=( GeneratorSentinel s, const GeneratorIterator& it ) noexcept -> bool {
            return it != s;
        }

        GeneratorIterator& operator++() {
            this->m_coroutine.resume();
            if( this->m_coroutine.done() ) {
                this->m_coroutine.promise().rethrow_if_exception();
            }

            return *this;
        }

        auto operator++( int ) -> void {
            (void)operator++();
        }

        reference operator*() const noexcept {
            return this->m_coroutine.promise().value();
        }

        pointer operator->() const noexcept {
            return std::addressof( operator*() );
        }

    private:
        TCoroutineHandle m_coroutine { nullptr };
    };

}

template<typename T>
class Generator {
public:
    using promise_type = Private::GeneratorPromise<T>;
    using iterator = Private::GeneratorIterator<T>;
    using sentinel = Private::GeneratorSentinel;

    Generator() noexcept
        : m_coroutine( nullptr ) {
    }

    Generator( const Generator& ) = delete;
    Generator( Generator&& other ) noexcept
        : m_coroutine( other.m_coroutine ) {
        other.m_coroutine = nullptr;
    }

    auto operator=( const Generator& ) = delete;
    auto operator=( Generator&& other ) noexcept -> Generator& {
        this->m_coroutine = other.m_coroutine;
        other.m_coroutine = nullptr;

        return *this;
    }

    ~Generator() {
        if( this->m_coroutine ) {
            this->m_coroutine.destroy();
        }
    }

    auto begin() -> iterator {
        if( this->m_coroutine != nullptr ) {
            this->m_coroutine.resume();
            if( this->m_coroutine.done() ) {
                this->m_coroutine.promise().rethrow_if_exception();
            }
        }

        return iterator { this->m_coroutine };
    }

    auto end() noexcept -> sentinel {
        return sentinel {};
    }

private:
    friend class Private::GeneratorPromise<T>;

    explicit Generator( std::coroutine_handle<promise_type> coroutine ) noexcept
        : m_coroutine( coroutine ) {
    }

    std::coroutine_handle<promise_type> m_coroutine;
};

namespace Private {
    template<typename T>
    auto GeneratorPromise<T>::get_return_object() noexcept -> Generator<T> {
        return Generator<T> { std::coroutine_handle<GeneratorPromise<T>>::from_promise( *this ) };
    }

}

}
