#ifndef BALEEN_STATISTICS_H
#define BALEEN_STATISTICS_H

#include "pin.H"
#include "types.h"

#include <fstream>
#include <map>
#include <string>

namespace baleen {

struct AccessStats {
  std::map<Language, int> reads;
  std::map<Language, int> writes;

  AccessStats() {
    reads[Language::RUST] = 0;
    reads[Language::C] = 0;
    writes[Language::RUST] = 0;
    writes[Language::C] = 0;
  }
};

struct AllocationStats {
  int rustBytes;
  int cBytes;

  AllocationStats() : rustBytes(0), cBytes(0) {}

  int total() const { return rustBytes + cBytes; }
};

class Statistics {
public:
  Statistics();
  ~Statistics();

  // Record a memory read
  void recordRead(const std::string &objectName, Language lang);

  // Record a memory write
  void recordWrite(const std::string &objectName, Language lang);

  // Record a malloc allocation
  void recordMalloc(Language lang, USIZE size);

  // Initialize statistics for a new object
  void initializeObject(const std::string &objectName);

  // Export statistics to CSV files
  void exportToCSV(const std::string &objectsFile,
                   const std::string &allocsFile);

  // Print summary to output stream
  void printSummary(std::ostream &out);

  // Get access stats for an object
  const AccessStats *getAccessStats(const std::string &objectName) const;

  // Get allocation stats
  const AllocationStats &getAllocationStats() const { return allocStats; }

private:
  mutable PIN_LOCK lock;
  std::map<std::string, AccessStats> objectStats;
  AllocationStats allocStats;
};

} // namespace baleen

#endif // BALEEN_STATISTICS_H
