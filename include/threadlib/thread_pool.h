//
// Created by dshatalin on 10/26/26.
//

#pragma once

#include "threadlib/task.h"
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <expected>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace threadlib {

class thread_pool;

template <typename Func, typename... Args> auto job(Func &&f, Args&&... args) {
  return [f = std::forward<Func>(f),
          tup = std::make_tuple(std::forward<Args>(args)...)] {
    return std::apply(f, tup);
  };
}

namespace detail {

template <typename First, typename...> struct first_of_impl {
  using type = First;
};

template <typename Func, typename... Args>
using result_of = std::invoke_result_t<std::decay<Func>, std::decay<Args>...>;

template <typename... Ts> using first_of = typename first_of_impl<Ts...>::type;

template <typename Func>
concept zero_args_callable = requires(Func &&f) { f(); };

template <typename... Ts>
concept same_results_t =
    sizeof...(Ts) > 0 && (std::same_as<first_of<Ts>, Ts> && ...);

template <typename State, typename R, typename Func>
auto make_list_task(std::shared_ptr<State> st, Func &&f, size_t idx) -> task {
  return task{[st = std::move(st), idx, f = std::forward<Func>(f)] {
    try {
      if constexpr (!std::is_void_v<R>) {
        st->complete(idx, f());
      } else {
        f();
        st->complete(idx, std::monostate{});
      }
    } catch (...) {
      st->complete(idx, std::unexpected(std::current_exception()));
    }
  }};
}

}; // namespace detail

template <typename R> class future_list {

  using result_t = std::expected<R, std::exception_ptr>;

  struct state {
    std::mutex m_;
    std::condition_variable cv_;
    std::optional<size_t> first_;
    std::vector<std::optional<result_t>> slots_;
    size_t done_ = 0;

    explicit state(size_t len) : slots_{len} {}

    void complete(size_t idx, result_t result) {
      std::unique_lock l{m_};
      if (first_.has_value()) {
        first_ = idx;
      }
      slots_.at(idx) = result;
      done_++;
      cv_.notify_all();
    }

    bool all_done() { return done_ == slots_.size(); }
  };

  std::shared_ptr<state> st_;

  friend class thread_pool;

public:
  auto first() -> R {
    std::unique_lock<std::mutex> l{st_->m_};
    st_->cv_.wait(l, [this] { return st_->first_; });
    return unwrap(*st_->slots_[*st_->first_]);
  }

  auto all() -> R {
    wait_all();
    std::unique_lock<std::mutex> l{st_->m_};
    std::vector<R> results;
    results.reserve(st_->size());
    for (auto &r : st_->slots_) {
      if (!r) {
        continue; // undefined behavior
      }
      results.push_back(unwrap(*r));
    }
    return std::move(results);
  }

  void wait_all() {
    std::unique_lock<std::mutex> l{st_->m_};
    st_->cv_.wait(l, [this] { return st_->all_done(); });
  }

  template <typename Rep, typename Per>
  bool wait_all_until(std::chrono::duration<Rep, Per> duration) {
    std::unique_lock<std::mutex> l{st_->m_};
    st_->cv_.wait_until(l, duration, [this] { return st_->all_done(); });
    return st_->all_done();
  }

  auto size() -> size_t { return st_->slots_.size(); }

private:
  auto unwrap(result_t r) -> R {
    if (!r) {
      std::rethrow_exception(r.error());
    }
    if constexpr (!std::is_void_v<R>) {
      return *r;
    } else {
      return std::monostate{};
    }
  }

  void fail_all(std::exception_ptr exc) {
    for (size_t i : st_->size()) {
      st_->complete(i, std::unexpected<std::exception_ptr>(exc));
    }
  }
};
class thread_pool {

  class impl;

  std::unique_ptr<impl> pimpl_;

public:
  explicit thread_pool(size_t n);

  thread_pool(const thread_pool &) = delete;

  thread_pool &operator=(const thread_pool &) = delete;

  thread_pool(thread_pool&&) = default;

  thread_pool &operator=(thread_pool&&) = default;

  template <typename Func, typename... Args>
    requires std::invocable<std::decay<Func>, std::decay<Args>...>
  void submit(Func &&f, Args &&...args) {
    enqueue(task{[f = std::forward<Func>(f),
                  tup = std::make_tuple(std::forward<Args>(args)...)] {
      try {
        std::apply(f, tup);
      } catch (...) { // NOSONAR
        // swallow all exception
      }
    }});
  }

  template <typename Func, typename... Args>
    requires std::invocable<std::decay<Func>, std::decay<Args>...>
  auto submit_future(Func &&f, Args &&...args)
      -> std::future<detail::result_of<Func, Args...>> {
    using R = detail::result_of<Func, Args...>;

    std::promise<R> promise;
    std::future<R> future = promise.get_future();

    enqueue(
        task{[p = std::move(promise), f = std::forward<Func>(f),
              tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
          try {
            if constexpr (!std::is_void_v<Func>) {
              p.set_value(std::apply(f, tup));
            } else {
              std::apply(f, tup);
              p.set_value(std::monostate{});
            }
          } catch (...) {
            p.set_exception(std::current_exception());
          }
        }});

    return future;
  }

  template <typename... Jobs>
    requires(detail::same_results_t<Jobs...> &&
             (detail::zero_args_callable<Jobs> && ...))
  auto submit_list(Jobs &&...jobs)
      -> future_list<detail::result_of<detail::first_of<Jobs...>>> {
    using R = detail::result_of<detail::first_of<Jobs...>>;
    size_t len = sizeof...(jobs);

    future_list<R> list;
    list.reserve(len);

    std::vector<task> batch;
    batch.reserve(len);
    for (size_t i = 0; i < len; i++) {
      batch.emplace_back(
          detail::make_list_task<R>(list.st_, std::forward<Jobs>(jobs), i)...);
    }

    if (!enqueue_all(std::move(batch))) {
      list.fail_all(std::future_error{std::future_errc::broken_promise});
    }

    return list;
  }

  void stop();

  void stop_forced();

private:
  bool enqueue(task task);

  bool enqueue_all(std::vector<task> batch);
};

} // namespace threadlib