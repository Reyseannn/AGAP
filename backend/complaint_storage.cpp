#include "complaint_storage.h"
#include "file_util.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace std;

static const string COMPLAINTS_FILE = "../data/complaints.txt";

static const size_t FIELD_COUNT = 8;

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

static vector<Complaint> readAll() {
    vector<Complaint> all;
    ifstream file(COMPLAINTS_FILE);
    string line;
    while (getline(file, line)) {
        cleanLine(line);
        if (line.empty()) continue;
        vector<string> f = splitByTab(line);
        Complaint c;
        c.id = f[0];
        c.login = f[1];
        c.category = f[2];
        c.location = f[3];
        c.description = f[4];
        c.status = f[5];
        c.filedDate = f[6];
        c.photoFile = f[7];
        all.push_back(c);
    }
    return all;
}

string nextComplaintId() {
    size_t count = readAll().size() + 1;
    stringstream id;
    id << "AGAP-" << setw(4) << setfill('0') << count;
    return id.str();
}

bool addComplaint(const Complaint& c) {
    return appendLine(COMPLAINTS_FILE, c.id + '\t' + c.login + '\t' + c.category + '\t' + c.location + '\t' +
                                         c.description + '\t' + c.status + '\t' + c.filedDate + '\t' + c.photoFile);
}

vector<Complaint> listComplaintsFor(const string& login) {
    vector<Complaint> mine;
    vector<Complaint> all = readAll();
    for (size_t i = all.size(); i > 0; i--) {
        if (all[i - 1].login == login) mine.push_back(all[i - 1]);
    }
    return mine;
}

vector<Complaint> listAllComplaints() {
    vector<Complaint> all = readAll();
    vector<Complaint> newestFirst(all.rbegin(), all.rend());
    return newestFirst;
}

bool findComplaint(const string& id, Complaint& found) {
    for (const Complaint& c : readAll()) {
        if (c.id == id) {
            found = c;
            return true;
        }
    }
    return false;
}

bool updateComplaintStatus(const string& id, const string& newStatus) {
    vector<Complaint> all = readAll();
    bool changed = false;
    for (Complaint& c : all) {
        if (c.id == id) {
            c.status = newStatus;
            changed = true;
        }
    }
    if (!changed) return false;

    ofstream out(COMPLAINTS_FILE, ios::trunc);
    if (!out) return false;
    for (const Complaint& c : all) {
        out << c.id << '\t' << c.login << '\t' << c.category << '\t' << c.location << '\t'
            << c.description << '\t' << c.status << '\t' << c.filedDate << '\t' << c.photoFile << "\n";
    }
    return true;
}
