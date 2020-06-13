// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// The representation of a DBImpl consists of a set of Versions.  The
// newest version is called "current".  Older versions may be kept
// around to provide a consistent view to live iterators.
//
// Each Version keeps track of a set of Table files per level.  The
// entire set of versions is maintained in a VersionSet.
//
// Version,VersionSet are thread-compatible, but require external
// synchronization on all accesses.

#ifndef STORAGE_LEVELDB_DB_VERSION_SET_H_
#define STORAGE_LEVELDB_DB_VERSION_SET_H_

#include <map>
#include <set>
#include <vector>

#include "db/dbformat.h"
#include "db/version_edit.h"
#include "port/port.h"
#include "port/thread_annotations.h"

namespace leveldb {

namespace log {
class Writer;
}

class Compaction;
class Iterator;
class MemTable;
class TableBuilder;
class TableCache;
class Version;
class VersionSet;
class WritableFile;

// Return the smallest index i such that files[i]->largest >= key.
// Return files.size() if there is no such file.
// REQUIRES: "files" contains a sorted list of non-overlapping files.
int FindFile(const InternalKeyComparator& icmp,
             const std::vector<FileMetaData*>& files, const Slice& key);

// Returns true iff some file in "files" overlaps the user key range
// [*smallest,*largest].
// smallest==nullptr represents a key smaller than all keys in the DB.
// largest==nullptr represents a key largest than all keys in the DB.
// REQUIRES: If disjoint_sorted_files, files[] contains disjoint ranges
//           in sorted order.
bool SomeFileOverlapsRange(const InternalKeyComparator& icmp,
                           bool disjoint_sorted_files,
                           const std::vector<FileMetaData*>& files,
                           const Slice* smallest_user_key,
                           const Slice* largest_user_key);

class Version {
 public:
  // Lookup the value for key.  If found, store it in *val and
  // return OK.  Else return a non-OK status.  Fills *stats.
  // REQUIRES: lock is not held
  struct GetStats {
    FileMetaData* seek_file;
    int seek_file_level;
  };

  // Append to *iters a sequence of iterators that will
  // yield the contents of this Version when merged together.
  // REQUIRES: This version has been saved (see VersionSet::SaveTo)
  void AddIterators(const ReadOptions&, std::vector<Iterator*>* iters);

  Status Get(const ReadOptions&, const LookupKey& key, std::string* val,
             GetStats* stats);

  // Adds "stats" into the current state.  Returns true if a new
  // compaction may need to be triggered, false otherwise.
  // REQUIRES: lock is held
  bool UpdateStats(const GetStats& stats);

  // Record a sample of bytes read at the specified internal key.
  // Samples are taken approximately once every config::kReadBytesPeriod
  // bytes.  Returns true if a new compaction may need to be triggered.
  // REQUIRES: lock is held
  bool RecordReadSample(Slice key);

  // Reference count management (so Versions do not disappear out from
  // under live iterators)
  void Ref();
  void Unref();

  void GetOverlappingInputs(
      int level,
      const InternalKey* begin,  // nullptr means before all keys
      const InternalKey* end,    // nullptr means after all keys
      std::vector<FileMetaData*>* inputs);

  // Returns true iff some file in the specified level overlaps
  // some part of [*smallest_user_key,*largest_user_key].
  // smallest_user_key==nullptr represents a key smaller than all the DB's keys.
  // largest_user_key==nullptr represents a key largest than all the DB's keys.
  bool OverlapInLevel(int level, const Slice* smallest_user_key,
                      const Slice* largest_user_key);

  // Return the level at which we should place a new memtable compaction
  // result that covers the range [smallest_user_key,largest_user_key].
  int PickLevelForMemTableOutput(const Slice& smallest_user_key,
                                 const Slice& largest_user_key);

  int NumFiles(int level) const { return files_[level].size(); }

  // Return a human readable string that describes this version's contents.
  std::string DebugString() const;

 private:
  friend class Compaction;
  friend class VersionSet;

  class LevelFileNumIterator;

