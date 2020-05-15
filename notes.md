####修改一个value
leveldb没有提供修改key的接口，按照官方文档https://github.com/google/leveldb/blob/master/doc/index.md 的说法，修改将delete和put封装成一个batch：

```
#include "leveldb/write_batch.h"
...
std::string value;
leveldb::Status s = db->Get(leveldb::ReadOptions(), key1, &value);
if (s.ok()) {
  leveldb::WriteBatch batch;
  batch.Delete(key1);
  batch.Put(key1, value);
  s = db->Write(leveldb::WriteOptions(), &batch);
}
```

