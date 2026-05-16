#include "network/protocol.h"                           // контракт сериализации
#include "common/binary_io.h"                           // примитивы чтения/записи
#include "common/row_serialization.h"                   // сериализация tuple
#include "common/status.h"                              // для проверок

namespace db {

std::string Protocol::SerializeRequest(const Request& req) { // упаковка запроса в байты
    ByteBuffer buffer;                                  // аккумулятор байтов
    binary_io::WriteString(&buffer, req.request_id);    // 4 байта длины + строка
    binary_io::WriteString(&buffer, req.database);      // имя базы
    binary_io::WriteString(&buffer, req.sql);           // текст запроса
    binary_io::WriteString(&buffer, req.auth_token);    // токен авторизации
    return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()); // оборачиваем в string
}

bool Protocol::DeserializeRequest(const std::string& data, Request* out) { // распаковка запроса
    if (out == nullptr) return false;                   // защита от nullptr
    ByteBuffer bytes(data.begin(), data.end());         // string -> вектор байт
    std::size_t offset = 0;                             // текущая позиция чтения
    Status status = binary_io::ReadString(bytes, &offset, &out->request_id); // идентификатор
    if (!status.ok()) return false;                     // битые данные
    status = binary_io::ReadString(bytes, &offset, &out->database); // база
    if (!status.ok()) return false;                     // не хватило байт
    status = binary_io::ReadString(bytes, &offset, &out->sql); // sql
    if (!status.ok()) return false;                     // обрыв посередине
    status = binary_io::ReadString(bytes, &offset, &out->auth_token); // токен
    if (!status.ok()) return false;                     // битый конец
    return offset == bytes.size();                      // лишние байты тоже ошибка
}

std::string Protocol::SerializeResponse(const Response& resp) { // упаковка ответа
    ByteBuffer buffer;                                  // начинаем с пустого буфера
    binary_io::WriteBool(&buffer, resp.ok);             // флаг успеха
    binary_io::WriteString(&buffer, resp.error);        // текст ошибки
    binary_io::WriteBool(&buffer, resp.result.ok);      // флаг внутри QueryResult
    binary_io::WriteString(&buffer, resp.result.error); // текст внутри QueryResult
    binary_io::WriteUint32(&buffer, static_cast<std::uint32_t>(resp.result.affected_rows)); // число затронутых строк
    binary_io::WriteUint32(&buffer, static_cast<std::uint32_t>(resp.result.columns.size())); // количество колонок
    for (const auto& col : resp.result.columns) {       // пишем имена колонок
        binary_io::WriteString(&buffer, col);
    }
    binary_io::WriteUint32(&buffer, static_cast<std::uint32_t>(resp.result.rows.size())); // число строк
    for (const auto& row : resp.result.rows) {          // сериализуем каждую строку
        row_serialization::SerializeTuple(row, &buffer);
    }
    return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

bool Protocol::DeserializeResponse(const std::string& data, Response* out) { // распаковка ответа
    if (out == nullptr) return false;                   // нет куда складывать
    ByteBuffer bytes(data.begin(), data.end());         // копируем в буфер
    std::size_t offset = 0;                             // позиция
    Status status = binary_io::ReadBool(bytes, &offset, &out->ok); // общий флаг
    if (!status.ok()) return false;
    status = binary_io::ReadString(bytes, &offset, &out->error); // общая ошибка
    if (!status.ok()) return false;
    status = binary_io::ReadBool(bytes, &offset, &out->result.ok); // флаг результата
    if (!status.ok()) return false;
    status = binary_io::ReadString(bytes, &offset, &out->result.error); // текст результата
    if (!status.ok()) return false;
    std::uint32_t affected = 0;                         // временная переменная
    status = binary_io::ReadUint32(bytes, &offset, &affected); // affected_rows
    if (!status.ok()) return false;
    out->result.affected_rows = affected;               // пишем в структуру
    std::uint32_t col_count = 0;                        // число колонок
    status = binary_io::ReadUint32(bytes, &offset, &col_count);
    if (!status.ok()) return false;
    out->result.columns.resize(col_count);              // выделяем место
    for (std::uint32_t i = 0; i < col_count; ++i) {    // читаем имена
        status = binary_io::ReadString(bytes, &offset, &out->result.columns[i]);
        if (!status.ok()) return false;
    }
    std::uint32_t row_count = 0;                        // число строк
    status = binary_io::ReadUint32(bytes, &offset, &row_count);
    if (!status.ok()) return false;
    out->result.rows.resize(row_count);                 // выделяем вектор
    for (std::uint32_t i = 0; i < row_count; ++i) {    // читаем tuple
        status = row_serialization::DeserializeTuple(bytes, &offset, &out->result.rows[i]);
        if (!status.ok()) return false;
    }
    return offset == bytes.size();                      // строгое соответствие размеру
}

} // namespace db
