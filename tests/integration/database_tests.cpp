#include <gtest/gtest.h>                              
#include <filesystem>                                 
#include "server/database.h"                          
#include "server/query_processor.h"                   
#include "server/session_context.h"                   
#include "common/config.h"                            

using namespace db;                                   

TEST(Database, Lifecycle) {                           
    Config config;                                    
    config.data_dir = "./test_data_lifecycle";        
    std::filesystem::remove_all(config.data_dir);     
    Database db(config);                              
    Status status = db.Start();                       
    ASSERT_TRUE(status.ok()) << status.message();     
    status = db.Stop();                               
    ASSERT_TRUE(status.ok()) << status.message();     
}

TEST(Database, EngineAccess) {                        
    Config config;
    config.data_dir = "./test_data_engine";
    std::filesystem::remove_all(config.data_dir);
    Database db(config);
    ASSERT_TRUE(db.Start().ok());                     
    StorageNodeEngine& engine = db.engine();          
    Status status = engine.CreateDatabase("testdb");  
    ASSERT_TRUE(status.ok()) << status.message();     
    db.Stop();                                        
}

TEST(Database, RestartPreservesData) {                
    Config config;                                    
    config.data_dir = "./test_data_restart";          
    std::filesystem::remove_all(config.data_dir);     

    {                                                 
        Database db(config);                          
        ASSERT_TRUE(db.Start().ok());                 
        QueryProcessor qp(&db);                       
        SessionContext ctx;                           
        ctx.client_id = "test";                       
        QueryResult rc1 = qp.Execute("CREATE DATABASE testdb;", &ctx); 
        ASSERT_TRUE(rc1.ok);
        QueryResult rc2 = qp.Execute("USE testdb;", &ctx); 
        ASSERT_TRUE(rc2.ok);
        QueryResult rc3 = qp.Execute("CREATE TABLE users (id INT, name STRING);", &ctx); 
        ASSERT_TRUE(rc3.ok);
        QueryResult r = qp.Execute("INSERT INTO users (id, name) VALUE (42, \"persisted\");", &ctx); 
        ASSERT_TRUE(r.ok) << r.error;                 
        ASSERT_EQ(r.affected_rows, 1u);               
        db.Stop();                                    
    }

    Database db2(config);                             
    ASSERT_TRUE(db2.Start().ok());                    
    QueryProcessor qp2(&db2);                         
    SessionContext ctx2;                              
    ctx2.client_id = "test";                          
    QueryResult rc_use = qp2.Execute("USE testdb;", &ctx2); 
    ASSERT_TRUE(rc_use.ok);
    QueryResult sel = qp2.Execute("SELECT * FROM users;", &ctx2); 
    ASSERT_TRUE(sel.ok) << sel.error;                 
    ASSERT_EQ(sel.rows.size(), 1u);                   
    EXPECT_EQ(sel.rows[0].values[0].AsInt(), 42);     
    EXPECT_EQ(sel.rows[0].values[1].AsString(), "persisted"); 
    db2.Stop();                                       
}
