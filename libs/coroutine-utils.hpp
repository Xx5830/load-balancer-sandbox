#pragma once

#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <future>
#include <optional>

template <typename T>
struct FutureAwaitable {
   private:
    std::future<T> fut_;

   public:
    // NOLINTNEXTLINE
    bool await_ready() const noexcept {
        return fut_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    // NOLINTNEXTLINE
    void await_suspend(std::coroutine_handle<> hand) {}

    // NOLINTNEXTLINE
    T await_resume() {
        return fut_.get();
    }

   private:
    asio::awaitable<void> pull(std::coroutine_handle<> hand) {
        auto executer = co_await asio::this_coro::executor;
        asio::steady_timer timer(executer);
        while (fut_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            timer.expires_after(std::chrono::microseconds(100));
            co_await timer.async_wait(asio::use_awaitable);
        }

        hand.resume();
    }
};