#pragma once

#include <concepts>
//#include <coroutine>
#include <experimental/coroutine>
#include <type_traits>
#include <utility>

namespace std {
    using namespace experimental;
}

namespace Coroutines::Concepts {
template<typename T>
concept CAwaiter = requires( T t, std::coroutine_handle<> c ) {
                      { t.await_ready() } -> std::same_as<bool>;
                      std::same_as<decltype( t.await_suspend( c ) ), void> || std::same_as<decltype( t.await_suspend( c ) ), bool> ||
                          std::same_as<decltype( t.await_suspend( c ) ), std::coroutine_handle<>>;
                      { t.await_resume() };
                  };

template<typename T>
concept CAwaitable = requires( T t ) {
                        { t.operator co_await() } -> CAwaiter;
                    };

template<typename T>
concept CAwaiterVoid = requires( T t, std::coroutine_handle<> c ) {
                           { t.await_ready() } -> std::same_as<bool>;
                           std::same_as<decltype( t.await_suspend( c ) ), void> || std::same_as<decltype( t.await_suspend( c ) ), bool> ||
                               std::same_as<decltype( t.await_suspend( c ) ), std::coroutine_handle<>>;
                           { t.await_resume() } -> std::same_as<void>;
                       };


template<typename T>
concept CAwaitableVoid = requires( T t ) {
                             { t.operator co_await() } -> CAwaiterVoid;
                         };

template<CAwaitable TAwaitable, typename = void>
struct CAwaitableTraits {};

template<CAwaitable TAwaitable>
static auto GetAwaiter( TAwaitable&& value ) {
    return std::forward<TAwaitable>( value ).operator co_await();
}

template<CAwaitable TAwaitable>
struct CAwaitableTraits<TAwaitable> {
    using TAwaiter = decltype( GetAwaiter( std::declval<TAwaitable>() ) );
    using TAwaiterResult = decltype( std::declval<TAwaiter>().await_resume() );
};
}