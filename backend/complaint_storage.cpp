#include "complaint_storage.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace std;

static const string COMPLAINTS_FILE = "../data/complaints.txt";

// Columns: id, login, category, location, description, status, filedDate, photoFile
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
        if (!line.empty() && line.back() == '\r') line.pop_back();
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
    filesystem::create_directories("../data");
    ofstream file(COMPLAINTS_FILE, ios::app);
    if (!file) return false;
    file << c.id << '\t' << c.login << '\t' << c.category << '\t' << c.location << '\t'
         << c.description << '\t' << c.status << '\t' << c.filedDate << '\t' << c.photoFile << "\n";
    return true;
}

vector<Complaint> listComplaintsFor(const string& login) {
    vector<Complaint> mine;
    vector<Complaint> all = readAll();
    for (size_t i = all.size(); i > 0; i--) {  // walk backwards = newest first
        if (all[i - 1].login == login) mine.push_back(all[i - 1]);
    }
    return mine;
}
