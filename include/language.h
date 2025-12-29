#ifndef LANGUAGE_H
#define LANGUAGE_H

#include "pin.H"
#include "logger.h"

#include <stack>
#include <string>
#include <map>
#include <iostream>

using std::string;
using std::map;
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
	Logger& logger;

public:
	LanguageTracker(Logger& l): logger(l) {}

	Language GetCurrent(THREADID tid);

	VOID Enter(THREADID tid, Language newLang);

	VOID Exit(THREADID tid);
};

#endif // LANGUAGE_H