#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "pin.H"

#include <string>
#include <map>
#include <iostream>
#include <fstream>

using std::string;
using std::map;
using std::ofstream;
using std::endl;

enum class Language {
    SHARED,
    RUST,
    C
};

string LanguageToString(Language lang);

class LanguageTracker {
private:
	PIN_LOCK lock;
	map<THREADID, Language> language;
	ofstream log;

public:
	LanguageTracker();
	VOID CheckState(THREADID tid, Language newLang, const string* rtnName);
	Language GetCurrent(THREADID tid);
};

#endif // LANGUAGE_H