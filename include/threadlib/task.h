//
// Created by dshatalin on 10/26/26.
//

#pragma once

#include <concepts>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace threadlib {

class task {

  struct base {
    virtual ~base() = default;

    virtual void run(std::stop_token stop) = 0;
  };

  template <typename Func>
    requires std::invocable<Func>
  struct callable : base {
    Func f;

    explicit callable(Func &&f) : f{std::move<Func>(f)} {}

    void run(std::stop_token stop) override { f(stop); }
  };

  std::unique_ptr<base> ptr_;

  friend class thread_pool;

public:
  task() = delete;

  template <typename Func>
    requires(!std::same_as<std::decay<Func>, task>)
  explicit task(Func &&f)
      : ptr_{std::make_unique<callable<Func>>(std::forward<Func>(f))} {}

  task(const task &) = delete;

  task &operator=(const task &) = delete;

  task(task &&) = default;

  task &operator=(task &&) = default;

  void operator()(std::stop_token stop) {
    if (!ptr_) {
      return;
    }
    ptr_->run(stop);
  }
};

} // namespace threadlib
