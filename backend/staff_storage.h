#ifndef STAFF_STORAGE_H
#define STAFF_STORAGE_H

#include <string>
#include <vector>
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
vector<Staff> listAllStaff();
bool setStaffStatus(const string& login, const string& newStatus);
bool setStaffPassword(const string& login, const string& newSalt, const string& newPasswordHash);
void ensureDefaultAdmin();

#endif
