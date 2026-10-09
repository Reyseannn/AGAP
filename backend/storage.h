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
    string idType;
    string idPhotoFile;
};

struct Complaint {
    string id;
    string login;
    string category;
    string location;
    string details;
    string status;
    string createdAt;
    string photoFile;
};

string getDataDirectory();
bool findUser(const string& login, User& found);
bool addUser(const User& user);
bool updateUser(const User& user);
bool addComplaint(const Complaint& complaint);
vector<Complaint> findComplaintsByUser(const string& login);

#endif
