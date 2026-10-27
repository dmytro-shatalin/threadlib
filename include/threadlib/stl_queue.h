//
// Created by dshatalin on 10/27/26.
//

#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <ranges>
#include <concepts>
#include <utility>
#include <cstddef>
#include <queue>

namespace threadlib {
    template<typename Tp>
    class queue {
        std::queue<Tp> q_;
        bool closed_{false};
        mutable std::mutex m_;
        mutable std::condition_variable cv_;

    public:
        queue() = default;

        queue(const queue &other) = delete;

        queue &operator=(const queue &other) = delete;

        auto try_pop() -> std::optional<Tp> {
            std::lock_guard lock{m_};
            if (q_.empty()) {
                return std::nullopt;
            }
            Tp v = std::move(q_.front());
            q_.pop();
            return v;
        }

        auto wait_pop() -> std::optional<Tp> {
            std::unique_lock lock{m_};
            cv_.wait(lock, [this] { return !q_.empty() || closed_; });
            if (q_.empty()) {
                return std::nullopt;
            }
            Tp v = std::move(q_.front());
            q_.pop();
            return v;
        }

        auto push(const Tp &value) -> bool {
            {
                std::lock_guard lock{m_};
                if (closed_) {
                    return false;
                }
                q_.push(value);
            }
            cv_.notify_one();
            return true;
        }

        auto push(Tp &&value) -> bool {
            {
                std::lock_guard lock{m_};
                if (closed_) {
                    return false;
                }
                q_.push(std::move(value));
            }
            cv_.notify_one();
            return true;
        }

        template<std::ranges::input_range R>
            requires std::convertible_to<std::ranges::range_reference_t<R>, Tp>
        auto push_range(R &&range) -> bool {
            {
                std::lock_guard lock{m_};
                if (closed_) {
                    return false;
                }
                q_.push_range(std::forward<R>(range));
            }
            cv_.notify_all();
            return true;
        }

        template<typename... Args>
        auto emplace(Args &&... args) -> bool {
            {
                std::lock_guard lock{m_};
                if (closed_) return false;
                q_.emplace(std::forward<Args>(args)...);
            }
            cv_.notify_one();
            return true;
        }

        auto size() const -> std::size_t {
            std::lock_guard lock{m_};
            return q_.size();
        }

        auto empty() const -> bool {
            std::lock_guard lock{m_};
            return q_.empty();
        }

        void stop() {
            {
                std::lock_guard lock{m_};
                closed_ = true;
            }
            cv_.notify_all();
        }

        auto is_closed() const -> bool {
            std::lock_guard lock{m_};
            return closed_;
        }
    };
}
