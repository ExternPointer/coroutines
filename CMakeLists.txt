cmake_minimum_required( VERSION 3.20 )
project( coroutines LANGUAGES CXX )
set( CMAKE_CXX_STANDARD 23 )

set( SOURCES
        src/AsyncMutex.cpp
        src/Event.cpp
        src/Latch.cpp
        src/Semaphore.cpp
        src/SyncWait.cpp
        src/ThreadPool.cpp )

set( HEADERS
        include/Coroutines/Concepts/Awaitable.h
        include/Coroutines/Concepts/Executor.h
        include/Coroutines/Concepts/RangeOf.h
        include/Coroutines/Private/VoidValue.h
		include/Coroutines/Async.h
		include/Coroutines/AsyncMutex.h
		include/Coroutines/AsyncSharedMutex.h
        include/Coroutines/Event.h
        include/Coroutines/Generator.h
        include/Coroutines/Latch.h
        include/Coroutines/RingBuffer.h
        include/Coroutines/Semaphore.h
        include/Coroutines/StopSignal.h
        include/Coroutines/SyncWait.h
        include/Coroutines/Task.h
        include/Coroutines/TaskContainer.h
        include/Coroutines/ThreadPool.h
        include/Coroutines/WhenAll.h )

add_library( ${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS} )
target_include_directories( ${PROJECT_NAME} PUBLIC include )
