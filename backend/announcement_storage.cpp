#include "announcement_storage.h"
#include "file_util.h"
#include <filesystem>
#include <fstream>
#include <sstream>
using namespace std;

static const string ANNOUNCEMENTS_FILE = "../data/announcements.txt";

static const size_t FIELD_COUNT = 5;

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

static vector<Announcement> readAll() {
    vector<Announcement> all;
    ifstream file(ANNOUNCEMENTS_FILE);
    string line;
    while (getline(file, line)) {
        cleanLine(line);
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
    return appendLine(ANNOUNCEMENTS_FILE, toLine(announcement));
}

bool deleteAnnouncement(const string& id) {
    vector<Announcement> all = readAll();
    ofstream out(ANNOUNCEMENTS_FILE, ios::trunc);
    if (!out) return false;
    for (const Announcement& a : all) {
        if (a.id != id) out << toLine(a) << "\n";
    }
    return true;
}
