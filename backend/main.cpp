#include "httplib.h"
#include "auth.h"
#include "storage.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace std;


static string readFile(const string& path) {
    ifstream file(path);
    stringstream text;
    text << file.rdbuf();
    return text.str();
}

static string replaceAll(string text, const string& from, const string& to) {
    size_t position = 0;
    while ((position = text.find(from, position)) != string::npos) {
        text.replace(position, from.length(), to);
        position += to.length();
    }
    return text;
}

static string escapeHtml(string text) {
    text = replaceAll(text, "&", "&amp;");
    text = replaceAll(text, "<", "&lt;");
    text = replaceAll(text, ">", "&gt;");
    text = replaceAll(text, "\"", "&quot;");
    return text;
}

static string cleanField(string text) {
    for (char& c : text) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    size_t start = text.find_first_not_of(' ');
    if (start == string::npos) return "";
    size_t end = text.find_last_not_of(' ');
    return text.substr(start, end - start + 1);
}

static string toLower(string text) {
    transform(text.begin(), text.end(), text.begin(), ::tolower);
    return text;
}

static string getSessionToken(const httplib::Request& req) {
    string cookies = req.get_header_value("Cookie");
    size_t start = cookies.find("session=");
    if (start == string::npos) return "";
    start += 8;  // length of "session="
    size_t end = cookies.find(';', start);
    if (end == string::npos) return cookies.substr(start);
    return cookies.substr(start, end - start);
}

static void showAuthPage(httplib::Response& res, const string& file, const string& error) {
    string page = readFile("../frontend/" + file);
    string banner = "";
    if (!error.empty()) {
        banner = "<div class=\"alert-error\">" + escapeHtml(error) + "</div>";
    }
    page = replaceAll(page, "<!--ERROR-->", banner);
    res.set_content(page, "text/html");
}

static void startSession(httplib::Response& res, const string& login) {
    string token = createSession(login);
    res.set_header("Set-Cookie", "session=" + token + "; Path=/; HttpOnly; SameSite=Lax");
    res.set_redirect("/dashboard");
}


int main() {
    httplib::Server server;

    server.set_mount_point("/", "../frontend");

    server.Post("/signup", [](const httplib::Request& req, httplib::Response& res) {
        string fullName = cleanField(req.get_param_value("fullname"));
        string login = toLower(cleanField(req.get_param_value("login")));
        string password = req.get_param_value("password");
        string confirm = req.get_param_value("confirm");

        if (fullName.empty() || login.empty() || password.empty()) {
            showAuthPage(res, "signup.html", "Please fill in every field.");
            return;
        }
        if (password.length() < 6) {
            showAuthPage(res, "signup.html", "Password must be at least 6 characters.");
            return;
        }
        if (password != confirm) {
            showAuthPage(res, "signup.html", "The two passwords do not match.");
            return;
        }

        User existing;
        if (findUser(login, existing)) {
            showAuthPage(res, "signup.html", "That email or username is already registered.");
            return;
        }

        User user;
        user.login = login;
        user.fullName = fullName;
        user.salt = makeSalt();
        user.passwordHash = hashPassword(user.salt, password);
        user.verification = "unverified";

        if (!addUser(user)) {
            showAuthPage(res, "signup.html", "Could not save your account. Does the data folder exist?");
            return;
        }
        startSession(res, login);
    });

    server.Post("/signin", [](const httplib::Request& req, httplib::Response& res) {
        string login = toLower(cleanField(req.get_param_value("login")));
        string password = req.get_param_value("password");

        User user;
        bool found = findUser(login, user);

        if (!found || hashPassword(user.salt, password) != user.passwordHash) {
            showAuthPage(res, "signin.html", "Wrong email or password. Please try again.");
            return;
        }
        startSession(res, login);
    });

    server.Get("/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));

        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }

        string firstName = user.fullName.substr(0, user.fullName.find(' '));
        string page = readFile("../frontend/dashboard.html");
        page = replaceAll(page, "{{firstname}}", escapeHtml(firstName));
        page = replaceAll(page, "{{login}}", escapeHtml(user.login));
        res.set_content(page, "text/html");
    });

    server.Get("/signout", [](const httplib::Request& req, httplib::Response& res) {
        deleteSession(getSessionToken(req));
        res.set_header("Set-Cookie", "session=; Path=/; Max-Age=0");
        res.set_redirect("/");
    });

    cout << "AGAP is running at http://localhost:8080" << endl;
    server.listen("localhost", 8080);
}