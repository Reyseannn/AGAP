#ifndef AUTH_H
#define AUTH_H

#include <string>
using namespace std;

string makeSalt();
string hashPassword(const string& salt, const string& password);

string createSession(const string& login);
string loginFromSession(const string& token);
void deleteSession(const string& token);

#endif