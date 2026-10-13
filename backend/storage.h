#ifndef STORAGE_H
#define STORAGE_H

#include <string>
#include <vector>
using namespace std;

struct User {
    string login;
    string fullName;
    string phone;
    string address;
    string salt;
    string passwordHash;
    string verification;
    string rejectionReason;
    string idType;
};

bool findUser(const string& login, User& found);

vector<User> listAllUsers();

bool addUser(const User& user);

bool saveVerificationRequest(const string& login, const string& phone, const string& address,
                              const string& idType, const string& newStatus);

bool setVerificationDecision(const string& login, const string& newStatus, const string& reason);

struct VerificationCounts {
    int unverified = 0;
    int pending = 0;
    int verified = 0;
    int rejected = 0;
};
VerificationCounts countResidentsByVerification();

#endif