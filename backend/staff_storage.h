#ifndef STAFF_STORAGE_H
#define STAFF_STORAGE_H

#include <string>
using namespace std;

struct Staff {
    string login;
    string fullName;
    string role;
    string salt;
    string passwordHash;
    string status;
};

bool findStaff(const string& login, Staff& found);

bool addStaff(const Staff& staff);

void ensureDefaultAdmin();

#endif