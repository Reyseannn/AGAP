#ifndef FILE_UTIL_H
#define FILE_UTIL_H

#include <string>
using namespace std;

void cleanLine(string& line);

bool appendLine(const string& path, const string& line);

#endif
