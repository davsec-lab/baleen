#include "utilities.h"

BOOL EndsWith(string_view s, string_view suffix) {
    if (s.length() < suffix.length()) return false;
    size_t start_pos = s.length() - suffix.length();
    return s.substr(start_pos) == suffix;
}