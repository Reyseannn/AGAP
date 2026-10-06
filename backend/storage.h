#ifndef STORAGE_H
#define STORAGE_H

#include <string>
using namespace std;


struct User {
    string login;
    string fullName;
    string phone;
    string address;
    string salt;
    string passwordHash;
    string verification;
};


bool findUser(const string& login, User& found);

bool addUser(const User& user);

#endif