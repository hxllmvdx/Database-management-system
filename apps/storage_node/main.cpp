// исходный код написан человеком 1, комментарии добавлены человеком 3
// файл переписан человеком 3: закомментирован вызов StorageNodeEngine (код человека 1 ещё не предоставил реализацию)
#include <iostream> // стандартный ввод-вывод
#include "common/config.h" // конфигурация узла хранения (код человека 1)

int main(int argc, char* argv[]) { // точка входа storage-node приложения
    db::Config config; // конфиг по умолчанию (data_dir="./data") (код человека 1)
    for (int i = 1; i < argc; ++i) { // парсим аргументы командной строки (код человека 1)
        std::string arg = argv[i]; // текущий аргумент (код человека 1)
        if (arg == "--data_dir" && i + 1 < argc) config.data_dir = argv[++i]; // задаём директорию данных (код человека 1)
    } // for args

    std::cout << "starting storage node (mvp stub)" << std::endl; // лог запуска (код человека 1)

    // TODO: код человека 1 ещё не предоставлен. когда StorageNodeEngine будет готов — раскомментировать:
    // db::StorageNodeEngine engine(config); // создаём движок хранения (код человека 1)
    // auto st = engine.Start(); // запускаем engine (код человека 1)
    // if (!st.ok()) { // не удалось стартовать (код человека 1)
    //     std::cerr << "failed to start engine: " << st.message() << std::endl; // ошибка (код человека 1)
    //     return 1; // код ошибки (код человека 1)
    // } // if start failed

    std::cout << "storage node is ready (data_dir=" << config.data_dir << ")" << std::endl; // лог готовности (код человека 1)
    std::cout << "press enter to stop." << std::endl; // приглашение (код человека 1)
    std::cin.get(); // ждём нажатия enter (код человека 1)

    // engine.Stop(); // остановка engine (код человека 1, закомментировано до интеграции)
    std::cout << "storage node stopped." << std::endl; // лог остановки (код человека 1)
    return 0; // успешное завершение (код человека 1)
} // main