  explicit Version(VersionSet* vset)
      : vset_(vset),
        next_(this),
        prev_(this),
        refs_(0),
        file_to_compact_(nullptr),
        file_to_compact_level_(-1),
        compaction_score_(-1),
        compaction_level_(-1) {}

  Version(const Version&) = delete;
  Version& operator=(const Version&) = delete;

  ~Version();

  Iterator* NewConcatenatingIterator(const ReadOptions&, int level) const;

  // Call func(arg, level, f) for every file that overlaps user_key in
  // order from newest to oldest.  If an invocation of func returns
  // false, makes no more calls.
  //
  // REQUIRES: user portion of internal_key == user_key.
  void ForEachOverlapping(Slice user_key, Slice internal_key, void* arg,
                          bool (*func)(void*, int, FileMetaData*));

  // 在VersionSet里面形成侵入式链表
  VersionSet* vset_;  // VersionSet to which this Version belongs
  Version* next_;     // Next version in linked list
  Version* prev_;     // Previous version in linked list
  int refs_;          // Number of live refs to this version

  // List of files per level
  std::vector<FileMetaData*> files_[config::kNumLevels];

  // Next file to compact based on seek stats.
  // 下一个要compact的文件
  FileMetaData* file_to_compact_;
  int file_to_compact_level_; 

  // Level that should be compacted next and its compaction score.
  // Score < 1 means compaction is not strictly needed.  These fields
  // are initialized by Finalize().
  // 下一个需要compact的leve和score。score<1说明compaction并不紧迫，这两个字段在Finalize()中初始化
  double compaction_score_;
  int compaction_level_;
};

// 每次产生一个新的Version以后，其实老的版本就没用了。
// 但是老的版本有可能依然被应用程序在访问数据（比如说持有指向老版本的Iterator在做遍历工作），
// 此时还不能马上删除老版本，所以新老几个版本可能会同时存在，VersionSet类用来管理多个版本。
// 内部采用双向循环链表进行管理多个Version的指针。
class VersionSet {
 public:
  VersionSet(const std::string& dbname, const Options* options,
             TableCache* table_cache, const InternalKeyComparator*);
  VersionSet(const VersionSet&) = delete;
  VersionSet& operator=(const VersionSet&) = delete;

  ~VersionSet();

  // Apply *edit to the current version to form a new descriptor that
  // is both saved to persistent state and installed as the new
  // current version.  Will release *mu while actually writing to the file.
  // REQUIRES: *mu is held on entry.
  // REQUIRES: no other thread concurrently calls LogAndApply()
  Status LogAndApply(VersionEdit* edit, port::Mutex* mu)
      EXCLUSIVE_LOCKS_REQUIRED(mu);

  // Recover the last saved descriptor from persistent storage.
  Status Recover(bool* save_manifest);

  // Return the current version.
  Version* current() const { return current_; }

  // Return the current manifest file number
  uint64_t ManifestFileNumber() const { return manifest_file_number_; }

  // Allocate and return a new file number
  uint64_t NewFileNumber() { return next_file_number_++; }

  // Arrange to reuse "file_number" unless a newer file number has
  // already been allocated.
  // REQUIRES: "file_number" was returned by a call to NewFileNumber().
  void ReuseFileNumber(uint64_t file_number) {
    if (next_file_number_ == file_number + 1) {
      next_file_number_ = file_number;
    }
  }

  // Return the number of Table files at the specified level.
  int NumLevelFiles(int level) const;

  // Return the combined file size of all files at the specified level.
  int64_t NumLevelBytes(int level) const;

  // Return the last sequence number.
  uint64_t LastSequence() const { return last_sequence_; }

  // Set the last sequence number to s.
  void SetLastSequence(uint64_t s) {
    assert(s >= last_sequence_);
    last_sequence_ = s;
  }

  // Mark the specified file number as used.
  void MarkFileNumberUsed(uint64_t number);

  // Return the current log file number.
  uint64_t LogNumber() const { return log_number_; }

  // Return the log file number for the log file that is currently
  // being compacted, or zero if there is no such log file.
  uint64_t PrevLogNumber() const { return prev_log_number_; }

