#pragma once

#include "Awaitable.h"

#include <concepts>
//#include <coroutine>

#include <experimental/coroutine>
namespace std {
    using namespace experimental;
}


namespace Coroutines::Concepts {
template<typename T>
concept CExecutor = requires( T t, std::coroutine_handle<> c ) {
                       { t.Schedule() } -> CAwaiter;
                       { t.yield() } -> CAwaiter;
                       { t.resume( c ) } -> std::same_as<void>;
                   };

}
