#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "cluster/entrypoint.h"
#include "common/config.h"
#include "logging/logger.h"

namespace fs = std::filesystem;

static volatile bool g_running = true;

static void SignalHandler(int) { g_running = false; }

static void PrintUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " [OPTIONS] [host:port ...]\n"
        << "\n"
        << "Options:\n"
        << "  --host <addr>     Listen address          (default: 127.0.0.1)\n"
        << "  --port <n>        Entrypoint port         (default: 9000)\n"
        << "  --data <dir>      Root data directory     (default: ./data)\n"
        << "                    Logs go into <data>/logs/\n"
        << "  --log-level <lvl> trace|debug|info|warn|error (default: info)\n"
        << "  --help            Show this help\n"
        << "\n"
        << "Positional args: storage node descriptors as host:port\n"
        << "  Example: " << argv0 << " --port 9000 127.0.0.1:7000 127.0.0.1:7001\n";
}

int main(int argc, char* argv[]) {
    db::Config config;

    // ── Parse CLI flags ────────────────────────────────────────────────────
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }

        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "error: " << flag << " requires a value\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--host")           { config.host             = next("--host"); }
        else if (arg == "--port")      { config.entrypoint_port  = std::stoi(next("--port")); }
        else if (arg == "--data") {
            const std::string root = next("--data");
            config.data_dir        = root;
            config.log_dir         = (fs::path(root) / "logs").string();
        }
        else if (arg == "--log-level") { config.log_level = next("--log-level"); }
        else if (arg.rfind('-', 0) == 0) {
            std::cerr << "error: unknown option '" << arg << "'\n";
            PrintUsage(argv[0]);
            return 1;
        }
        else {
            // positional: host:port descriptor
            const auto colon = arg.rfind(':');
            if (colon == std::string::npos) {
                std::cerr << "error: invalid node descriptor (expected host:port): "
                          << arg << "\n";
                return 1;
            }
            db::NodeConfig nc;
            nc.id   = "node-" + std::to_string(config.nodes.size() + 1);
            nc.host = arg.substr(0, colon);
            nc.port = std::stoi(arg.substr(colon + 1));
            config.nodes.push_back(nc);
        }
    }

    if (config.nodes.empty()) {
        std::cerr << "warning: no storage nodes configured\n";
    }

    // ── Signals ────────────────────────────────────────────────────────────
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    db::Logger::Init(config);
    db::Logger::Info("entrypoint initialising",
                     {{"port",  std::to_string(config.entrypoint_port)},
                      {"nodes", std::to_string(config.nodes.size())},
                      {"data",  config.data_dir}});

    db::EntrypointNode entrypoint(config);
    db::Status s = entrypoint.Start();
    if (!s.ok()) {
        std::cerr << "failed to start entrypoint: " << s.message() << "\n";
        return 1;
    }

    std::cout << "CourseDB entrypoint listening on " << config.host
              << ":" << config.entrypoint_port
              << "  nodes=" << config.nodes.size()
              << "  data=" << config.data_dir
              << "  (Ctrl-C to stop)\n";

    // ── Run loop ──────────────────────────────────────────────────────────
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\nshutting down entrypoint...\n";
    entrypoint.Stop();
    db::Logger::Shutdown();
    return 0;
}
