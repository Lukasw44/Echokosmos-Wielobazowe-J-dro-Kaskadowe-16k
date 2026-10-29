// ──────────────────────────────────────────────────────────────────────────────────
// amsin_quantum_16k.cpp — Wielobazowe Jądro Kaskadowe (16 Kubitów / Core P3 v4.05)
// Licencja: CC BY-NC-ND 4.0 | Łukasz Włodarski | System Labs (Koszalin, 2026)
// ──────────────────────────────────────────────────────────────────────────────────

#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <iostream>
#include <cmath>
#include <cstdint>
#include <immintrin.h>
#include <atomic>
#include <chrono>
#include <omp.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include <intrin.h>
#include <vector>
#include <thread>
#include <mutex>
#include <string>

namespace py = pybind11;

constexpr double DELTA_PHASE_SLIP = 1.618e-12;
constexpr double L_W              = 1.0;
constexpr double R_S              = 1.0e-9;
constexpr double SYSTEM_GAIN      = 1.0e15;
constexpr double PHI_44_Z         = 44.0;
constexpr int    PORT_P3          = 1495;

struct SystemConstants {
    static constexpr double delta = DELTA_PHASE_SLIP;
    static constexpr double Lw    = L_W;
    static constexpr double Rs    = R_S;
    static constexpr double Phi44 = PHI_44_Z;
    static constexpr double Gain  = SYSTEM_GAIN;
};

constexpr uint64_t BAZA_A0    = 585;
constexpr uint64_t GTS_KROK   = 91;

std::atomic<uint64_t> g_ml_exhaust_triggers{0};
std::atomic<uint64_t> g_zibi_activations{0};
std::atomic<double> g_global_p3_coherence{0.0};

std::atomic<double> g_state_pop_pure_zero{0.0};
std::atomic<double> g_state_pop_full_one{0.0};

std::thread g_telemetry_listener;
std::atomic<bool> g_listener_running{false};
#ifdef _WIN32
SOCKET g_server_socket = INVALID_SOCKET;
std::vector<SOCKET> g_clients;
std::mutex g_clients_mutex;
#endif

static inline uint64_t calculate_pure_s_value(uint64_t val) {
    uint64_t t = val;
    uint64_t s = 0;
    t = __umulh(t, 0xCCCCCCCDULL); s += (val - t * 10); val = t;
    t = __umulh(val, 0xCCCCCCCDULL); s += (val - t * 10); val = t;
    t = __umulh(val, 0xCCCCCCCDULL); s += (val - t * 10); val = t;
    t = __umulh(val, 0xCCCCCCCDULL); s += (val - t * 10);
    return s;
}

static inline uint64_t execute_p3_takt_16k(uint64_t index_n, uint64_t& last_b) {
    uint64_t a_n = BAZA_A0 + GTS_KROK * index_n;
    uint64_t s_last_b = calculate_pure_s_value(last_b);

    if (s_last_b >= 22ULL) {
        g_ml_exhaust_triggers.fetch_add(1, std::memory_order_relaxed);
        int64_t b_next = static_cast<int64_t>(last_b) + (91LL * (static_cast<int64_t>(s_last_b) - 17LL)) + 4418LL - 315LL;
        b_next = b_next % 1170LL;
        if (b_next < 0) b_next += 1170LL;
        last_b = static_cast<uint64_t>(b_next);
    } else {
        last_b = (last_b + 91ULL * s_last_b + 4418ULL) % 1170ULL;
    }

    double raw_correction = (SystemConstants::delta * SystemConstants::delta * SystemConstants::Lw) / SystemConstants::Rs;
    int64_t scaled_impulse = static_cast<int64_t>(raw_correction * SystemConstants::Gain) % 13LL;

    int64_t gik_delta = static_cast<int64_t>(last_b) - static_cast<int64_t>(a_n);
    uint64_t gik_n = std::abs(gik_delta) % 585ULL;

    if (gik_n > 44ULL) {
        g_zibi_activations.fetch_add(1, std::memory_order_relaxed);
    }

    return (last_b + scaled_impulse) ^ static_cast<uint64_t>(SystemConstants::Phi44);
}

