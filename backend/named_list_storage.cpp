#include "named_list_storage.h"
#include "file_util.h"
#include <fstream>
using namespace std;

vector<string> listNamedItems(const string& filePath) {
    vector<string> items;
    ifstream file(filePath);
    string line;
    while (getline(file, line)) {
        cleanLine(line);
        if (!line.empty()) items.push_back(line);
    }
    return items;
}

bool addNamedItem(const string& filePath, const string& name) {
    if (name.empty() || name.find_first_of("\t\n\r") != string::npos) return false;
    for (const string& item : listNamedItems(filePath)) {
        if (item == name) return false;
    }
    return appendLine(filePath, name);
}

bool removeNamedItem(const string& filePath, const string& name) {
    ifstream in(filePath);
    if (!in) return false;

    vector<string> items;
    string line;
    bool removed = false;
    while (getline(in, line)) {
        cleanLine(line);
        if (!removed && line == name) {
            removed = true;
            continue;
        }
        if (!line.empty()) items.push_back(line);
    }
    in.close();
    if (!removed) return false;

    ofstream out(filePath, ios::trunc);
    if (!out) return false;
    for (const string& item : items) out << item << '\n';
    out.flush();
    return out.good();
}
