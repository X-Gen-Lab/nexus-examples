/**
 * \file            network_gateway_main_test.cpp
 * \brief           Real process withdrawal and retained descriptor regressions
 * \author          Nexus Team
 * \version         1.0.0
 * \date            2026-11-03
 * \copyright       Copyright (c) 2026 Nexus Team
 */
#include <sys/socket.h>

#include "gateway_application.h"
#include "native/host.h"
#include "native/host_peer.h"
#include "native/native_runtime.h"
#include "nexus/tls/storage.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {
int gateway_fixture_join(pid_t child, int descriptor, int* observations) {
    example_gateway_host_t state{};
    state.peer = child;
    state.control_descriptor = descriptor;
    const auto result = example_gateway_host_join(&state);
    observations[0] = state.peer_joined;
    observations[1] = state.peer_report_valid;
    observations[2] = state.peer_report.local_resources_released;
    return static_cast<int>(result);
}

int gateway_fixture_run(int argc, char** argv, int* observations) {
    static example_gateway_host_t state;
    const int result = example_gateway_host_run(&state, argc, argv);
    observations[0] = state.frame_descriptor;
    observations[1] = state.frames.descriptor;
    observations[2] = state.network_live;
    observations[3] = fcntl(state.frames.descriptor, F_GETFD) >= 0;
    return result;
}

int gateway_fixture_start_peer(int* observations) {
    example_gateway_host_t state{};
    state.frame_descriptor = state.control_descriptor = -1;
    const auto started = example_gateway_host_start_peer(&state, 1U);
    if (started != NX_SUCCESS) {
        return static_cast<int>(started);
    }
    struct sigaction action {};
    observations[0] = sigaction(SIGCHLD, nullptr, &action) == 0;
    observations[1] = action.sa_handler == SIG_DFL;
    observations[2] = (action.sa_flags & SA_NOCLDWAIT) == 0;
    const auto result = example_gateway_host_join(&state);
    observations[3] = state.peer_joined;
    observations[4] = state.peer_report_valid;
    observations[5] = state.peer_report.local_resources_released;
    (void)close(state.frame_descriptor);
    (void)close(state.control_descriptor);
    return static_cast<int>(result);
}

int gateway_fixture_job(int selected) {
    example_gateway_host_t state{};
    state.selected = static_cast<gateway_case_t>(selected);
    return static_cast<int>(example_gateway_host_run_job(&state));
}
pid_t watched_child = -1;
unsigned watched_kills = 0U;
pid_t retained_adapter_process = -1;
unsigned adapter_stop_calls = 0U;
bool observe_signals_only = false;
unsigned observed_signal_calls = 0U;
bool observe_job_config = false;
unsigned observed_jobs = 0U;
example_gateway_policy_config_t supplied_policy{};
nx_time_us_t supplied_cleanup_budget = 0U;

void tick() {
    const timespec delay{0, 1000000};
    (void)nanosleep(&delay, nullptr);
}

void acknowledge_stop(int descriptor, int exit_status) {
    const auto deadline = example_gateway_host_now(nullptr) + 2000000U;
    while (example_gateway_host_now(nullptr) < deadline) {
        uint8_t command = 0U;
        if (recv(descriptor, &command, 1U, MSG_DONTWAIT) == 1) {
            if (command != EXAMPLE_GATEWAY_PEER_STOP) {
                _exit(91);
            }
            const uint8_t acknowledgement = command | 0x80U;
            example_gateway_peer_report_t report{};
            report.local_resources_released = true;
            if (send(descriptor, &acknowledgement, 1U, MSG_NOSIGNAL) != 1 ||
                send(descriptor, &report, sizeof(report), MSG_NOSIGNAL) !=
                    static_cast<ssize_t>(sizeof(report))) {
                _exit(92);
            }
            _exit(exit_status);
        }
        tick();
    }
    _exit(93);
}

