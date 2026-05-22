#include <gtest/gtest.h>                              
#include "network/protocol.h"                         
#include "execution/value.h"                          

using namespace db;                                   

TEST(Protocol, RoundTripRequest) {                    
    Request req;                                      
    req.request_id = "req-42";                        
    req.database = "test_db";                         
    req.sql = "select * from t";                      
    req.auth_token = "secret";                        

    std::string data = Protocol::SerializeRequest(req); 
    Request out;                                      
    ASSERT_TRUE(Protocol::DeserializeRequest(data, &out)); 
    EXPECT_EQ(out.request_id, req.request_id);        
    EXPECT_EQ(out.database, req.database);
    EXPECT_EQ(out.sql, req.sql);
    EXPECT_EQ(out.auth_token, req.auth_token);
}

TEST(Protocol, RoundTripResponse) {                   
    Response resp;                                    
    resp.ok = true;                                   
    resp.error = "";                                  
    resp.result.ok = true;                            
    resp.result.error = "";
    resp.result.affected_rows = 2;                    
    resp.result.columns = {"id", "name"};             
    resp.result.rows = {                              
        Tuple{{Value::Int(1), Value::String("alice")}},
        Tuple{{Value::Int(2), Value::String("bob")}}
    };

    std::string data = Protocol::SerializeResponse(resp); 
    Response out;                                     
    ASSERT_TRUE(Protocol::DeserializeResponse(data, &out)); 
    EXPECT_TRUE(out.ok);                              
    EXPECT_EQ(out.result.affected_rows, 2u);          
    EXPECT_EQ(out.result.columns.size(), 2u);         
    EXPECT_EQ(out.result.rows.size(), 2u);            
    EXPECT_EQ(out.result.rows[0].values[0].AsInt(), 1); 
    EXPECT_EQ(out.result.rows[0].values[1].AsString(), "alice");
}

TEST(Protocol, CorruptedDataReturnsFalse) {           
    std::string garbage = "not-a-valid-payload";      
    Request req;
    EXPECT_FALSE(Protocol::DeserializeRequest(garbage, &req)); 
    Response resp;
    EXPECT_FALSE(Protocol::DeserializeResponse(garbage, &resp));
}
