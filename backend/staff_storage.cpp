#include "staff_storage.h"
#include "auth.h"
#include "file_util.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace std;

// The file is found relative to the backend folder, the same way users.txt is.
static const string STAFF_FILE = "../data/staff.txt";

// Cuts one line of the file into its fields at every tab character.
static vector<string> splitByTab(const string& line) {
    vector<string> fields;
    stringstream stream(line);
    string field;
    while (getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

bool findStaff(const string& login, Staff& found) {
    ifstream file(STAFF_FILE);
    string line;

    while (getline(file, line)) {
        cleanLine(line);

        vector<string> f = splitByTab(line);
        if (f.size() != 6) continue;  // skip broken lines

        if (f[0] == login) {
            found.login = f[0];
            found.fullName = f[1];
            found.role = f[2];
            found.salt = f[3];
            found.passwordHash = f[4];
            found.status = f[5];
            return true;
        }
    }
    return false;
}

bool addStaff(const Staff& staff) {
    return appendLine(STAFF_FILE, staff.login + '\t' + staff.fullName + '\t' + staff.role + '\t' +
                                   staff.salt + '\t' + staff.passwordHash + '\t' + staff.status);
}

void ensureDefaultAdmin() {
    ifstream check(STAFF_FILE);
    if (check.good()) return;  // the file already exists, don't overwrite staff accounts

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
        cout << "Sign in at /admin, then change this password (Step 8: Settings)." << endl;
        cout << "----------------------------------------------------------" << endl;
    } else {
        cout << "WARNING: could not create the starter admin account (see the error above)." << endl;
    }
}