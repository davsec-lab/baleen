#include "../include/statistics.h"
#include <iostream>

namespace baleen {

Statistics::Statistics() { PIN_InitLock(&lock); }

Statistics::~Statistics() { delete &lock; }

void Statistics::recordRead(const std::string &objectName, Language lang) {
  PIN_GetLock(&lock, 0);
  objectStats[objectName].reads[lang]++;
  PIN_ReleaseLock(&lock);
}

void Statistics::recordWrite(const std::string &objectName, Language lang) {
  PIN_GetLock(&lock, 0);
  objectStats[objectName].writes[lang]++;
  PIN_ReleaseLock(&lock);
}

void Statistics::recordMalloc(Language lang, USIZE size) {
  PIN_GetLock(&lock, 0);

  if (lang == Language::RUST) {
    allocStats.rustBytes += size;
  } else {
    allocStats.cBytes += size;
  }

  PIN_ReleaseLock(&lock);
}

void Statistics::initializeObject(const std::string &objectName) {
  PIN_GetLock(&lock, 0);

  if (objectStats.find(objectName) == objectStats.end()) {
    objectStats[objectName] = AccessStats();
  }

  PIN_ReleaseLock(&lock);
}

void Statistics::exportToCSV(const std::string &objectsFile,
                             const std::string &allocsFile) {
  // Export object statistics
  std::ofstream objCsv(objectsFile);
  objCsv << "Name,Reads (Rust),Reads (C),Writes (Rust),Writes (C)" << std::endl;

  for (const auto &pair : objectStats) {
    const std::string &objName = pair.first;
    const AccessStats &stats = pair.second;

    objCsv << objName << "," << stats.reads.at(Language::RUST) << ","
           << stats.reads.at(Language::C) << ","
           << stats.writes.at(Language::RUST) << ","
           << stats.writes.at(Language::C) << std::endl;
  }

  objCsv.close();

  // Export allocation statistics
  std::ofstream allocCsv(allocsFile);
  allocCsv << "Malloced (Rust),Malloced (C),Malloced (Total)" << std::endl;
  allocCsv << allocStats.rustBytes << "," << allocStats.cBytes << ","
           << allocStats.total() << std::endl;

  allocCsv.close();
}

void Statistics::printSummary(std::ostream &out) {
  out << "========== Access Summary ==========" << std::endl;

  for (const auto &pair : objectStats) {
    const std::string &objName = pair.first;
    const AccessStats &stats = pair.second;

    int rustReads = stats.reads.at(Language::RUST);
    int cReads = stats.reads.at(Language::C);
    int rustWrites = stats.writes.at(Language::RUST);
    int cWrites = stats.writes.at(Language::C);

    out << "Object \"" << objName << "\":" << std::endl;
    out << "  Reads  - Rust: " << rustReads << ", C: " << cReads
        << ", Total: " << (rustReads + cReads) << std::endl;
    out << "  Writes - Rust: " << rustWrites << ", C: " << cWrites
        << ", Total: " << (rustWrites + cWrites) << std::endl;
    out << std::endl;
  }

  out << "==========================================" << std::endl;
}

const AccessStats *
Statistics::getAccessStats(const std::string &objectName) const {
  auto it = objectStats.find(objectName);
  if (it != objectStats.end()) {
    return &it->second;
  }
  return nullptr;
}

} // namespace baleen
