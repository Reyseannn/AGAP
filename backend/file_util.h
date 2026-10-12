#ifndef FILE_UTIL_H
#define FILE_UTIL_H

#include <string>
using namespace std;

// Cleans one line just read from a data file: removes the "\r" that Windows puts at the
// end of a line, and the invisible UTF-8 marker (BOM) that Notepad can add at the very
// start of a file. Without this, a login like "juan@x.com" would never match.
void cleanLine(string& line);

// Adds one line to the end of a text file. Creates the folder (and the file) if they
// don't exist yet, and makes sure the line starts on its own line even if the file was
// saved by hand without a final newline. Prints the full path to the console and
// returns false if the file can't be written.
bool appendLine(const string& path, const string& line);

#endif
