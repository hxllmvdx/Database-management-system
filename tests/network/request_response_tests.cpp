#include <gtest/gtest.h>                              
#include <thread>                                       
#include <chrono>                                       
#include "network/tcp_server.h"                         
#include "network/tcp_client.h"                         
#include "network/protocol.h"                           

using namespace db;                                   

TEST(Network, SmokeTest) {                            
    TcpServer server("127.0.0.1", 17777);             
    bool handler_called = false;                      
    Status start_status = server.Start([&handler_called](const Session& session, const Request& req) { 
        handler_called = true;                        
        Response resp;                                
        resp.ok = true;                               
        resp.result.ok = true;
        resp.result.affected_rows = 1;
        return resp;                                  
    });
    ASSERT_TRUE(start_status.ok()) << start_status.message(); 

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); 

    TcpClient client("127.0.0.1", 17777);             
    Request req;                                      
    req.sql = "select 1";                             
    req.request_id = "test-1";                        
    Response resp;                                    
    Status send_status = client.Send(req, &resp);     
    ASSERT_TRUE(send_status.ok()) << send_status.message(); 
    EXPECT_TRUE(resp.ok);                             
    EXPECT_EQ(resp.result.affected_rows, 1u);         
    EXPECT_TRUE(handler_called);                      

    server.Stop();                                    
}
