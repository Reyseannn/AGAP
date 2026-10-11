#ifndef STAFF_STORAGE_H
#define STAFF_STORAGE_H

#include <string>
using namespace std;

// One barangay staff/admin account. Each field is one column in data/staff.txt.
struct Staff {
    string login;         // email (saved in lowercase)
    string fullName;
    string role;           // "admin" or "staff"
    string salt;           // random text mixed into the password before hashing
    string passwordHash;   // the hashed password (never the real password)
    string status;         // "active" or "inactive"
};

// Looks for a staff account by login. Returns true and fills "found" if it exists.
bool findStaff(const string& login, Staff& found);

// Adds a new staff account at the end of data/staff.txt.
bool addStaff(const Staff& staff);

// If data/staff.txt doesn't exist yet, creates it with one starter admin account
// and prints the login/password to the console (never to the browser or any
// frontend file). Run once automatically when the server starts.
void ensureDefaultAdmin();

#endif