#ifndef STORAGE_H
#define STORAGE_H

#include <string>
#include <vector>
using namespace std;

// One resident account. Each field is one column in data/users.txt.
struct User {
    string login;           // email or username (saved in lowercase)
    string fullName;
    string phone;           // empty until the resident fills it in
    string address;         // empty until the resident fills it in
    string salt;            // random text mixed into the password before hashing
    string passwordHash;    // the hashed password (never the real password)
    string verification;    // unverified, pending, verified or rejected
    string rejectionReason; // only set when verification == "rejected"
    string idType;          // e.g. "Barangay ID" — from the Verify form's ID type field
};

// Looks for a user by login. Returns true and fills "found" if the user exists.
bool findUser(const string& login, User& found);

// Returns every resident account, in the order they appear in the file.
// Used by the admin Residents page.
vector<User> listAllUsers();

// Adds a new user at the end of data/users.txt. Returns false if the file can't be written.
bool addUser(const User& user);

// Saves the resident's phone, address and ID type, and sets their verification
// status to "pending" (used when a resident submits the Verify form). Always
// clears any previous rejection reason, since this is a brand new request.
// Rewrites the whole file, since a text file can't edit one line in place.
bool saveVerificationRequest(const string& login, const string& phone, const string& address,
                              const string& idType, const string& newStatus);

// Used by admin staff to approve or reject a resident's verification request.
// Does NOT touch phone/address/idType — only the status and (for a rejection)
// the reason shown back to the resident.
bool setVerificationDecision(const string& login, const string& newStatus, const string& reason);

// How many residents are in each verification state. Used by the admin
// dashboard and the (future) Residents page, read straight from users.txt.
struct VerificationCounts {
    int unverified = 0;
    int pending = 0;
    int verified = 0;
    int rejected = 0;
};
VerificationCounts countResidentsByVerification();

#endif