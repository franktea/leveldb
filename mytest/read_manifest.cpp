/**
 * 读取manifest文件的内容。
 * 
 * 2020-05-19
 */

#include <iostream>
#include <string>
#include <fstream>
#include <streambuf>
#include "port/port_stdcxx.h"
#include "db/version_set.h"

using namespace std;

int main(int argc, char** argv) {
    if(argc != 2) {
        std::cout<<"usage xxx manifest_file_name\n";
        return -1;
    }

    // 将文件内容读入到string中
    ifstream f(argv[1]);
    std::string text( (istreambuf_iterator<char>(f)), 
        (istreambuf_iterator<char>()) );

    leveldb::VersionEdit ve;
    leveldb::Slice slice(text);
    leveldb::Status s = ve.DecodeFrom(slice);
    if(!s.ok()) {
        cout<<"error: "<<s.ToString()<<"\n";
    }
}