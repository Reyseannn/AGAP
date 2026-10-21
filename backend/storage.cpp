#include "storage.h"
#include "file_util.h"
#include <fstream>
#include <functional>
#include <sstream>
#include <vector>
using namespace std;


static const string USERS_FILE = "../data/users.txt";

static const size_t FIELD_COUNT = 9;

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

static string userToLine(const User& user) {
    return user.login + '\t' + user.fullName + '\t' + user.phone + '\t' + user.address + '\t' +
           user.salt + '\t' + user.passwordHash + '\t' + user.verification + '\t' +
           user.rejectionReason + '\t' + user.idType;
}

bool findUser(const string& login, User& found) {
    ifstream file(USERS_FILE);
    string line;

    while (getline(file, line)) {
        cleanLine(line);
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
        cleanLine(line);
        if (line.empty()) continue;

        users.push_back(fieldsToUser(splitByTab(line)));
    }
    return users;
}

bool addUser(const User& user) {
    return appendLine(USERS_FILE, userToLine(user));
}

static bool updateOneUser(const string& login, const std::function<void(User&)>& change) {
    ifstream in(USERS_FILE);
    if (!in) return false;

    vector<string> lines;
    string line;
    bool found = false;

    while (getline(in, line)) {
        cleanLine(line);
        if (line.empty()) continue;

        vector<string> f = splitByTab(line);
        if (f[0] == login) {
            User user = fieldsToUser(f);
            change(user);
            lines.push_back(userToLine(user));
            found = true;
        } else {
            lines.push_back(userToLine(fieldsToUser(f)));
        }
    }
    in.close();
    if (!found) return false;

    ofstream out(USERS_FILE, ios::trunc);
    if (!out) return false;
    for (const string& l : lines) out << l << '\n';
    return true;
}

bool saveVerificationRequest(const string& login, const string& phone, const string& address,
                              const string& idType, const string& newStatus) {
    return updateOneUser(login, [&](User& user) {
        // A submitted request must never demote a resident already approved by staff.
        if (user.verification == "verified" || newStatus != "pending") return;
        user.phone = phone;
        user.address = address;
        user.idType = idType;
        user.verification = "pending";
        user.rejectionReason = "";
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