void check_join(int child_status, nx_result_t expected, bool joined) {
    std::array<int, 2> controls{};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                         0, controls.data()),
              0);
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)close(controls[0]);
        acknowledge_stop(controls[1], child_status);
    }
    (void)close(controls[1]);
    watched_child = child;
    watched_kills = 0U;
    std::array<int, 3> observations{};
    EXPECT_EQ(gateway_fixture_join(child, controls[0], observations.data()),
              expected);
    EXPECT_EQ(observations[0], joined);
    EXPECT_EQ(observations[1], 1);
    EXPECT_EQ(observations[2], 1);
    EXPECT_EQ(watched_kills, 0U);
    int status = 0;
    errno = 0;
    EXPECT_EQ(waitpid(child, &status, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
    watched_child = -1;
    (void)close(controls[0]);
}

using IsolatedObservations = std::array<int, 8>;

bool inherited_auto_reap() {
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    action.sa_flags = SA_NOCLDWAIT;
    return sigemptyset(&action.sa_mask) == 0 &&
           sigaction(SIGCHLD, &action, nullptr) == 0;
}

IsolatedObservations isolated(void (*operation)(int)) {
    std::array<int, 2> channel{};
    if (pipe(channel.data()) != 0) {
        return {};
    }
    const pid_t process = fork();
    if (process == 0) {
        (void)close(channel[0]);
        operation(channel[1]);
        _exit(95);
    }
    (void)close(channel[1]);
    IsolatedObservations result{};
    EXPECT_GT(process, 0);
    if (process > 0) {
        EXPECT_EQ(read(channel[0], result.data(), sizeof(result)),
                  static_cast<ssize_t>(sizeof(result)));
        int status = 0;
        EXPECT_EQ(waitpid(process, &status, 0), process);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
    (void)close(channel[0]);
    return result;
}

void auto_reaped_join(int descriptor) {
    if (!inherited_auto_reap()) {
        _exit(96);
    }
    std::array<int, 2> controls{};
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0,
                   controls.data()) != 0) {
        _exit(97);
    }
    const pid_t child = fork();
    if (child == 0) {
        _exit(7);
    }
    if (child < 0) {
        _exit(98);
    }
    (void)close(controls[1]);
    watched_child = child;
    watched_kills = 0U;
    observed_signal_calls = 0U;
    observe_signals_only = true;
    IsolatedObservations observed{};
    const auto deadline = example_gateway_host_now(nullptr) + 2000000U;
    while (example_gateway_host_now(nullptr) < deadline) {
        int status = 0;
        if (waitpid(child, &status, WNOHANG) < 0 && errno == ECHILD) {
            observed[0] = 1;
            break;
        }
        tick();
    }
    std::array<int, 3> joined{};
    observed[1] = gateway_fixture_join(child, controls[0], joined.data());
    observed[2] = joined[0];
    observed[3] = static_cast<int>(watched_kills);
    observed[4] = static_cast<int>(observed_signal_calls);
    const auto copied = write(descriptor, observed.data(), sizeof(observed));
    _exit(copied == static_cast<ssize_t>(sizeof(observed)) ? 0 : 99);
}

void establish_child_policy(int descriptor) {
    if (!inherited_auto_reap()) {
        _exit(96);
    }
    observed_signal_calls = 0U;
    observe_signals_only = true;
    IsolatedObservations observed{};
    observed[6] = gateway_fixture_start_peer(observed.data());
    observed[7] = static_cast<int>(observed_signal_calls);
    const auto copied = write(descriptor, observed.data(), sizeof(observed));
    _exit(copied == static_cast<ssize_t>(sizeof(observed)) ? 0 : 99);
}

TEST(GatewayHostMain, AutoReapedChildIsNeverSignaledAfterEchild) {
    const auto observed = isolated(auto_reaped_join);
    EXPECT_EQ(observed[0], 1);
    EXPECT_EQ(observed[1], NX_ERROR_IO);
    EXPECT_EQ(observed[2], 0);
    EXPECT_EQ(observed[3], 0);
    EXPECT_EQ(observed[4], 0);
}

