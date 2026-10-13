#ifndef NAMED_LIST_STORAGE_H
#define NAMED_LIST_STORAGE_H

#include <string>
#include <vector>
using namespace std;

vector<string> listNamedItems(const string& filePath);
bool addNamedItem(const string& filePath, const string& name);
bool removeNamedItem(const string& filePath, const string& name);

#endif
