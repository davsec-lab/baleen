#include "../include/memory.h"

#include <iostream>

namespace baleen {

MemoryTracker::MemoryTracker(Registry& reg, Statistics& stats, 
                             LanguageTracker& langTracker, std::ofstream& logFile)
    : registry(reg), statistics(stats), languageTracker(langTracker), memLog(logFile) {
    PIN_InitLock(&reallocLock);
    PIN_InitLock(&mallocLock);
}

MemoryTracker::~MemoryTracker() {
    delete &reallocLock;
    delete &mallocLock;
}

void MemoryTracker::recordMemRead(THREADID tid, ADDRINT ip, ADDRINT addr) {
    Node* node = registry.find(addr);
    
    if (node) {
        Language currentLang = languageTracker.get(tid);
        
        memLog << "[READ] Read from 0x" << std::hex << addr 
               << " ('" << node->object << "')" << std::dec << std::endl;
        
        statistics.recordRead(node->object, currentLang);
    }
}

void MemoryTracker::recordMemWrite(THREADID tid, ADDRINT ip, ADDRINT addr) {
    Node* node = registry.find(addr);
    
    if (node) {
        Language currentLang = languageTracker.get(tid);
        
        memLog << "[WRITE] Write to 0x" << std::hex << addr 
               << " ('" << node->object << "')" << std::dec << std::endl;
        
        statistics.recordWrite(node->object, currentLang);
    }
}

void MemoryTracker::beforeBaleen(ADDRINT addr, ADDRINT size, ADDRINT name) {
    std::string objectName;
    
    if (name != 0) {
        char buffer[256];
        PIN_SafeCopy(buffer, (void*)name, sizeof(buffer) - 1);
        buffer[255] = '\0';
        objectName = buffer;
    } else {
        objectName = "unnamed";
    }
    
    registry.insert(addr, size, objectName);
    objectStarts[objectName] = addr;
    statistics.initializeObject(objectName);
    
    memLog << "[BEFORE BALEEN] Object '" << objectName
           << "' occupies " << size
           << " bytes in range [0x" << std::hex << addr
           << ", 0x" << (addr + size)
           << ")" << std::dec << std::endl;
    
    memLog.flush();
}

void MemoryTracker::beforeMalloc(THREADID tid, USIZE size) {
    Language lang = languageTracker.get(tid);
    
    memLog << "[BEFORE MALLOC] " << size
           << " bytes requested by " << to_string(lang)
           << std::endl;
    
    PIN_GetLock(&mallocLock, tid);
    pendingMalloc[tid] = {lang, size};
    PIN_ReleaseLock(&mallocLock);
}

void MemoryTracker::afterMalloc(THREADID tid, ADDRINT ret) {
    PIN_GetLock(&mallocLock, tid);
    auto pending = pendingMalloc[tid];
    PIN_ReleaseLock(&mallocLock);
    
    // Only count successful allocations
    if (ret != 0) {
        statistics.recordMalloc(pending.lang, pending.size);
    }
}

void MemoryTracker::beforeRealloc(THREADID tid, ADDRINT addr, USIZE size) {
    PIN_GetLock(&reallocLock, tid);
    pendingRealloc[tid] = {addr, size};
    PIN_ReleaseLock(&reallocLock);
}

void MemoryTracker::afterRealloc(THREADID tid, ADDRINT ret) {
    PIN_GetLock(&reallocLock, tid);
    auto args = pendingRealloc[tid];
    PIN_ReleaseLock(&reallocLock);
    
    std::cout << "realloc(0x" << std::hex << args.addr << std::dec
              << ", " << args.size
              << ") → 0x" << std::hex << ret << std::dec << std::endl;
    
    Node* node = registry.remove(args.addr);
    
    if (node) {
        memLog << "[AFTER REALLOC] Object '" << node->object
               << "' was reallocated!" << std::endl;
        
        memLog << "[AFTER REALLOC] - [0x" << std::hex << node->start
               << ", 0x" << (node->start + node->size)
               << ") → [0x" << ret
               << ", 0x" << (ret + args.size)
               << ")" << std::dec << std::endl;
        
        memLog << "[AFTER REALLOC] - " << node->size
               << " → " << args.size
               << " bytes" << std::endl;
        
        registry.insert(ret, args.size, node->object);
        objectStarts[node->object] = ret;
        
        delete node;
    }
}

void MemoryTracker::beforeFree(ADDRINT addr) {
    Node* removed = registry.remove(addr);
    
    if (removed) {
        memLog << "[BEFORE FREE] Object '" << removed->object
               << "' is no longer mapped to range [0x" << std::hex << removed->start
               << ", 0x" << (removed->start + removed->size)
               << ")" << std::dec << std::endl;
        
        objectStarts[removed->object] = 0;
        delete removed;
    }
    
    std::cout << "free(0x" << std::hex << addr << std::dec << ")" << std::endl;
}

} // namespace baleen