TEST(GatewayHostMain, PeerStartEstablishesWaitableChildPolicyBeforeFork) {
    const auto observed = isolated(establish_child_policy);
    EXPECT_EQ(observed[0], 1);
    EXPECT_EQ(observed[1], 1);
    EXPECT_EQ(observed[2], 1);
    EXPECT_EQ(observed[3], 1);
    EXPECT_EQ(observed[4], 1);
    EXPECT_EQ(observed[5], 1);
    EXPECT_EQ(observed[6], NX_SUCCESS);
    EXPECT_EQ(observed[7], 0);
}

TEST(GatewayHostMain, NativeCasesSupplyTheirFiniteAttemptAndWholeJobBudgets) {
    const std::array<nx_time_us_t, 5> attempts{2500000U, 4500000U, 2500000U,
                                               900000U, 2500000U};
    observe_job_config = true;
    observed_jobs = 0U;
    for (size_t selected = 0U; selected < attempts.size(); ++selected) {
        SCOPED_TRACE(selected);
        const auto before = example_gateway_host_now(nullptr);
        EXPECT_EQ(gateway_fixture_job(static_cast<int>(selected)), NX_ERROR_IO);
        const auto after = example_gateway_host_now(nullptr);
        const nx_time_us_t whole_job = selected == 3U ? 1200000U : 5000000U;
        EXPECT_EQ(supplied_policy.attempt_budget_us, attempts[selected]);
        EXPECT_GE(supplied_policy.overall_deadline, before + whole_job);
        EXPECT_LE(supplied_policy.overall_deadline, after + whole_job);
        EXPECT_EQ(supplied_policy.retry_delay_us, 10000U);
        EXPECT_EQ(supplied_policy.attempts_limit, selected == 4U ? 1U : 2U);
        EXPECT_EQ(supplied_cleanup_budget, 750000U);
    }
    EXPECT_EQ(observed_jobs, attempts.size());
    observe_job_config = false;
}

TEST(GatewayHostMain, ReapedNonzeroChildIsNeverSignaledAgain) {
    check_join(7, NX_ERROR_IO, false);
}

TEST(GatewayHostMain, ReapedSuccessfulChildHasCopiedReleaseReport) {
    check_join(0, NX_SUCCESS, true);
}

TEST(GatewayHostMain, UnresponsiveChildIsWithdrawnAndFailureRemainsVisible) {
    std::array<int, 2> controls{};
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                         0, controls.data()),
              0);
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)close(controls[0]);
        for (;;) {
            pause();
        }
    }
    (void)close(controls[1]);
    watched_child = child;
    watched_kills = 0U;
    std::array<int, 3> observations{};
    const auto started = example_gateway_host_now(nullptr);
    EXPECT_EQ(gateway_fixture_join(child, controls[0], observations.data()),
              NX_ERROR_IO);
    EXPECT_LT(example_gateway_host_now(nullptr) - started, 1500000U);
    EXPECT_EQ(observations[0], 0);
    EXPECT_EQ(observations[1], 0);
    EXPECT_EQ(watched_kills, 1U);
    watched_child = -1;
    (void)close(controls[0]);
}