  // Pick level and inputs for a new compaction.
  // Returns nullptr if there is no compaction to be done.
  // Otherwise returns a pointer to a heap-allocated object that
  // describes the compaction.  Caller should delete the result.
  Compaction* PickCompaction();

  // Return a compaction object for compacting the range [begin,end] in
  // the specified level.  Returns nullptr if there is nothing in that
  // level that overlaps the specified range.  Caller should delete
  // the result.
  Compaction* CompactRange(int level, const InternalKey* begin,
                           const InternalKey* end);

  // Return the maximum overlapping data (in bytes) at next level for any
  // file at a level >= 1.
  int64_t MaxNextLevelOverlappingBytes();

  // Create an iterator that reads over the compaction inputs for "*c".
  // The caller should delete the iterator when no longer needed.
  Iterator* MakeInputIterator(Compaction* c);

  // Returns true iff some level needs a compaction.
  bool NeedsCompaction() const {
    Version* v = current_;
    return (v->compaction_score_ >= 1) || (v->file_to_compact_ != nullptr);
  }

  // Add all files listed in any live version to *live.
  // May also mutate some internal state.
  void AddLiveFiles(std::set<uint64_t>* live);

  // Return the approximate offset in the database of the data for
  // "key" as of version "v".
  uint64_t ApproximateOffsetOf(Version* v, const InternalKey& key);

  // Return a human-readable short (single-line) summary of the number
  // of files per level.  Uses *scratch as backing store.
  struct LevelSummaryStorage {
    char buffer[100];
  };
  const char* LevelSummary(LevelSummaryStorage* scratch) const;

 private:
  class Builder;

  friend class Compaction;
  friend class Version;

  bool ReuseManifest(const std::string& dscname, const std::string& dscbase);

  // 初始化Version* v的compaction_score_和compaction_level_
  void Finalize(Version* v);

  void GetRange(const std::vector<FileMetaData*>& inputs, InternalKey* smallest,
                InternalKey* largest);

  void GetRange2(const std::vector<FileMetaData*>& inputs1,
                 const std::vector<FileMetaData*>& inputs2,
                 InternalKey* smallest, InternalKey* largest);

  void SetupOtherInputs(Compaction* c);

  // Save current contents to *log
  Status WriteSnapshot(log::Writer* log);

  // 将一个新的Version* v添加到当前VersionSet中
  void AppendVersion(Version* v);

  // 从构造函数传入进行初始化的与db相关的成员
  Env* const env_;
  const std::string dbname_;
  const Options* const options_;
  TableCache* const table_cache_;
  const InternalKeyComparator icmp_;
  
  // db元信息相关
  uint64_t next_file_number_; // log文件编号
  uint64_t manifest_file_number_; // manifest文件编号
  uint64_t last_sequence_;
  uint64_t log_number_; // log编号
  uint64_t prev_log_number_;  // 0 or backing store for memtable being compacted

  // Opened lazily，manifest相关
  WritableFile* descriptor_file_;
  log::Writer* descriptor_log_;
  // 循环双向链表的头结点，一个没用的元素，使得链表的操作更简单，和std::list中的实现类似。
  Version dummy_versions_;  // Head of circular doubly-linked list of versions.
  // current_是当前最新版本，注意它总是指向双向链表的最后一个元素，相当于tail
  Version* current_;        // == dummy_versions_.prev_

  // Per-level key at which the next compaction at that level should start.
  // Either an empty string, or a valid InternalKey.
  // 每个level下一次compacttion开始的key，要么为空的string，要么为一个合法的InternalKey
  std::string compact_pointer_[config::kNumLevels];
};

// A Compaction encapsulates information about a compaction.
// db中有一个compact后台线程，负责将memtable持久化成sstable，以及均衡整个db中各个level的sstable。
// compact线程会优先将写满的memtable dump成level 0的sstable（不会合并相同的key或者清理已经删除的key），
// 然后，根据设计的策略选取level n以及level n+1中有key range overlap的几个sstable进行合并，
// 期间会合并相同的key以及清理删除的key，最后生成若干个level n+1的sstable。随着数据不断地写入和compact的进行，
// 低level的sstable不断向高级别level迁移。level 0中的sstable是由memtable直接dump得到，所以key range可能有
// overlap，而level 1以及更高的级别中的sstable都是有merge产生，保证了位于同一个level的sstalbe之间的
// key range不会有overlap。
class Compaction {
 public:
  ~Compaction();

