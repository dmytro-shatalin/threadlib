#include "threadlib/thread_pool.h"
#include "threadlib/stl_queue.h"
#include "threadlib/task.h"
#include <algorithm>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

using namespace threadlib;

void run_worker(std::stop_token stop, queue<task> &tasks) {
  while (!tasks.is_closed()) {
    auto task = tasks.pop_wait([&tasks] { return !tasks.is_closed(); });
    if (!task) {
      return;
    }
    (*task)(stop);
  }
}

class thread_pool::impl {
  queue<task> tasks_;
  std::vector<std::jthread> workers_;

public:
  explicit impl(size_t n) {
    if (n < 1) {
      throw std::invalid_argument{"worker count cannot be < 1"};
    }
    workers_.reserve(n);
    try {
      for (int i = 0; i < n; i++) {
        std::jthread t(
            [this](std::stop_token stop) { run_worker(stop, tasks_); });
        workers_.push_back(std::move(t));
      }
    } catch (...) {
      stop();
      throw;
    }
  }

  ~impl() { stop(); }

  impl(const impl &other) = delete;

  impl &operator=(const impl &other) = delete;

  impl(impl &&other) = delete;

  impl &operator=(impl &&other) = delete;

  void enqueue(task task) { tasks_.push(std::move(task)); }

  bool enqueue_all(std::vector<task> const &batch) {
    return std::ranges::all_of(
        batch, [this](const task &task) { return tasks_.push(task); });
  }

  void stop() { tasks_.stop(); }

  void stop_forced() {
    tasks_.stop();
    tasks_.clear();
    for (auto &w : workers_) {
      w.request_stop();
    }
  }
};
