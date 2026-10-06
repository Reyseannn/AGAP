#include "auth.h"
#include "picosha2.h"
#include <map>
#include <mutex>
#include <random>
using namespace std;

static map<string, string> sessions;
static mutex sessionLock;

static string randomHex(int length) {
    static random_device device;
    static mt19937_64 generator(device());
    const char* digits = "0123456789abcdef";

    string result;
    for (int i = 0; i < length; i++) {
        result += digits[generator() % 16];
    }
    return result;
}

string makeSalt() {
    return randomHex(16);
}

string hashPassword(const string& salt, const string& password) {
    return picosha2::hash256_hex_string(salt + password);
}

string createSession(const string& login) {
    string token = randomHex(32);
    lock_guard<mutex> lock(sessionLock);
    sessions[token] = login;
    return token;
}

string loginFromSession(const string& token) {
    lock_guard<mutex> lock(sessionLock);
    auto found = sessions.find(token);
    if (found == sessions.end()) return "";
    return found->second;
}

void deleteSession(const string& token) {
    lock_guard<mutex> lock(sessionLock);
    sessions.erase(token);
}