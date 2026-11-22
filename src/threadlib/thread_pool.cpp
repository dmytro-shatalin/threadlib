#include "threadlib/thread_pool.h"
#include "threadlib/stl_queue.h"
#include "threadlib/task.h"
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

using namespace threadlib;

void run_worker(std::stop_token stop, queue<task> &tasks) {
  while (!stop.stop_requested()) {
    auto task = tasks.pop_wait([&stop] {
      return stop.stop_requested();
    });
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

  bool enqueue(task task) { return tasks_.push(std::move(task)); }

  bool enqueue_all(std::vector<task> batch) {
    for (auto &t : batch) {
      if (!tasks_.push(std::move(t))) {
        return false;
      }
    }
    return true;
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

thread_pool::thread_pool(size_t n) : pimpl_(std::make_unique<impl>(n)) {}

thread_pool::~thread_pool() noexcept = default;

thread_pool::thread_pool(thread_pool &&) noexcept = default;

thread_pool &thread_pool::operator=(thread_pool &&) noexcept = default;

void thread_pool::stop() { pimpl_->stop(); }

void thread_pool::stop_forced() { pimpl_->stop_forced(); }

bool thread_pool::enqueue(task task) {
  return pimpl_->enqueue(std::move(task));
}

bool thread_pool::enqueue_all(std::vector<task> batch) {
  return pimpl_->enqueue_all(std::move(batch));
}
