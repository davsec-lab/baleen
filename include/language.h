#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "pin.H"

#include <stack>

#include <string>
#include <map>
#include <iostream>
#include <fstream>

using std::string;
using std::map;
using std::ofstream;
using std::endl;
using std::stack;

enum class Language {
    RUST,
    C
};

string LanguageToString(Language lang);

class LanguageTracker {
private:
	PIN_LOCK lock;
	map<THREADID, Language> language;
	map<THREADID, stack<Language>> remembered;
	ofstream& log;

public:
	LanguageTracker(ofstream& l): log(l) {}

	Language GetCurrent(THREADID tid);

	VOID Enter(THREADID tid, Language newLang);

	VOID Exit(THREADID tid);
};

#endif // LANGUAGE_H