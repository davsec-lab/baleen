#ifndef LOGGER_H
#define LOGGER_H

#include "pin.H"
#include <fstream>
#include <map>

using std::map;
using std::ofstream;

enum class LogSubject {
    INSTRUMENTATION,
    EXECUTION,
    MEMORY,
    ACCESS,
    OBJECTS
};

class Logger {
private:
    PIN_LOCK lock;
    
    // Map each subject to its log file
    map<LogSubject, ofstream> streams;
    
public:
    Logger();
    ~Logger();
    
    // Stream method that returns the stream for a given subject
    ofstream& Stream(LogSubject subject);
    
    // Report stream (separate, for final reports)
    ofstream& GetReportStream();
    
    void CloseAll();
};

#endif // LOGGER_H