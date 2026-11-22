//
// Created by dshatalin on 10/26/26.
//

#include "threadlib/task.h"
#include "threadlib/thread_pool.h"

#include <functional>
#include <iostream>
#include <print>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

int main() {
  using namespace std::chrono_literals;
  threadlib::thread_pool pool(4);

  std::println("initialized thread_pool");

  pool.submit(
      [](std::stop_token, int id, const std::string &msg) {
        std::cout << "job " << id << ": " << msg << "\n";
      },
      1, "hello from worker");

  std::println("submitted fire & forget");

  auto sum = pool.submit_future(
      [](std::stop_token, int a, int b) {
        std::this_thread::sleep_for(100ms);
        return a + b;
      },
      20, 22);

  std::println("sum = {}", sum.get());

  auto list = pool.submit_list([](std::stop_token) { return 10; },
                               [](std::stop_token) { return 20; },
                               [](std::stop_token) { return 30; });

  std::println("first done = {}", list.first());

  list.wait_all();

  pool.stop();

  threadlib::thread_pool span_pool(3);

  std::vector<std::function<void(std::stop_token)>> jobs;
  jobs.emplace_back([](std::stop_token) { std::println("T1: hello"); });
  jobs.emplace_back([](std::stop_token) { std::println("T2: hello"); });

  auto job_list = span_pool.submit_list(jobs);

  job_list.wait_all();
}
