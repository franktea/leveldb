源码中不严谨的地方：

```
Status DBImpl::NewDB() {
  VersionEdit new_db;
  new_db.SetComparatorName(user_comparator()->Name());
  new_db.SetLogNumber(0);
  new_db.SetNextFile(2);
  new_db.SetLastSequence(0);

  const std::string manifest = DescriptorFileName(dbname_, 1);
  WritableFile* file;
  Status s = env_->NewWritableFile(manifest, &file);
  if (!s.ok()) {
    return s; //这里return了后面的delete file就不执行了。严格的说可以算是内存泄露。
  }
  {
    log::Writer log(file);
    std::string record;
    new_db.EncodeTo(&record);
    s = log.AddRecord(record);
    if (s.ok()) {
      s = file->Close(); // file的析构函数里面会调用一次close。这里的close也是多余的。
    }
  }
  delete file;
  if (s.ok()) {
    // Make "CURRENT" file that points to the new manifest file.
    s = SetCurrentFile(env_, dbname_, 1);
  } else {
    env_->RemoveFile(manifest);
  }
  return s;
}
```


table/format.cc中的delete[] buf为什么不用RAII

```
Status ReadBlock(RandomAccessFile* file, const ReadOptions& options,
                 const BlockHandle& handle, BlockContents* result) {
  //…………
  
  // 下面的大段代码对于buf delete管理并没有采用raii，到处都是delete[] buf
  char* buf = new char[n + kBlockTrailerSize];
  Slice contents;
  Status s = file->Read(handle.offset(), n + kBlockTrailerSize, &contents, buf);
  if (!s.ok()) {
    delete[] buf; // 删了一次
    return s;
  }
  if (contents.size() != n + kBlockTrailerSize) {
    delete[] buf; // 这里还要调用删除
    return Status::Corruption("truncated block read");
  }
  
  // ……后面还有6处，闪瞎眼
```



