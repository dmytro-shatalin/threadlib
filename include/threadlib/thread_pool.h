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
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace threadlib {

class thread_pool;

template <typename Func, typename... Args> auto job(Func &&f, Args &&...args) {
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
using result_of =
    std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>;

template <typename Func>
using stoppable_result_of =
    std::invoke_result_t<std::decay_t<Func>, std::stop_token>;

template <typename... Ts> using first_of = typename first_of_impl<Ts...>::type;

template <typename Func>
concept stoppable_callable = std::invocable<Func, std::stop_token>;

template <typename... Ts>
concept same_stoppable_results =
    sizeof...(Ts) > 0 && (std::same_as<stoppable_result_of<first_of<Ts...>>,
                                       stoppable_result_of<Ts>> &&
                          ...);

template <typename R, typename State, typename Func>
auto make_list_task(std::shared_ptr<State> st, Func &&f, size_t idx) -> task {
  return task{[st = std::move(st), idx,
               f = std::forward<Func>(f)](std::stop_token stop) mutable {
    try {
      if constexpr (!std::is_void_v<R>) {
        st->complete(idx, f(stop));
      } else {
        f(stop);
        st->complete(idx, std::expected<R, std::exception_ptr>{});
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

    explicit state(size_t len) : slots_(len) {}

    void complete(size_t idx, result_t result) {
      std::unique_lock l{m_};
      if (slots_.at(idx).has_value()) {
        return;
      }
      if (!first_.has_value()) {
        first_ = idx;
      }
      slots_[idx] = std::move(result);
      done_++;
      cv_.notify_all();
    }

    bool all_done() { return done_ == slots_.size(); }
  };

  std::shared_ptr<state> st_;

  friend class thread_pool;

public:
  future_list() = default;

  auto first() -> R {
    std::unique_lock<std::mutex> l{st_->m_};
    st_->cv_.wait(l, [this] { return st_->first_; });
    return unwrap(*st_->slots_[*st_->first_]);
  }

  auto all() {
    wait_all();
    std::unique_lock<std::mutex> l{st_->m_};
    if constexpr (std::is_void_v<R>) {
      for (auto &r : st_->slots_) {
        if (r) {
          unwrap(*r);
        }
      }
    } else {
      std::vector<R> results;
      results.reserve(st_->slots_.size());
      for (auto &r : st_->slots_) {
        if (r) {
          results.push_back(unwrap(*r));
        }
      }
      return results;
    }
  }

  void wait_all() {
    std::unique_lock<std::mutex> l{st_->m_};
    st_->cv_.wait(l, [this] { return st_->all_done(); });
  }

  template <typename Rep, typename Per>
  bool wait_all_for(std::chrono::duration<Rep, Per> duration) {
    std::unique_lock<std::mutex> l{st_->m_};
    return st_->cv_.wait_for(l, duration, [this] { return st_->all_done(); });
  }

  auto size() const -> size_t { return st_ ? st_->slots_.size() : 0; }

private:
  auto unwrap(result_t &r) -> R {
    if (!r) {
      std::rethrow_exception(r.error());
    }
    if constexpr (!std::is_void_v<R>) {
      return std::move(*r);
    }
  }

  void fail_all(std::exception_ptr exc) {
    if (!st_) {
      return;
    }
    for (size_t i = 0; i < st_->slots_.size(); ++i) {
      st_->complete(i, std::unexpected(exc));
    }
  }
};

class thread_pool {
  class impl;

  std::unique_ptr<impl> pimpl_;

public:
  explicit thread_pool(size_t n);

  ~thread_pool() noexcept;

  thread_pool(const thread_pool &) = delete;

  thread_pool &operator=(const thread_pool &) = delete;

  thread_pool(thread_pool &&) noexcept;

  thread_pool &operator=(thread_pool &&) noexcept;

  template <typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, std::stop_token,
                            std::decay_t<Args>...>
  void submit(Func &&f, Args &&...args) {
    enqueue(task{[f = std::forward<Func>(f),
                  tup = std::make_tuple(std::forward<Args>(args)...)](
                     std::stop_token stop) mutable {
      try {
        std::apply(std::move(f),
                   std::tuple_cat(std::forward_as_tuple(stop), std::move(tup)));
      } catch (...) { // NOSONAR
        // swallow all exception
      }
    }});
  }

  template <typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, std::stop_token,
                            std::decay_t<Args>...>
  auto submit_future(Func &&f, Args &&...args)
      -> std::future<std::invoke_result_t<std::decay_t<Func>, std::stop_token,
                                          std::decay_t<Args>...>> {
    using R = std::invoke_result_t<std::decay_t<Func>, std::stop_token,
                                   std::decay_t<Args>...>;

    std::promise<R> promise;
    std::future<R> future = promise.get_future();

    enqueue(task{[p = std::move(promise), f = std::forward<Func>(f),
                  tup = std::make_tuple(std::forward<Args>(args)...)](
                     std::stop_token stop) mutable {
      try {
        if constexpr (std::is_void_v<R>) {
          std::apply(std::move(f), std::tuple_cat(std::forward_as_tuple(stop),
                                                  std::move(tup)));
          p.set_value();
        } else {
          p.set_value(std::apply(
              std::move(f),
              std::tuple_cat(std::forward_as_tuple(stop), std::move(tup))));
        }
      } catch (...) {
        p.set_exception(std::current_exception());
      }
    }});

    return future;
  }

  template <typename... Jobs>
    requires(detail::same_stoppable_results<Jobs...> &&
             (detail::stoppable_callable<Jobs> && ...))
  auto submit_list(Jobs &&...jobs)
      -> future_list<detail::stoppable_result_of<detail::first_of<Jobs...>>> {
    using R = detail::stoppable_result_of<detail::first_of<Jobs...>>;
    size_t len = sizeof...(jobs);

    future_list<R> list;
    list.st_ = std::make_shared<typename future_list<R>::state>(len);

    std::vector<task> batch;
    batch.reserve(len);

    size_t i = 0;
    (batch.emplace_back(
         detail::make_list_task<R>(list.st_, std::forward<Jobs>(jobs), i++)),
     ...);

    if (!enqueue_all(std::move(batch))) {
      list.fail_all(std::make_exception_ptr(
          std::future_error{std::future_errc::broken_promise}));
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
