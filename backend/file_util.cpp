#include "file_util.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace std;

void cleanLine(string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() >= 3 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
}

static bool endsWithoutNewline(const string& path) {
    ifstream in(path, ios::binary);
    if (!in) return false;
    in.seekg(0, ios::end);
    if (in.tellg() <= 0) return false;
    in.seekg(-1, ios::end);
    char last = '\n';
    in.get(last);
    return last != '\n';
}

bool appendLine(const string& path, const string& line) {
    filesystem::path file(path);
    if (file.has_parent_path()) {
        error_code ignored;
        filesystem::create_directories(file.parent_path(), ignored);
    }

    bool needsNewline = endsWithoutNewline(path);

    ofstream out(path, ios::app);
    if (!out) {
        error_code ignored;
        cerr << "ERROR: could not write to " << filesystem::absolute(file, ignored).string() << endl;
        return false;
    }
    if (needsNewline) out << '\n';
    out << line << '\n';
    return true;
}
