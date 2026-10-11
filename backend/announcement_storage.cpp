#include "announcement_storage.h"
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace std;

// Found relative to the backend folder, like every other data file.
static const string ANNOUNCEMENTS_FILE = "../data/announcements.txt";

// Columns on each line: id, title, category, message, postedDate
static const size_t FIELD_COUNT = 5;

// Cuts one line at every tab, padding with empty fields if the line is short.
static vector<string> splitByTab(const string& line) {
    vector<string> fields;
    stringstream stream(line);
    string field;
    while (getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    while (fields.size() < FIELD_COUNT) fields.push_back("");
    return fields;
}

static string toLine(const Announcement& a) {
    return a.id + '\t' + a.title + '\t' + a.category + '\t' + a.message + '\t' + a.postedDate;
}

// Reads the whole file in the order it was written (oldest first).
static vector<Announcement> readAll() {
    vector<Announcement> all;
    ifstream file(ANNOUNCEMENTS_FILE);
    string line;
    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        vector<string> f = splitByTab(line);
        Announcement a;
        a.id = f[0];
        a.title = f[1];
        a.category = f[2];
        a.message = f[3];
        a.postedDate = f[4];
        all.push_back(a);
    }
    return all;
}

vector<Announcement> listAnnouncements() {
    vector<Announcement> all = readAll();
    vector<Announcement> newestFirst(all.rbegin(), all.rend());
    return newestFirst;
}

bool addAnnouncement(const Announcement& announcement) {
    filesystem::create_directories("../data");  // does nothing if it already exists
    ofstream file(ANNOUNCEMENTS_FILE, ios::app);  // app = add to the end
    if (!file) return false;
    file << toLine(announcement) << "\n";
    return true;
}

bool deleteAnnouncement(const string& id) {
    vector<Announcement> all = readAll();
    ofstream out(ANNOUNCEMENTS_FILE, ios::trunc);  // trunc = replace the whole file
    if (!out) return false;
    for (const Announcement& a : all) {
        if (a.id != id) out << toLine(a) << "\n";
    }
    return true;
}