TEST(GatewayHostMain, FailedAdapterWithdrawalKeepsBorrowedFrameFdUntilExit) {
    std::array<char, 40> directory{};
    const char pattern[] = "/dev/shm/gateway-main-XXXXXX";
    std::copy(std::begin(pattern), std::end(pattern), directory.begin());
    ASSERT_NE(mkdtemp(directory.data()), nullptr);
    for (const char* name : {"ca.der", "client.der", "client.key.der"}) {
        const std::string path = std::string(directory.data()) + "/" + name;
        const int descriptor =
            open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        ASSERT_GE(descriptor, 0);
        const uint8_t unused_der = 1U;
        ASSERT_EQ(write(descriptor, &unused_der, 1U), 1);
        ASSERT_EQ(close(descriptor), 0);
    }
    std::array<int, 2> channel{};
    ASSERT_EQ(pipe(channel.data()), 0);
    const pid_t process = fork();
    ASSERT_GE(process, 0);
    if (process == 0) {
        (void)close(channel[0]);
        retained_adapter_process = getpid();
        char name[] = "network_gateway";
        char port[] = "1";
        char selected[] = "clean";
        std::array<char*, 4> arguments{name, port, selected, directory.data()};
        std::array<int, 6> observed{};
        observed[4] = gateway_fixture_run(4, arguments.data(), observed.data());
        observed[5] = static_cast<int>(adapter_stop_calls);
        const ssize_t copied =
            write(channel[1], observed.data(), sizeof(observed));
        _exit(copied == static_cast<ssize_t>(sizeof(observed)) ? 0 : 94);
    }
    (void)close(channel[1]);
    std::array<int, 6> observed{};
    EXPECT_EQ(read(channel[0], observed.data(), sizeof(observed)),
              static_cast<ssize_t>(sizeof(observed)));
    int status = 0;
    EXPECT_EQ(waitpid(process, &status, 0), process);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_GE(observed[0], 0);
    EXPECT_EQ(observed[0], observed[1]);
    EXPECT_EQ(observed[2], 1);
    EXPECT_EQ(observed[3], 1);
    EXPECT_EQ(observed[4], 1);
    EXPECT_EQ(observed[5], 1);
    (void)close(channel[0]);
    for (const char* name : {"ca.der", "client.der", "client.key.der"}) {
        const std::string path = std::string(directory.data()) + "/" + name;
        EXPECT_EQ(unlink(path.c_str()), 0);
    }
    EXPECT_EQ(rmdir(directory.data()), 0);
}
} /* namespace */

extern "C" int __real_kill(pid_t process, int signal);
extern "C" int __wrap_kill(pid_t process, int signal) {
    if (process == watched_child) {
        ++watched_kills;
    }
    if (observe_signals_only) {
        ++observed_signal_calls;
        errno = ESRCH;
        return -1;
    }
    return __real_kill(process, signal);
}

extern "C" nx_result_t
__real_example_gateway_run(example_gateway_application_t* application,
                           const example_gateway_config_t* config,
                           example_gateway_report_t* report);
extern "C" nx_result_t
__wrap_example_gateway_run(example_gateway_application_t* application,
                           const example_gateway_config_t* config,
                           example_gateway_report_t* report) {
    if (observe_job_config) {
        ++observed_jobs;
        supplied_policy = config->policy;
        supplied_cleanup_budget = config->cleanup_budget_us;
        return NX_ERROR_IO;
    }
    return __real_example_gateway_run(application, config, report);
}

extern "C" nx_result_t
__real_nx_tls_runtime_start(nx_tls_runtime_t* runtime,
                            const nx_tls_runtime_config_t* config,
                            nx_time_us_t deadline);
extern "C" nx_result_t
__wrap_nx_tls_runtime_start(nx_tls_runtime_t* runtime,
                            const nx_tls_runtime_config_t* config,
                            nx_time_us_t deadline) {
    if (getpid() == retained_adapter_process) {
        return NX_ERROR_IO;
    }
    return __real_nx_tls_runtime_start(runtime, config, deadline);
}

extern "C" nx_result_t __real_nx_lwip_adapter_stop(nx_lwip_adapter_t* adapter);
extern "C" nx_result_t __wrap_nx_lwip_adapter_stop(nx_lwip_adapter_t* adapter) {
    if (getpid() == retained_adapter_process) {
        ++adapter_stop_calls;
        return NX_ERROR_BUSY;
    }
    return __real_nx_lwip_adapter_stop(adapter);
}
