#include "staff_storage.h"
#include "auth.h"
#include "file_util.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <vector>
using namespace std;

static const string STAFF_FILE = "../data/staff.txt";

static vector<string> splitByTab(const string& line) {
    vector<string> fields;
    stringstream stream(line);
    string field;
    while (getline(stream, field, '\t')) fields.push_back(field);
    return fields;
}

static Staff fieldsToStaff(const vector<string>& fields) {
    Staff staff;
    staff.login = fields[0];
    staff.fullName = fields[1];
    staff.role = fields[2];
    staff.salt = fields[3];
    staff.passwordHash = fields[4];
    staff.status = fields[5];
    return staff;
}

static string staffToLine(const Staff& staff) {
    return staff.login + '\t' + staff.fullName + '\t' + staff.role + '\t' +
           staff.salt + '\t' + staff.passwordHash + '\t' + staff.status;
}

bool findStaff(const string& login, Staff& found) {
    ifstream file(STAFF_FILE);
    string line;
    while (getline(file, line)) {
        cleanLine(line);
        vector<string> fields = splitByTab(line);
        if (fields.size() == 6 && fields[0] == login) {
            found = fieldsToStaff(fields);
            return true;
        }
    }
    return false;
}

bool addStaff(const Staff& staff) {
    return appendLine(STAFF_FILE, staffToLine(staff));
}

vector<Staff> listAllStaff() {
    vector<Staff> staffList;
    ifstream file(STAFF_FILE);
    string line;
    while (getline(file, line)) {
        cleanLine(line);
        if (line.empty()) continue;
        vector<string> fields = splitByTab(line);
        if (fields.size() == 6) staffList.push_back(fieldsToStaff(fields));
    }
    return staffList;
}

static bool updateOneStaff(const string& login, const function<void(Staff&)>& change) {
    ifstream in(STAFF_FILE);
    if (!in) return false;

    vector<string> lines;
    string line;
    bool found = false;
    while (getline(in, line)) {
        cleanLine(line);
        if (line.empty()) continue;
        vector<string> fields = splitByTab(line);
        if (fields.size() == 6 && fields[0] == login && !found) {
            Staff staff = fieldsToStaff(fields);
            change(staff);
            lines.push_back(staffToLine(staff));
            found = true;
        } else {
            lines.push_back(line);
        }
    }
    in.close();
    if (!found) return false;

    ofstream out(STAFF_FILE, ios::trunc);
    if (!out) return false;
    for (const string& savedLine : lines) out << savedLine << '\n';
    out.flush();
    return out.good();
}

bool setStaffStatus(const string& login, const string& newStatus) {
    if (newStatus != "active" && newStatus != "inactive") return false;
    return updateOneStaff(login, [&](Staff& staff) { staff.status = newStatus; });
}

bool setStaffPassword(const string& login, const string& newSalt, const string& newPasswordHash) {
    if (newSalt.empty() || newPasswordHash.empty()) return false;
    return updateOneStaff(login, [&](Staff& staff) {
        staff.salt = newSalt;
        staff.passwordHash = newPasswordHash;
    });
}

void ensureDefaultAdmin() {
    ifstream check(STAFF_FILE);
    if (check.good()) return;

    Staff admin;
    admin.login = "admin@barangay.gov";
    admin.fullName = "Barangay Administrator";
    admin.role = "admin";
    admin.salt = makeSalt();
    admin.passwordHash = hashPassword(admin.salt, "admin123");
    admin.status = "active";

    if (addStaff(admin)) {
        cout << "----------------------------------------------------------" << endl;
        cout << "No staff accounts found. Created a starter admin account:" << endl;
        cout << "  Email:    " << admin.login << endl;
        cout << "  Password: admin123" << endl;
        cout << "Sign in at /admin, then change this password in Settings." << endl;
        cout << "----------------------------------------------------------" << endl;
    } else {
        cout << "WARNING: could not create the starter admin account (see the error above)." << endl;
    }
}
