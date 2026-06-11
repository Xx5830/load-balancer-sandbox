#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "load-balancing.hpp"
#include "pick-policy.hpp"
#include "server.hpp"
#include "task.hpp"

namespace load_balancer {

struct NoPolicy : std::runtime_error {
    NoPolicy()
        : std::runtime_error("No pick policy has been set") {}
};

struct NoServerAvailable : std::runtime_error {
    NoServerAvailable()
        : std::runtime_error("No server available") {}
};

struct ServerManager {
   private:
    std::unique_ptr<LoadBalancer> balancer_;
    std::vector<ServerPtr> servers_;
    mutable std::shared_mutex servers_mtx_;

    size_t num_workers_;
    std::vector<std::thread> workers_;
    std::queue<TaskItem> task_queue_;
    std::mutex queue_mtx_;
    std::condition_variable queue_cv_;
    bool stop_ = false;

    void workerLoop() {
        while (true) {
            Task task;
            std::promise<Duration> prom;
            {
                std::unique_lock<std::mutex> lk(queue_mtx_);
                queue_cv_.wait(lk, [this] { return stop_ || !task_queue_.empty(); });

                if (stop_) {
                    while (!task_queue_.empty()) {
                        task_queue_.front().promise.set_exception(std::make_exception_ptr(NoServerAvailable{}));
                        task_queue_.pop();
                    }
                    return;
                }
                task = std::move(task_queue_.front().task);
                prom = std::move(task_queue_.front().promise);
                task_queue_.pop();
            }

            ServerPtr server;
            {
                std::shared_lock lock(servers_mtx_);
                if (!balancer_->hasPickPolicy()) {
                    prom.set_exception(std::make_exception_ptr(NoPolicy{}));
                    continue;
                }
                auto server_opt = balancer_->pickServer(item.task.getId(), servers_);
                if (!server_opt) {
                    item.promise.set_exception(std::make_exception_ptr(NoServerAvailable{}));
                    continue;
                }
                server = server_opt;
            }

            try {
                auto fut = server->submit(task);
                Duration result = fut.get();
                prom.set_value(result);
            } catch (...) {
                prom.set_exception(std::current_exception());
            }
        }
    }

   public:
    explicit ServerManager(std::unique_ptr<LoadBalancer> balancer, size_t num_workers = 0)
        : balancer_(std::move(balancer))
        , num_workers_(num_workers) {
        if (!balancer_) {
            balancer_ = std::make_unique<LoadBalancer>();
        }

        for (size_t index = 0; index < num_workers; index++) {
            workers_.emplace_back(&ServerManager::workerLoop, this);
        }
    }

    std::future<Duration> submitTask(const Task& task) {
        if (num_workers_ == 0) {
            std::shared_lock lock(servers_mtx_);
            if (!balancer_->hasPickPolicy()) {
                std::promise<Duration> prom;
                prom.set_exception(std::make_exception_ptr(NoPolicy{}));
                return prom.get_future();
            }

            auto server = balancer_->pickServer(task.getId(), servers_);

            if (!server_opt) {
                std::promise<Duration> prom;
                prom.set_exception(std::make_exception_ptr(NoServerAvailable{}));
                return prom.get_future();
            }

            return server_opt->submit(task);
        } else {
            std::promise<Duration> prom;
            auto fut = prom.get_future();
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                if (stop_) {
                    prom.set_exception(std::make_exception_ptr(NoServerAvailable{}));
                    return fut;
                }
                task_queue_.push({task, std::move(prom)});
            }
            queue_cv_.notify_one();
            return fut;
        }
    }

    template <typename PickPolicy, typename... Args>
        requires std::is_base_of_v<IPickPolicy, PickPolicy>
    void setPickPolicy(Args&&... args) {
        std::unique_lock lock(servers_mtx_);
        balancer_->setPickPolicy<PickPolicy>(std::forward<Args>(args)...);
        balancer_->resetServers(servers_);
    }

    bool hasPickPolicy() const {
        return balancer_->hasPickPolicy();
    }

    uint64_t addServer(uint32_t weight) {
        auto server = std::make_shared<Server>(weight, 1.0, 1);
        std::unique_lock lock(servers_mtx_);
        servers_.push_back(server);
        balancer_->addServerEvent(server, servers_);
        return server->getId();
    }

    void addServer(ServerPtr server) {
        std::unique_lock lock(servers_mtx_);
        servers_.push_back(server);
        balancer_->addServerEvent(server, servers_);
    }

    bool deleteServer(uint64_t id) {
        std::unique_lock lock(servers_mtx_);
        auto it = std::find_if(servers_.begin(), servers_.end(), [id](const ServerPtr& s) { return s->getId() == id; });

        if (it == servers_.end()) {
            return false;
        }

        ServerPtr removed = *it;
        servers_.erase(it);
        balancer_->eraseServerEvent(removed, servers_);
        return true;
    }

    std::vector<uint64_t> listServersIds() const {
        std::shared_lock lock(servers_mtx_);
        std::vector<uint64_t> ids;
        for (const auto& s : servers_) {
            ids.push_back(s->getId());
        }
        return ids;
    }

    std::vector<ServerPtr> servers() const {
        std::shared_lock lock(servers_mtx_);
        return servers_;
    }

    bool countServer(uint64_t id) const {
        std::shared_lock lock(servers_mtx_);
        return std::find_if(servers_.begin(), servers_.end(), [id](const ServerPtr& s) { return s->getId() == id; }) != servers_.end();
    }

    ~ServerManager() {
        if (num_workers_ > 0) {
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                stop_ = true;
            }
            queue_cv_.notify_all();
            for (auto& w : workers_) {
                if (w.joinable()) {
                    w.join();
                }
            }
        }
    }
};

}  // namespace load_balancer
