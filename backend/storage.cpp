#include "storage.h"
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>
using namespace std;


// The file is found relative to the backend folder, the same way "../frontend" is.
static const string USERS_FILE = "../data/users.txt";

// The columns, in order, that each line of users.txt holds.
static const size_t FIELD_COUNT = 9;  // login, fullName, phone, address, salt,
                                       // passwordHash, verification, rejectionReason, idType

// Cuts one line of the file into its fields at every tab character, then pads
// it with empty fields up to FIELD_COUNT if it's short. Padding matters
// because rejectionReason and idType were added to the file format after
// some residents may have already signed up with the older 7-column format —
// this lets those older rows keep working instead of being silently skipped.
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

// Turns one already-split, already-padded line into a User.
static User fieldsToUser(const vector<string>& f) {
    User user;
    user.login = f[0];
    user.fullName = f[1];
    user.phone = f[2];
    user.address = f[3];
    user.salt = f[4];
    user.passwordHash = f[5];
    user.verification = f[6];
    user.rejectionReason = f[7];
    user.idType = f[8];
    return user;
}

// Turns a User back into one tab-separated line, ready to write to the file.
static string userToLine(const User& user) {
    return user.login + '\t' + user.fullName + '\t' + user.phone + '\t' + user.address + '\t' +
           user.salt + '\t' + user.passwordHash + '\t' + user.verification + '\t' +
           user.rejectionReason + '\t' + user.idType;
}

bool findUser(const string& login, User& found) {
    ifstream file(USERS_FILE);
    string line;

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // Notepad adds \r on Windows
        if (line.empty()) continue;

        vector<string> f = splitByTab(line);
        if (f[0] == login) {
            found = fieldsToUser(f);
            return true;
        }
    }
    return false;
}

vector<User> listAllUsers() {
    vector<User> users;
    ifstream file(USERS_FILE);
    string line;

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        users.push_back(fieldsToUser(splitByTab(line)));
    }
    return users;
}

bool addUser(const User& user) {
    ofstream file(USERS_FILE, ios::app);
    if (!file) return false;

    file << userToLine(user) << '\n';
    return true;
}

// Shared by saveVerificationRequest and setVerificationDecision: reads every
// line, lets the given function change the one matching "login", and rewrites
// the whole file. A text file can't edit a single line in place, so every
// update means reading it all and writing it all back.
static bool updateOneUser(const string& login, const std::function<void(User&)>& change) {
    ifstream in(USERS_FILE);
    if (!in) return false;

    vector<string> lines;
    string line;
    bool found = false;

    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        vector<string> f = splitByTab(line);
        if (f[0] == login) {
            User user = fieldsToUser(f);
            change(user);
            lines.push_back(userToLine(user));
            found = true;
        } else {
            lines.push_back(userToLine(fieldsToUser(f)));  // also upgrades old 7-column lines
        }
    }
    in.close();
    if (!found) return false;

    ofstream out(USERS_FILE, ios::trunc);  // trunc = replace the whole file
    if (!out) return false;
    for (const string& l : lines) out << l << '\n';
    return true;
}

bool saveVerificationRequest(const string& login, const string& phone, const string& address,
                              const string& idType, const string& newStatus) {
    return updateOneUser(login, [&](User& user) {
        user.phone = phone;
        user.address = address;
        user.idType = idType;
        user.verification = newStatus;
        user.rejectionReason = "";  // a fresh request clears any old rejection reason
    });
}

bool setVerificationDecision(const string& login, const string& newStatus, const string& reason) {
    return updateOneUser(login, [&](User& user) {
        user.verification = newStatus;
        user.rejectionReason = reason;
    });
}

VerificationCounts countResidentsByVerification() {
    VerificationCounts counts;
    for (const User& user : listAllUsers()) {
        if (user.verification == "unverified") counts.unverified++;
        else if (user.verification == "pending") counts.pending++;
        else if (user.verification == "verified") counts.verified++;
        else if (user.verification == "rejected") counts.rejected++;
    }
    return counts;
}