  // Return the level that is being compacted.  Inputs from "level"
  // and "level+1" will be merged to produce a set of "level+1" files.
  int level() const { return level_; }

  // Return the object that holds the edits to the descriptor done
  // by this compaction.
  VersionEdit* edit() { return &edit_; }

  // "which" must be either 0 or 1
  int num_input_files(int which) const { return inputs_[which].size(); }

  // Return the ith input file at "level()+which" ("which" must be 0 or 1).
  FileMetaData* input(int which, int i) const { return inputs_[which][i]; }

  // Maximum size of files to build during this compaction.
  uint64_t MaxOutputFileSize() const { return max_output_file_size_; }

  // Is this a trivial compaction that can be implemented by just
  // moving a single input file to the next level (no merging or splitting)
  bool IsTrivialMove() const;

  // Add all inputs to this compaction as delete operations to *edit.
  void AddInputDeletions(VersionEdit* edit);

  // Returns true if the information we have available guarantees that
  // the compaction is producing data in "level+1" for which no data exists
  // in levels greater than "level+1".
  bool IsBaseLevelForKey(const Slice& user_key);

  // Returns true iff we should stop building the current output
  // before processing "internal_key".
  bool ShouldStopBefore(const Slice& internal_key);

  // Release the input version for the compaction, once the compaction
  // is successful.
  void ReleaseInputs();

 private:
  friend class Version;
  friend class VersionSet;

  Compaction(const Options* options, int level);

  int level_;  // 需要compact的level
  uint64_t max_output_file_size_; // 生成sstable的最大size(kTargetFileSize)
  Version* input_version_; // compact时当时的version
  VersionEdit edit_; // 记录compact过程中的操作

  // Each compaction reads inputs from "level_" and "level_+1"
  // inputs_[0]为level_的sstable文件信息，inputs_[1]为level_+1的sstable文件信息
  std::vector<FileMetaData*> inputs_[2];  // The two sets of inputs

  // State used to check for number of overlapping grandparent files
  // (parent == level_ + 1, grandparent == level_ + 2)
  // 位于level_+2，并且与compact的key range有overlap的sstable。
  // 保存grandparents_是因为compact最终会生成一系列level_+1的sstable，
  // 而如果生成sstable与level_+2中有过多的overlap的话，到时候compact会产生过多的merge，
  // 为避免这种情况，compact过程中需要检查与level_+2中产生overlap的size并与阈值kMaxGrandParentOverlapBytes做比较，
  // 以便提前终止compact。
  std::vector<FileMetaData*> grandparents_;
  // 记录compact时grandparents_中已经overlap的index
  size_t grandparent_index_;  // Index in grandparent_starts_
  // 记录是否已经有key检查overlap，如果是第一次检查，发现有overlap，
  // 也不会增加overlapped_bytes_。——这样做有何意义？
  bool seen_key_;             // Some output key has been seen
  // 记录已经overlap的累计size
  int64_t overlapped_bytes_;  // Bytes of overlap between current output
                              // and grandparent files

  // State for implementing IsBaseLevelForKey

  // level_ptrs_ holds indices into input_version_->levels_: our state
  // is that we are positioned at one of the file ranges for each
  // higher level than the ones involved in this compaction (i.e. for
  // all L >= level_ + 2).
  // compact时，当key的ValueType是kTypeDeletion时，
  // 要检查其在level n+1以上是否存在（调用IsBaseLevelForKey())来决定是否
  // 丢弃掉该key。因为compact时，key的遍历是顺序的，所以每次检查从上一次检查结束的地方开始即可。
  // level_ptrs_[i]中就记录了input_vesion_->levels_[i]中，
  // 上一次比较结束的sstable的容器下标。
  size_t level_ptrs_[config::kNumLevels];
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_VERSION_SET_H_
