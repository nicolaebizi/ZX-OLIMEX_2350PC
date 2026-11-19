#pragma once

/**
 * Thanks to:
 * 
 * https://www.programiz.com/dsa/quick-sort 
 * https://iq.opengenus.org/quick-sort-using-stack/
 * https://alienryderflex.com/quicksort/
 * https://github.com/faf0/LampSort/blob/master/src/de/ffoerg/sort/LampSort.java
 * 
 */

#include <stdio.h>
#include "ff.h"
#include <map>
#include <vector>
#include "FatFsDirCacheInputStream.h"
#include "FatFsDirCacheOutputStream.h"

class FatFsDirCache;

class FatFsDirCacheSorter {
private:

  FatFsDirCacheInputStream* _is;
  FatFsDirCacheOutputStream* _os;
  
  // Entries are stored as 16 bits to save heap, which limits a folder to MAX_ENTRIES
  std::vector<uint16_t> _stack;
  std::vector<uint16_t> _index;

  bool read(uint32_t i, FILINFO *info);
  void swap(int32_t low, int32_t high);
  int32_t partition(int32_t low, int32_t high);
  bool lampSort();
  bool flush();
  bool push(int32_t i);
  bool pop(int32_t* i);
  bool push(int32_t i, int32_t j) { return push(i) && push(j); }
  bool pop(int32_t* i, int32_t* j) { return pop(j) && pop(i); }
  static int compare(FILINFO *a, FILINFO *b);
  uint32_t stackSize();
  static int32_t min(int32_t i, int32_t j) { return i < j ? i : j; }
  static int32_t max(int32_t i, int32_t j) { return i > j ? i : j; }
  
public:

  static const uint32_t MAX_ENTRIES = 0x10000;

  FatFsDirCacheSorter(
    FatFsDirCacheInputStream* is,
    FatFsDirCacheOutputStream* os
  );
  
  ~FatFsDirCacheSorter() {}
  
  bool sort();
};

