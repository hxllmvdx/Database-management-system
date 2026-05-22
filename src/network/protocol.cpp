#include "network/protocol.h"                           
#include "common/binary_io.h"                           
#include "common/row_serialization.h"                   
#include "common/status.h"                              

namespace db {

std::string Protocol::SerializeRequest(const Request& req) { 
    ByteBuffer buffer;                                  
    binary_io::WriteString(&buffer, req.request_id);    
    binary_io::WriteString(&buffer, req.database);      
    binary_io::WriteString(&buffer, req.sql);           
    binary_io::WriteString(&buffer, req.auth_token);    
    return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size()); 
}

bool Protocol::DeserializeRequest(const std::string& data, Request* out) { 
    if (out == nullptr) return false;                   
    ByteBuffer bytes(data.begin(), data.end());         
    std::size_t offset = 0;                             
    Status status = binary_io::ReadString(bytes, &offset, &out->request_id); 
    if (!status.ok()) return false;                     
    status = binary_io::ReadString(bytes, &offset, &out->database); 
    if (!status.ok()) return false;                     
    status = binary_io::ReadString(bytes, &offset, &out->sql); 
    if (!status.ok()) return false;                     
    status = binary_io::ReadString(bytes, &offset, &out->auth_token); 
    if (!status.ok()) return false;                     
    return offset == bytes.size();                      
}

std::string Protocol::SerializeResponse(const Response& resp) { 
    ByteBuffer buffer;                                  
    binary_io::WriteBool(&buffer, resp.ok);             
    binary_io::WriteString(&buffer, resp.error);        
    binary_io::WriteBool(&buffer, resp.result.ok);      
    binary_io::WriteString(&buffer, resp.result.error); 
    binary_io::WriteUint32(&buffer, static_cast<std::uint32_t>(resp.result.affected_rows)); 
    binary_io::WriteUint32(&buffer, static_cast<std::uint32_t>(resp.result.columns.size())); 
    for (const auto& col : resp.result.columns) {       
        binary_io::WriteString(&buffer, col);
    }
    binary_io::WriteUint32(&buffer, static_cast<std::uint32_t>(resp.result.rows.size())); 
    for (const auto& row : resp.result.rows) {          
        row_serialization::SerializeTuple(row, &buffer);
    }
    return std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
}

bool Protocol::DeserializeResponse(const std::string& data, Response* out) { 
    if (out == nullptr) return false;                   
    ByteBuffer bytes(data.begin(), data.end());         
    std::size_t offset = 0;                             
    Status status = binary_io::ReadBool(bytes, &offset, &out->ok); 
    if (!status.ok()) return false;
    status = binary_io::ReadString(bytes, &offset, &out->error); 
    if (!status.ok()) return false;
    status = binary_io::ReadBool(bytes, &offset, &out->result.ok); 
    if (!status.ok()) return false;
    status = binary_io::ReadString(bytes, &offset, &out->result.error); 
    if (!status.ok()) return false;
    std::uint32_t affected = 0;                         
    status = binary_io::ReadUint32(bytes, &offset, &affected); 
    if (!status.ok()) return false;
    out->result.affected_rows = affected;               
    std::uint32_t col_count = 0;                        
    status = binary_io::ReadUint32(bytes, &offset, &col_count);
    if (!status.ok()) return false;
    out->result.columns.resize(col_count);              
    for (std::uint32_t i = 0; i < col_count; ++i) {    
        status = binary_io::ReadString(bytes, &offset, &out->result.columns[i]);
        if (!status.ok()) return false;
    }
    std::uint32_t row_count = 0;                        
    status = binary_io::ReadUint32(bytes, &offset, &row_count);
    if (!status.ok()) return false;
    out->result.rows.resize(row_count);                 
    for (std::uint32_t i = 0; i < row_count; ++i) {    
        status = row_serialization::DeserializeTuple(bytes, &offset, &out->result.rows[i]);
        if (!status.ok()) return false;
    }
    return offset == bytes.size();                      
}

} 
