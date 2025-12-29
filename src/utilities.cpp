#include "utilities.h"

BOOL EndsWith(string_view s, string_view suffix) {
    if (s.length() < suffix.length()) return false;
    size_t start_pos = s.length() - suffix.length();
    return s.substr(start_pos) == suffix;
}

string ExtractFileName(const string& fullPath) {
    size_t lastSlashPos = fullPath.rfind('/');
    if (lastSlashPos == std::string::npos) {
        return fullPath;
    } else {
        return fullPath.substr(lastSlashPos + 1);
    }
}

int Run(const char* command) {
    FILE* pipe = popen(command, "r");

    if (pipe) {
        int status = pclose(pipe);
        return status;
    }

    return -1;
}