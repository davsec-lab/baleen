#ifndef UTILITIES_H
#define UTILITIES_H

#include "pin.H"

using std::string_view;
using std::string;

BOOL EndsWith(string_view s, string_view suffix);

string ExtractFileName(const string& fullPath);

#endif // UTILITIES_H