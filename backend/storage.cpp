#include "storage.h"
#include <fstream>
#include <sstream>
#include <vector>
using namespace std;


static const string USERS_FILE = "../data/users.txt";


static vector<string> splitByTab(const string& line) {
    vector<string> fields;
    stringstream stream(line);
    string field;
    while (getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

bool findUser(const string& login, User& found) {
    ifstream file(USERS_FILE);
    string line;

    while (getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        vector<string> f = splitByTab(line);
        if (f.size() != 7) continue;

        if (f[0] == login) {
            found.login = f[0];
            found.fullName = f[1];
            found.phone = f[2];
            found.address = f[3];
            found.salt = f[4];
            found.passwordHash = f[5];
            found.verification = f[6];
            return true;
        }
    }
    return false;
}

bool addUser(const User& user) {
    ofstream file(USERS_FILE, ios::app);
    if (!file) return false;

    file << user.login << '\t' << user.fullName << '\t' << user.phone << '\t'
        << user.address << '\t' << user.salt << '\t' << user.passwordHash << '\t'
        << user.verification << '\n';
    return true;
}