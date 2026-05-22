#include <gtest/gtest.h>                              
#include <thread>                                       
#include <chrono>                                       
#include <filesystem>                                 
#include "server/database.h"                          
#include "server/query_processor.h"                   
#include "server/storage_service.h"                   
#include "network/tcp_server.h"                       
#include "network/tcp_client.h"                       

using namespace db;                                   

TEST(NetworkE2E, FullPipeline) {                      
    Config config;                                    
    config.data_dir = "./test_data_e2e_net";          
    config.port = 17778;                              
    std::filesystem::remove_all(config.data_dir);     

    Database db(config);                              
    ASSERT_TRUE(db.Start().ok());                     

    QueryProcessor qp(&db);                           
    StorageService service(&qp, config);              

    TcpServer server(config.host, config.port);       
    Status s = server.Start([&service](const Session& session, const Request& req) { 
        return service.HandleRequest(session, req);     
    });
    ASSERT_TRUE(s.ok()) << s.message();               

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); 

    TcpClient client(config.host, config.port);       

    Request req1;                                     
    req1.sql = "CREATE DATABASE netdb;";
    req1.request_id = "e2e-1";
    Response resp1;
    s = client.Send(req1, &resp1);                    
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp1.ok) << resp1.error;             

    Request req2;                                     
    req2.sql = "USE netdb;";
    req2.request_id = "e2e-1";
    Response resp2;
    s = client.Send(req2, &resp2);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp2.ok);                            

    Request req3;                                     
    req3.sql = "CREATE TABLE items (id INT, label STRING);";
    req3.request_id = "e2e-1";
    Response resp3;
    s = client.Send(req3, &resp3);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp3.ok);                            

    Request req4;                                     
    req4.sql = "INSERT INTO items (id, label) VALUE (7, \"test\");";
    req4.request_id = "e2e-1";
    Response resp4;
    s = client.Send(req4, &resp4);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp4.ok);                            
    EXPECT_EQ(resp4.result.affected_rows, 1u);        

    Request req5;                                     
    req5.sql = "SELECT * FROM items;";
    req5.request_id = "e2e-1";
    Response resp5;
    s = client.Send(req5, &resp5);
    ASSERT_TRUE(s.ok()) << s.message();
    EXPECT_TRUE(resp5.ok);                            
    ASSERT_EQ(resp5.result.rows.size(), 1u);          
    EXPECT_EQ(resp5.result.rows[0].values[0].AsInt(), 7);       
    EXPECT_EQ(resp5.result.rows[0].values[1].AsString(), "test"); 

    server.Stop();                                    
    db.Stop();                                        
}
