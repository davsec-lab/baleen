#include <iostream>

#include "logger.h"
#include "utilities.h"

using std::cerr;
using std::endl;

Logger::Logger() {
    PIN_InitLock(&lock);

    Run("mkdir -p .baleen");
    
    // Open log files for each subject
    streams[LogSubject::INSTRUMENTATION].open(".baleen/instrumentation.log");
    streams[LogSubject::EXECUTION].open(".baleen/execution.log");
    streams[LogSubject::MEMORY].open(".baleen/memory.log");
    streams[LogSubject::ACCESS].open(".baleen/access.log");
    streams[LogSubject::OBJECTS].open(".baleen/objects.log");
    
    // Verify all streams opened successfully
    for (auto& pair : streams) {
        if (!pair.second.is_open()) {
            cerr << "[WARNING] Failed to open log file for subject " 
                 << static_cast<int>(pair.first) << endl;
        }
    }
}

Logger::~Logger() {
    CloseAll();
}

ofstream& Logger::Stream(LogSubject subject) {
    return streams[subject];
}

void Logger::CloseAll() {
    PIN_GetLock(&lock, PIN_ThreadId() + 1);
    
    for (auto& pair : streams) {
        if (pair.second.is_open()) {
            pair.second.close();
        }
    }

    PIN_ReleaseLock(&lock);
}
