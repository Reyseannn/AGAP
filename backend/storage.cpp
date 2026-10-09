#include "storage.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
using namespace std;

string getDataDirectory() {
    const char* configuredPath = getenv("AGAP_DATA_DIR");
    if (configuredPath != nullptr && configuredPath[0] != '\0') {
        return configuredPath;
    }
    return "../Data";
}

static string usersFilePath() {
    const char* configuredPath = getenv("AGAP_USERS_FILE");
    if (configuredPath != nullptr && configuredPath[0] != '\0') {
        return configuredPath;
    }
    return getDataDirectory() + "/users.txt";
}

static string complaintsFilePath() {
    const char* configuredPath = getenv("AGAP_COMPLAINTS_FILE");
    if (configuredPath != nullptr && configuredPath[0] != '\0') {
        return configuredPath;
    }
    return getDataDirectory() + "/complaints.txt";
}

static bool ensureParentDirectory(const string& path) {
    filesystem::path parent = filesystem::path(path).parent_path();
    if (parent.empty()) return true;
    error_code error;
    filesystem::create_directories(parent, error);
    return !error;
}

static vector<string> splitByTab(const string& line) {
    vector<string> fields;
    stringstream stream(line);
    string field;
    while (getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    if (!line.empty() && line.back() == '\t') fields.push_back("");
    return fields;
}

static void assignUserFields(const vector<string>& fields, User& user) {
    user.login = fields[0];
    user.fullName = fields.size() > 1 ? fields[1] : "";
    user.phone = fields.size() > 2 ? fields[2] : "";
    user.address = fields.size() > 3 ? fields[3] : "";
    user.salt = fields.size() > 4 ? fields[4] : "";
    user.passwordHash = fields.size() > 5 ? fields[5] : "";
    user.verification = fields.size() > 6 ? fields[6] : "unverified";
    user.idType = fields.size() > 7 ? fields[7] : "";
    user.idPhotoFile = fields.size() > 8 ? fields[8] : "";
}

static void writeUser(ofstream& file, const User& user) {
    file << user.login << '\t' << user.fullName << '\t' << user.phone << '\t'
         << user.address << '\t' << user.salt << '\t' << user.passwordHash << '\t'
         << user.verification << '\t' << user.idType << '\t' << user.idPhotoFile << '\n';
}

bool findUser(const string& login, User& found) {
    ifstream file(usersFilePath());
    string line;

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vector<string> fields = splitByTab(line);
        if (fields.size() < 7) continue;

        if (fields[0] == login) {
            assignUserFields(fields, found);
            return true;
        }
    }
    return false;
}

bool addUser(const User& user) {
    if (!ensureParentDirectory(usersFilePath())) return false;
    ofstream file(usersFilePath(), ios::app);
    if (!file) return false;
    writeUser(file, user);
    return true;
}

bool updateUser(const User& updatedUser) {
    ifstream input(usersFilePath());
    if (!input) return false;

    vector<User> users;
    string line;
    bool updated = false;

    while (getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vector<string> fields = splitByTab(line);
        if (fields.size() < 7) continue;

        User user;
        assignUserFields(fields, user);
        if (user.login == updatedUser.login) {
            user = updatedUser;
            updated = true;
        }
        users.push_back(user);
    }
    input.close();

    if (!updated) return false;

    ofstream output(usersFilePath(), ios::trunc);
    if (!output) return false;
    for (const User& user : users) writeUser(output, user);
    return true;
}

bool addComplaint(const Complaint& complaint) {
    if (!ensureParentDirectory(complaintsFilePath())) return false;
    ofstream file(complaintsFilePath(), ios::app);
    if (!file) return false;

    file << complaint.id << '\t' << complaint.login << '\t' << complaint.category << '\t'
         << complaint.location << '\t' << complaint.details << '\t' << complaint.status << '\t'
         << complaint.createdAt << '\t' << complaint.photoFile << '\n';
    return true;
}

vector<Complaint> findComplaintsByUser(const string& login) {
    vector<Complaint> complaints;
    ifstream file(complaintsFilePath());
    string line;

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vector<string> fields = splitByTab(line);
        if (fields.size() < 7 || fields[1] != login) continue;

        Complaint complaint;
        complaint.id = fields[0];
        complaint.login = fields[1];
        complaint.category = fields[2];
        complaint.location = fields[3];
        complaint.details = fields[4];
        complaint.status = fields[5];
        complaint.createdAt = fields[6];
        complaint.photoFile = fields.size() > 7 ? fields[7] : "";
        complaints.push_back(complaint);
    }
    return complaints;
}


vector<User> listUsers() {
    vector<User> users;
    ifstream file(usersFilePath());
    string line;

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vector<string> fields = splitByTab(line);
        if (fields.size() < 7) continue;

        User user;
        assignUserFields(fields, user);
        users.push_back(user);
    }
    return users;
}
