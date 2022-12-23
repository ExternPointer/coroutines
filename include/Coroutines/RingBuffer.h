#pragma once

#include "StopSignal.h"

#include <array>
#include <atomic>
#include <coroutine>
#include <mutex>
#include <optional>

namespace Coroutines {

template<typename TElement, size_t COUNT>
class RingBuffer {
public:
    RingBuffer() {
        if( COUNT == 0 ) {
            throw std::runtime_error { "COUNT cannot be zero" };
        }
    }

    ~RingBuffer() {
        StopSignalNotifyWaiters();
    }

    RingBuffer( const RingBuffer<TElement, COUNT>& ) = delete;
    RingBuffer( RingBuffer<TElement, COUNT>&& ) = delete;

    auto operator=( const RingBuffer<TElement, COUNT>& ) noexcept -> RingBuffer<TElement, COUNT>& = delete;
    auto operator=( RingBuffer<TElement, COUNT>&& ) noexcept -> RingBuffer<TElement, COUNT>& = delete;

    struct ProduceOperation {
        ProduceOperation( RingBuffer<TElement, COUNT>& rb, TElement e )
            : m_rb( rb )
            , m_e( std::move( e ) ) {
        }

        auto await_ready() noexcept -> bool {
            std::unique_lock lk { this->m_rb.m_mutex };
            return this->m_rb.try_produce_locked( lk, this->m_e );
        }

        auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
            std::unique_lock lk { this->m_rb.m_mutex };
            if( this->m_rb.m_stopped.load( std::memory_order::acquire ) ) {
                this->m_stopped = true;
                return false;
            }

            this->m_awaiting_coroutine = awaiting_coroutine;
            this->m_next = this->m_rb.m_produceWaiters;
            this->m_rb.m_produceWaiters = this;
            return true;
        }

        auto await_resume() -> void {
            if( this->m_stopped ) {
                throw this->StopSignal {};
            }
        }

    private:
        template<typename element_subtype, size_t num_elements_subtype>
        friend class RingBuffer;

        RingBuffer<TElement, COUNT>& m_rb;
        std::coroutine_handle<> m_awaiting_coroutine;
        ProduceOperation* m_next { nullptr };
        TElement m_e;
        bool m_stopped { false };
    };

    struct ConsumeOperation {
        explicit ConsumeOperation( RingBuffer<TElement, COUNT>& rb )
            : m_rb( rb ) {
        }

        auto await_ready() noexcept -> bool {
            std::unique_lock lk { this->m_rb.m_mutex };
            return this->m_rb.try_consume_locked( lk, this );
        }

        auto await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> bool {
            std::unique_lock lk { this->m_rb.m_mutex };
            if( this->m_rb.m_stopped.load( std::memory_order::acquire ) ) {
                this->m_stopped = true;
                return false;
            }
            this->m_awaiting_coroutine = awaiting_coroutine;
            this->m_next = this->m_rb.m_consumeWaiters;
            this->m_rb.m_consumeWaiters = this;
            return true;
        }

        auto await_resume() -> TElement {
            if( this->m_stopped ) {
                throw this->StopSignal {};
            }

            return std::move( this->m_e );
        }

    private:
        template<typename element_subtype, size_t num_elements_subtype>
        friend class RingBuffer;

        RingBuffer<TElement, COUNT>& m_rb;
        std::coroutine_handle<> m_awaiting_coroutine;
        ConsumeOperation* m_next { nullptr };
        TElement m_e;
        bool m_stopped { false };
    };

    [[nodiscard]] auto Produce( TElement e ) -> ProduceOperation {
        return ProduceOperation { *this, std::move( e ) };
    }
    [[nodiscard]] auto Consume() -> ConsumeOperation {
        return ConsumeOperation { *this };
    }

    auto size() const -> size_t {
        std::atomic_thread_fence( std::memory_order::acquire );
        return this->m_used;
    }

    auto empty() const -> bool {
        return size() == 0;
    }

    auto StopSignalNotifyWaiters() -> void {
        if( this->m_stopped.load( std::memory_order::acquire ) ) {
            return;
        }

        std::unique_lock lk { this->m_mutex };
        this->m_stopped.exchange( true, std::memory_order::release );

        while( this->m_produceWaiters != nullptr ) {
            auto* toResume = this->m_produceWaiters;
            toResume->m_stopped = true;
            this->m_produceWaiters = this->m_produceWaiters->m_next;

            lk.unlock();
            toResume->m_awaiting_coroutine.resume();
            lk.lock();
        }

        while( this->m_consumeWaiters != nullptr ) {
            auto* toResume = this->m_consumeWaiters;
            toResume->m_stopped = true;
            this->m_consumeWaiters = this->m_consumeWaiters->m_next;

            lk.unlock();
            toResume->m_awaiting_coroutine.resume();
            lk.lock();
        }
    }

private:
    friend ProduceOperation;
    friend ConsumeOperation;

    std::mutex m_mutex {};

    std::array<TElement, COUNT> m_elements {};
    size_t m_front { 0 };
    size_t m_back { 0 };
    size_t m_used { 0 };

    ProduceOperation* m_produceWaiters { nullptr };
    ConsumeOperation* m_consumeWaiters { nullptr };

    std::atomic<bool> m_stopped { false };

    auto try_produce_locked( std::unique_lock<std::mutex>& lk, TElement& e ) -> bool {
        if( this->m_used == COUNT ) {
            return false;
        }

        this->m_elements[ this->m_front ] = std::move( e );
        this->m_front = ( this->m_front + 1 ) % COUNT;
        ++this->m_used;

        if( this->m_consumeWaiters != nullptr ) {
            ConsumeOperation* to_resume = this->m_consumeWaiters;
            this->m_consumeWaiters = this->m_consumeWaiters->m_next;

            to_resume->m_e = std::move( this->m_elements[ this->m_back ] );
            this->m_back = ( this->m_back + 1 ) % COUNT;
            --this->m_used;

            lk.unlock();
            to_resume->m_awaiting_coroutine.resume();
        }

        return true;
    }

    auto try_consume_locked( std::unique_lock<std::mutex>& lk, ConsumeOperation* op ) -> bool {
        if( this->m_used == 0 ) {
            return false;
        }

        op->m_e = std::move( this->m_elements[ this->m_back ] );
        this->m_back = ( this->m_back + 1 ) % COUNT;
        --this->m_used;

        if( this->m_produceWaiters != nullptr ) {
            ProduceOperation* to_resume = this->m_produceWaiters;
            this->m_produceWaiters = this->m_produceWaiters->m_next;

            this->m_elements[ this->m_front ] = std::move( to_resume->m_e );
            this->m_front = ( this->m_front + 1 ) % COUNT;
            ++this->m_used;

            lk.unlock();
            to_resume->m_awaiting_coroutine.resume();
        }

        return true;
    }
};

}