uint64_t execute_quantum_16k_simulation(int64_t total_loops) {
    py::gil_scoped_release release;

    g_ml_exhaust_triggers.store(0, std::memory_order_relaxed);
    g_zibi_activations.store(0, std::memory_order_relaxed);
    g_global_p3_coherence.store(0.0, std::memory_order_relaxed);
    g_state_pop_pure_zero.store(0.0, std::memory_order_relaxed);
    g_state_pop_full_one.store(0.0, std::memory_order_relaxed);

    double local_p3_coh_sum = 0.0;
    double local_pop_zero = 0.0;
    double local_pop_one = 0.0;

    omp_set_num_threads(4);

    #pragma omp parallel reduction(+:local_p3_coh_sum) reduction(+:local_pop_zero) reduction(+:local_pop_one)
    {
        uint64_t local_last_b = BAZA_A0;

        #pragma omp for schedule(guided, 262144)
        for (int64_t i = 0; i < total_loops; ++i) {
            uint64_t state_seed = execute_p3_takt_16k(static_cast<uint64_t>(i), local_last_b);

            uint64_t q_mask = (state_seed ^ (state_seed >> 16) ^ (state_seed >> 32) ^ (state_seed >> 48)) & 0xFFFFULL;

            uint64_t control_bit = q_mask & 1ULL;
            uint64_t cascade_state = control_bit;

            for (int k = 1; k < 16; ++k) {
                uint64_t target_bit = (q_mask >> k) & 1ULL;
                control_bit = target_bit ^ control_bit;
                cascade_state |= (control_bit << k);
            }

            double theta_16k = (static_cast<double>(cascade_state) / 65535.0) * 3.14159265358979323846;
            local_p3_coh_sum += std::cos(theta_16k);

            int64_t mask_zero = (cascade_state == 0x0000ULL) ? 1LL : 0LL;
            int64_t mask_one  = (cascade_state == 0xFFFFULL) ? 1LL : 0LL;

            local_pop_zero += static_cast<double>(mask_zero);
            local_pop_one  += static_cast<double>(mask_one);
        }
    }

    g_global_p3_coherence.store(local_p3_coh_sum, std::memory_order_relaxed);
    g_state_pop_pure_zero.store(local_pop_zero, std::memory_order_relaxed);
    g_state_pop_full_one.store(local_pop_one, std::memory_order_relaxed);

    return static_cast<uint64_t>(total_loops);
}

void start_telemetry_listener() {
    if (g_listener_running.load()) return;
    g_listener_running.store(true);
    g_telemetry_listener = std::thread([]() {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        g_server_socket = socket(AF_INET, SOCK_STREAM, 0);
        
        char opt = 1;
        setsockopt(g_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT_P3);
        addr.sin_addr.s_addr = INADDR_ANY;
        
        bind(g_server_socket, (sockaddr*)&addr, sizeof(addr));
        listen(g_server_socket, 5);

        u_long mode = 1;
        ioctlsocket(g_server_socket, FIONBIO, &mode);

        while (g_listener_running.load()) {
            SOCKET client = accept(g_server_socket, nullptr, nullptr);
            if (client != INVALID_SOCKET) {
                std::lock_guard<std::mutex> lock(g_clients_mutex);
                g_clients.push_back(client);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        std::lock_guard<std::mutex> lock(g_clients_mutex);
        for (auto& sock : g_clients) {
            closesocket(sock);
        }
        g_clients.clear();
        closesocket(g_server_socket);
        WSACleanup();
#endif
    });
}

void broadcast_system_labs_dump(const std::string& payload) {
#ifdef _WIN32
    if (!g_listener_running.load()) return;
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    auto it = g_clients.begin();
    while (it != g_clients.end()) {
        int res = send(*it, payload.c_str(), static_cast<int>(payload.size()), 0);
        if (res == SOCKET_ERROR) {
            closesocket(*it);
            it = g_clients.erase(it);
        } else {
            ++it;
        }
    }
#endif
}

py::dict get_quantum_16k_telemetry(double total_loops) {
    py::dict d;
    double total_l = total_loops;

    double p_zero = g_state_pop_pure_zero.load(std::memory_order_relaxed) / total_l;
    double p_one  = g_state_pop_full_one.load(std::memory_order_relaxed) / total_l;
    double global_coh = g_global_p3_coherence.load(std::memory_order_relaxed) / total_l;

    d["pop_0000_16k"] = p_zero;
    d["pop_1111_16k"] = p_one;
    d["global_coherence"] = global_coh;
    
    double residual_resonance = 1.0 - std::abs(global_coh);
    d["trace_16k"] = p_zero + p_one + residual_resonance;
    
    d["zibi_triggers"] = g_zibi_activations.load(std::memory_order_relaxed);
    d["ml_exhausts"]   = g_ml_exhaust_triggers.load(std::memory_order_relaxed);

    double purity_p3 = (global_coh * global_coh) + (1.0 - std::abs(global_coh));
    if (purity_p3 > 1.0) purity_p3 = 1.0;

    d["purity_p3"] = purity_p3;
    d["ram_allocation_bytes"] = 0.0;

    std::string dump = "SYSTEM_LABS_DUMP: " + py::repr(d).cast<std::string>() + "\n";
    broadcast_system_labs_dump(dump);

    return d;
}

void stop_telemetry_listener() {
    if (!g_listener_running.load()) return;
    g_listener_running.store(false);
    if (g_telemetry_listener.joinable()) {
        g_telemetry_listener.join();
    }
}

PYBIND11_MODULE(amsin_quantum_16k, m) {
    m.doc() = "AMSIN Quantum Cascade 16-Kubit Core (System Labs, 2026)";
    m.def("execute_quantum_16k_simulation", &execute_quantum_16k_simulation, "Masowy silnik kaskady 16 kubitów");
    m.def("get_quantum_16k_telemetry",    &get_quantum_16k_telemetry,    "Port telemetryczny dla rejestru 16k");
    m.def("start_telemetry_listener",      &start_telemetry_listener,     "Uruchomienie pasywnego portu nasłuchu 1495");
    m.def("stop_telemetry_listener",       &stop_telemetry_listener,      "Bezpieczne zatrzymanie i sterylizacja gniazd sieciowych");
}
