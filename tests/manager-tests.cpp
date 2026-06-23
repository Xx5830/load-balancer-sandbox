#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "load-balancing.hpp"
#include "manager.hpp"
#include "pick-policy.hpp"
#include "server.hpp"
#include "task.hpp"

using load_balancer::Duration;
using load_balancer::LoadBalancer;
using load_balancer::NoPolicy;
using load_balancer::NoServerAvailable;
using load_balancer::RoundRobinPick;
using load_balancer::Server;
using load_balancer::ServerManager;
using load_balancer::ServerPtr;
using load_balancer::Task;

namespace {
std::shared_ptr<ServerManager> makeManager() {
    return std::make_shared<ServerManager>(std::make_unique<LoadBalancer>(), 0);
}
}  // namespace

// addServer регистрирует сервер и возвращает его id.
TEST(ServerManager, AddServerRegistersId) {
    auto m = makeManager();
    uint64_t id = m->addServer(2);
    EXPECT_TRUE(m->countServer(id));
    auto ids = m->listServersIds();
    EXPECT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], id);
}

// deleteServer удаляет существующий и возвращает false для отсутствующего.
TEST(ServerManager, DeleteServer) {
    auto m = makeManager();
    uint64_t id = m->addServer(1);
    EXPECT_TRUE(m->deleteServer(id));
    EXPECT_FALSE(m->countServer(id));
    EXPECT_FALSE(m->deleteServer(id));
}

// Без политики submitTask завершается исключением NoPolicy.
TEST(ServerManager, SubmitWithoutPolicyThrowsNoPolicy) {
    auto m = makeManager();
    m->addServer(1);
    auto fut = m->submitTask(Task(0, 1.0L));
    EXPECT_THROW(fut.get(), NoPolicy);
}

// С политикой, но без серверов submitTask завершается NoServerAvailable.
TEST(ServerManager, SubmitWithoutServersThrows) {
    auto m = makeManager();
    m->setPickPolicy<RoundRobinPick>();
    auto fut = m->submitTask(Task(0, 1.0L));
    EXPECT_THROW(fut.get(), NoServerAvailable);
}

// С политикой и сервером задача выполняется успешно.
TEST(ServerManager, SubmitSucceeds) {
    auto m = makeManager();
    m->setPickPolicy<RoundRobinPick>();
    m->addServer(1);
    auto fut = m->submitTask(Task(0, 1.0L));
    EXPECT_NO_THROW(fut.get());
}

// listServersIds отражает все добавленные серверы.
TEST(ServerManager, ListReflectsAllServers) {
    auto m = makeManager();
    m->addServer(1);
    m->addServer(1);
    m->addServer(1);
    EXPECT_EQ(m->listServersIds().size(), 3u);
    EXPECT_EQ(m->servers().size(), 3u);
}