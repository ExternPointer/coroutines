#pragma once

#include "Concepts/Executor.h"
#include "Task.h"

#include <atomic>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace Coroutines {

template<Concepts::CExecutor TExecutor>
class TaskContainer {
public:
    using TIterator = std::list<std::size_t>::iterator;

    struct options {
        std::size_t reserve_size { 8 };
        double growth_factor { 2 };
    };


    TaskContainer( std::shared_ptr<TExecutor> e, const options opts = options { .reserve_size = 8, .growth_factor = 2 } )
        : m_growth_factor( opts.growth_factor )
        , m_executor( std::move( e ) )
        , m_executorPtr( m_executor.get() ) {
        if( this->m_executor == nullptr ) {
            throw std::runtime_error { "TaskContainer cannot have a nullptr CExecutor" };
        }

        init( opts.reserve_size );
    }
    TaskContainer( const TaskContainer& ) = delete;
    TaskContainer( TaskContainer&& ) = delete;
    auto operator=( const TaskContainer& ) -> TaskContainer& = delete;
    auto operator=( TaskContainer&& ) -> TaskContainer& = delete;
    ~TaskContainer() {
        while( !IsEmpty() ) {
            GarbageCollect();
        }
    }

    enum class garbage_collect_t { yes, no };

    auto Start( Coroutines::Task<void>&& user_task, garbage_collect_t cleanup = garbage_collect_t::yes ) -> void {
        this->m_size.fetch_add( 1, std::memory_order::relaxed );

        std::scoped_lock lk { this->m_mutex };

        if( cleanup == garbage_collect_t::yes ) {
            gc_internal();
        }

        if( this->m_freePosition == this->m_taskIndices.end() ) {
            this->m_freePosition = grow();
        }

        auto index = *m_freePosition;
        this->m_tasks[ index ] = make_cleanup_task( std::move( user_task ), this->m_freePosition );
        std::advance( this->m_freePosition, 1 );
        this->m_tasks[ index ].resume();
    }

    auto GarbageCollect() -> std::size_t {
        std::scoped_lock lk { this->m_mutex };
        return gc_internal();
    }

    auto CountTasksToDelete() const -> std::size_t {
        std::atomic_thread_fence( std::memory_order::acquire );
        return this->m_tasksToDelete.size();
    }

    auto HasTasksToDelete() const -> bool {
        std::atomic_thread_fence( std::memory_order::acquire );
        return !this->m_tasksToDelete.empty();
    }

    auto Size() const -> std::size_t {
        return this->m_size.load( std::memory_order::relaxed );
    }
    auto IsEmpty() const -> bool {
        return Size() == 0;
    }
    auto Capacity() const -> std::size_t {
        std::atomic_thread_fence( std::memory_order::acquire );
        return this->m_tasks.size();
    }

    auto GarbageCollectAndYieldUntilEmpty() -> Coroutines::Task<void> {
        while( !IsEmpty() ) {
            GarbageCollect();
            co_await this->m_executorPtr->yield();
        }
    }

private:
    auto grow() -> TIterator {
        auto last_pos = std::prev( this->m_taskIndices.end() );
        std::size_t new_size = this->m_tasks.size() * this->m_growth_factor;
        for( std::size_t i = this->m_tasks.size(); i < new_size; ++i ) {
            this->m_taskIndices.emplace_back( i );
        }
        this->m_tasks.resize( new_size );
        return std::next( last_pos );
    }

    auto gc_internal() -> std::size_t {
        std::size_t deleted { 0 };
        if( !this->m_tasksToDelete.empty() ) {
            for( const auto& pos: this->m_tasksToDelete ) {
                this->m_taskIndices.splice( this->m_taskIndices.end(), this->m_taskIndices, pos );
            }
            deleted = this->m_tasksToDelete.size();
            this->m_tasksToDelete.clear();
        }
        return deleted;
    }

    auto make_cleanup_task( Task<void> user_task, TIterator pos ) -> Coroutines::Task<void> {
        // Immediately move the Task onto the CExecutor.
        co_await this->m_executorPtr->Schedule();

        try {
            // Await the users Task to complete.
            co_await user_task;
        } catch( const std::exception& e ) {
            std::cerr << "Coroutines::TaskContainer user_task had an unhandled exception e.what()= " << e.what() << "\n";
        } catch( ... ) {
            std::cerr << "Coroutines::TaskContainer user_task had unhandle exception, not derived from std::exception.\n";
        }

        std::scoped_lock lk { this->m_mutex };
        this->m_tasksToDelete.push_back( pos );
        this->m_size.fetch_sub( 1, std::memory_order::relaxed );
        co_return;
    }

    std::mutex m_mutex {};
    std::atomic<std::size_t> m_size {};
    std::vector<Task<void>> m_tasks {};
    std::list<std::size_t> m_taskIndices {};
    std::vector<TIterator> m_tasksToDelete {};
    TIterator m_freePosition {};
    double m_growth_factor {};
    std::shared_ptr<TExecutor> m_executor { nullptr };
    TExecutor* m_executorPtr { nullptr };

    TaskContainer( TExecutor& e, const options opts = options { .reserve_size = 8, .growth_factor = 2 } )
        : m_growth_factor( opts.growth_factor )
        , m_executorPtr( &e ) {
        init( opts.reserve_size );
    }

    auto init( std::size_t reserve_size ) -> void {
        this->m_tasks.resize( reserve_size );
        for( std::size_t i = 0; i < reserve_size; ++i ) {
            this->m_taskIndices.emplace_back( i );
        }
        this->m_freePosition = this->m_taskIndices.begin();
    }
};

}
