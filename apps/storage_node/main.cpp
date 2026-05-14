#include <iostream>
#include "common/config.h"
#include "runtime/storage_node_engine.h"

int main(int argc, char* argv[]) {
    db::Config config; // конфиг по умолчанию
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data_dir" && i + 1 < argc) config.data_dir = argv[++i];
    }

    std::cout << "starting storage node (mvp stub)" << std::endl;

    // mvp: просто инициализируем движок и выходим
    db::StorageNodeEngine engine(config);
    auto st = engine.Start();
    if (!st.ok()) {
        std::cerr << "failed to start engine: " << st.message() << std::endl;
        return 1;
    }

    std::cout << "storage node is ready (data_dir=" << config.data_dir << ")" << std::endl;
    std::cout << "press enter to stop." << std::endl;
    std::cin.get();

    engine.Stop();
    std::cout << "storage node stopped." << std::endl;
    return 0;
}
