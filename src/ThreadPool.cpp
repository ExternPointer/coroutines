#include "Coroutines/ThreadPool.h"

#include <iostream>

namespace Coroutines {
ThreadPool::Operation::Operation( ThreadPool& tp ) noexcept
    : m_threadPool( tp ) {
}

auto ThreadPool::Operation::await_suspend( std::coroutine_handle<> awaiting_coroutine ) noexcept -> void {
    this->m_awaitingCoroutine = awaiting_coroutine;
    this->m_threadPool.ScheduleImpl( this->m_awaitingCoroutine );
}

ThreadPool::ThreadPool( options opts )
    : m_opts( std::move( opts ) ) {
    this->m_threads.reserve( this->m_opts.thread_count );

    for( uint32_t i = 0; i < this->m_opts.thread_count; ++i ) {
        this->m_threads.emplace_back( [ this, i ]( std::stop_token st ) { Executor( std::move( st ), i ); } );
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

auto ThreadPool::Schedule() -> Operation {
    if( !this->m_shutdownRequested.load( std::memory_order::relaxed ) ) {
        this->m_size.fetch_add( 1, std::memory_order::release );
        return Operation { *this };
    }

    throw std::runtime_error( "Coroutines::ThreadPool is shutting down, unable to Schedule new tasks." );
}

auto ThreadPool::resume( std::coroutine_handle<> handle ) noexcept -> void {
    if( handle == nullptr ) {
        return;
    }

    this->m_size.fetch_add( 1, std::memory_order::release );
    ScheduleImpl( handle );
}

auto ThreadPool::shutdown() noexcept -> void {
    if( this->m_shutdownRequested.exchange( true, std::memory_order::acq_rel ) == false ) {
        for( auto& thread: this->m_threads ) {
            thread.request_stop();
        }

        for( auto& thread: this->m_threads ) {
            if( thread.joinable() ) {
                thread.join();
            }
        }
    }
}

auto ThreadPool::Executor( std::stop_token stop_token, std::size_t idx ) -> void {
    if( this->m_opts.on_thread_start_functor != nullptr ) {
        this->m_opts.on_thread_start_functor( idx );
    }

    while( !stop_token.stop_requested() ) {
        while( true ) {
            std::unique_lock<std::mutex> lk { this->m_waitMutex };
            this->m_waitCv.wait( lk, stop_token, [ this ] { return !this->m_queue.empty(); } );
            if( this->m_queue.empty() ) {
                lk.unlock();
                break;
            }

            auto handle = this->m_queue.front();
            this->m_queue.pop_front();

            lk.unlock();

            handle.resume();
            this->m_size.fetch_sub( 1, std::memory_order::release );
        }
    }

    if( this->m_opts.on_thread_stop_functor != nullptr ) {
        this->m_opts.on_thread_stop_functor( idx );
    }
}

auto ThreadPool::ScheduleImpl( std::coroutine_handle<> handle ) noexcept -> void {
    if( handle == nullptr ) {
        return;
    }

    {
        std::scoped_lock lk { this->m_waitMutex };
        this->m_queue.emplace_back( handle );
    }

    this->m_waitCv.notify_one();
}

}
