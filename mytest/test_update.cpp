/**********
 * 测试修改某个value，
 * leveldb不支持直接修改一个value，需要先将对应的key删除，
 * 然年再添加一个新的值。
 */
#include <iostream>
#include <random>
#include <cassert>
#include <string>
#include "leveldb/db.h"
#include "leveldb/write_batch.h"

void TestAdd(leveldb::DB* db)
{
    for(int i = 0; i < 10000000; ++i)
    {
        char key[100];
        char value[100];
        snprintf(key, sizeof(key), "this is key %d", i);
        snprintf(value, sizeof(value), "this is value %d", i);
        leveldb::Status s = db->Put(leveldb::WriteOptions(), key, value);
        if(!s.ok()) std::cerr<<s.ToString()<<"\n";
    }
}

void TestDeleteAndAdd(leveldb::DB* db)
{
    for(int i = 0; i < 10000000; ++i)
    {
        char key[100];
        char value[100];
        snprintf(key, sizeof(key), "this is key %d", i);
        snprintf(value, sizeof(value), "this is new value %d", i);
        std::string old_value;
        leveldb::Status s = db->Get(leveldb::ReadOptions(), key, &old_value);
        if(s.ok())
        {
            leveldb::WriteBatch batch;
            batch.Delete(key);
            batch.Put(key, value);
            s = db->Write(leveldb::WriteOptions(), &batch);
        } else {
            std::cout<<"not found "<<key<<"\n";
        }
    }
}

void TestGet(leveldb::DB* db)
{
    std::string key = "this is key 1217311";
    std::string value;
    leveldb::Status s = db->Get(leveldb::ReadOptions(), key, &value);
    if(s.ok())
    {
        std::cout<<"get: "<<value<<"\n";
    } else {
        std::cout<<"not found\n";
    }
}

void TestBigKey(leveldb::DB* db)
{
    std::string key = "big key";
    for(int i = 0; i < 100000000; ++i)
    {
        key.append(std::string("key") + std::to_string(i));
    }

    std::string value = "big value";
    for(int i = 0; i < 100000000; ++i)
    {
        value.append(std::string("value") + std::to_string(i));
    }

    leveldb::Status s = db->Put(leveldb::WriteOptions(), key, value);
    if(!s.ok())
    {
        std::cout<<"failed to put key"<<"\n";
    }
}

void TestSmallKey(leveldb::DB* db)
{
    for(int i = 0; i < 10000; ++i)
    {
        std::string key = "this is a key";
        std::string value = "this is a value";
        leveldb::Status s = db->Put(leveldb::WriteOptions(), key, value);
        if(!s.ok())
            std::cout<<"can not insert key\n";
    }
}

int main()
{
    // opend db
    leveldb::DB* db;
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, "testdb", &db);
    assert(status.ok());

    TestAdd(db);
    //TestDeleteAndAdd(db);
    //TestGet(db);
    //TestBigKey(db);
    //TestSmallKey(db);
    
    // close db。没有close方法，直接delete
    delete db;
}