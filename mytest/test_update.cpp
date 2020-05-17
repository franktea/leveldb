/**********
 * 测试修改某个value，
 * leveldb不支持直接修改一个value，需要先将对应的key删除，
 * 然年再添加一个新的值。
 */
#include <iostream>
#include <random>
#include <cassert>
#include "leveldb/db.h"

void TestAdd(leveldb::DB* db)
{
    for(int i = 0; i < 10000000; ++i)
    {
        char key[100];
        char value[100];
        snprintf(key, sizeof(key), "thi is key %d", i);
        snprintf(value, sizeof(value), "this is value %d", i);
        leveldb::Status s = db->Put(leveldb::WriteOptions(), key, value);
        if(!s.ok()) std::cerr<<s.ToString()<<"\n";
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
    
    // close db。没有close方法，直接delete
    delete db;
}