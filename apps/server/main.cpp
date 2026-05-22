#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include "common/config.h"
#include "logging/logger.h"
#include "network/tcp_server.h"
#include "server/database.h"
#include "server/query_processor.h"
#include "server/storage_service.h"

namespace fs = std::filesystem;

static volatile bool g_running = true;

static void SignalHandler(int) { g_running = false; }

static void PrintUsage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " [OPTIONS]\n"
        << "\n"
        << "Options:\n"
        << "  --host <addr>      Listen address         (default: 127.0.0.1)\n"
        << "  --port <n>         Listen port            (default: 7000)\n"
        << "  --data <dir>       Root data directory    (default: ./data)\n"
        << "                     Sub-dirs created automatically:\n"
        << "                       <data>/wal          — write-ahead log\n"
        << "                       <data>/task_queue   — async task WAL\n"
        << "                       <data>/logs         — server + access logs\n"
        << "                       <data>/rbac.json    — user/group store\n"
        << "  --log-level <lvl>  trace|debug|info|warn|error (default: info)\n"
        << "  --help             Show this help\n";
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

        if (arg == "--host")      { config.host      = next("--host"); }
        else if (arg == "--port") { config.port      = std::stoi(next("--port")); }
        else if (arg == "--data") {
            const fs::path root    = next("--data");
            config.data_dir        = root.string();
            config.wal_dir         = (root / "wal").string();
            config.task_queue_dir  = (root / "task_queue").string();
            config.log_dir         = (root / "logs").string();
        }
        else if (arg == "--log-level") { config.log_level = next("--log-level"); }
        else {
            std::cerr << "error: unknown option '" << arg << "'\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // ── Signals ────────────────────────────────────────────────────────────
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // ── Start subsystems ──────────────────────────────────────────────────
    db::Database database(config);
    db::Status   status = database.Start();
    if (!status.ok()) {
        std::cerr << "failed to start database: " << status.message() << "\n";
        return 1;
    }

    db::QueryProcessor qp(&database);
    db::StorageService service(&qp, database.config());

    db::TcpServer server(config.host, config.port);
    status = server.Start([&service](const db::Session& sess,
                                     const db::Request& req) {
        return service.HandleRequest(sess, req);
    });
    if (!status.ok()) {
        std::cerr << "failed to start server: " << status.message() << "\n";
        database.Stop();
        return 1;
    }

    db::Logger::Info("server ready",
                     {{"host", config.host},
                      {"port", std::to_string(config.port)},
                      {"data", config.data_dir}});
    std::cout << "CourseDB listening on " << config.host << ":" << config.port
              << "  data=" << config.data_dir
              << "  (Ctrl-C to stop)\n";

    // ── Run loop ──────────────────────────────────────────────────────────
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\nshutting down...\n";
    server.Stop();
    database.Stop();
    db::Logger::Shutdown();
    return 0;
}
