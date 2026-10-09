#include "httplib.h"
#include "auth.h"
#include "storage.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace std;
namespace fs = std::filesystem;

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
    text = replaceAll(text, "'", "&#39;");
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
    transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return text;
}

static string getSessionToken(const httplib::Request& req) {
    string cookies = req.get_header_value("Cookie");
    size_t start = cookies.find("session=");
    if (start == string::npos) return "";
    start += 8;
    size_t end = cookies.find(';', start);
    if (end == string::npos) return cookies.substr(start);
    return cookies.substr(start, end - start);
}

static string loggedInUser(const httplib::Request& req) {
    return loginFromSession(getSessionToken(req));
}

static void showAuthPage(httplib::Response& res, const string& file, const string& error) {
    string page = readFile("../frontend/" + file);
    string banner;
    if (!error.empty()) {
        banner = "<div class=\"alert-error\" role=\"alert\">" + escapeHtml(error) + "</div>";
    }
    page = replaceAll(page, "<!--ERROR-->", banner);
    res.set_content(page, "text/html; charset=utf-8");
}

static void startSession(httplib::Response& res, const string& login) {
    string token = createSession(login);
    res.set_header("Set-Cookie", "session=" + token + "; Path=/; HttpOnly; SameSite=Lax");
    res.set_redirect("/dashboard");
}

static string safeStatus(const User& user) {
    string status = toLower(user.verification);
    if (status == "verified") return "verified";
    if (status == "pending") return "pending";
    if (status == "rejected") return "rejected";
    return "unverified";
}

static string verificationLabel(const string& status) {
    if (status == "verified") return "Verified resident";
    if (status == "pending") return "Pending verification";
    if (status == "rejected") return "Not verified";
    return "Not verified";
}

static string dashboardNotice(const string& status) {
    if (status == "verified") {
        return "<div class=\"verification-notice notice-success\">&#10003; Your account is verified. You can file a complaint.</div>";
    }
    if (status == "pending") {
        return "<div class=\"verification-notice notice-pending\">Your verification is pending. Staff are checking your ID. You can file complaints once it is approved.</div>";
    }
    if (status == "rejected") {
        return "<div class=\"verification-notice notice-error\">Your ID verification was not approved. Please upload a clearer photo and submit it again.</div>";
    }
    return "<div class=\"verification-notice notice-pending\">Verify your account to file a complaint. Upload a valid ID with your phone number and address.</div>";
}

static string complaintButton(const string& status) {
    if (status == "verified") {
        return "<a class=\"btn btn-yellow\" href=\"/filecomplaint\">File a complaint</a>";
    }
    return "<a class=\"btn btn-yellow disabled-action\" href=\"/verify\" aria-disabled=\"true\">File a complaint</a>";
}

static string renderComplaints(const string& login) {
    vector<Complaint> complaints = findComplaintsByUser(login);
    if (complaints.empty()) {
        return "<div class=\"empty-complaints\"><div class=\"empty-complaints-icon\" aria-hidden=\"true\">&#9776;</div><h3>No complaints yet</h3><p>Your reports and their status will show up here.</p></div>";
    }

    string html;
    for (const Complaint& complaint : complaints) {
        html += "<article class=\"complaint-row complaint-pending\"><div class=\"complaint-copy\"><h3>"
            + escapeHtml(complaint.category) + "</h3><p>" + escapeHtml(complaint.location)
            + " &middot; " + escapeHtml(complaint.createdAt) + " &middot; " + escapeHtml(complaint.id)
            + "</p><p>" + escapeHtml(complaint.details) + "</p></div><span class=\"badge badge-pending\">"
            + escapeHtml(complaint.status) + "</span></article>";
    }
    return html;
}

static string renderDashboard(User& user) {
    string page = readFile("../frontend/dashboard.html");
    string firstName = user.fullName.substr(0, user.fullName.find(' '));
    string status = safeStatus(user);
    page = replaceAll(page, "{{firstname}}", escapeHtml(firstName));
    page = replaceAll(page, "{{login}}", escapeHtml(user.login));
    page = replaceAll(page, "{{phone}}", escapeHtml(user.phone.empty() ? "Phone not provided" : user.phone));
    page = replaceAll(page, "{{address}}", escapeHtml(user.address.empty() ? "Address not provided" : user.address));
    page = replaceAll(page, "{{verification_label}}", verificationLabel(status));
    page = replaceAll(page, "{{verification_notice}}", dashboardNotice(status));
    page = replaceAll(page, "{{complaint_action}}", complaintButton(status));
    page = replaceAll(page, "{{complaints}}", renderComplaints(user.login));
    return page;
}

static string renderVerifyPage(User& user, const string& error) {
    string page = readFile("../frontend/verify.html");
    string status = safeStatus(user);
    string notice;
    if (!error.empty()) {
        notice = "<div class=\"alert-error\" role=\"alert\">" + escapeHtml(error) + "</div>";
    } else if (status == "pending") {
        notice = "<div class=\"verification-notice notice-pending\" role=\"status\">Your ID was submitted. Staff are reviewing your verification.</div>";
    } else if (status == "verified") {
        notice = "<div class=\"verification-notice notice-success\" role=\"status\">Your account is verified. You can now file complaints.</div>";
    } else if (status == "rejected") {
        notice = "<div class=\"verification-notice notice-error\" role=\"alert\">Your previous submission was not approved. Upload a clearer ID photo to try again.</div>";
    }
    page = replaceAll(page, "{{verification_notice}}", notice);
    page = replaceAll(page, "{{phone}}", escapeHtml(user.phone));
    page = replaceAll(page, "{{address}}", escapeHtml(user.address));
    page = replaceAll(page, "{{id_type}}", escapeHtml(user.idType));
    page = replaceAll(page, "{{verification_status}}", escapeHtml(verificationLabel(status)));
    return page;
}

static string renderComplaintPage(User& user, const string& error) {
    string page = readFile("../frontend/filecomplaint.html");
    string errorBanner;
    if (!error.empty()) {
        errorBanner = "<div class=\"alert-error\" role=\"alert\">" + escapeHtml(error) + "</div>";
    }
    page = replaceAll(page, "{{complaint_error}}", errorBanner);
    page = replaceAll(page, "{{fullname}}", escapeHtml(user.fullName));
    page = replaceAll(page, "{{phone}}", escapeHtml(user.phone));
    page = replaceAll(page, "{{address}}", escapeHtml(user.address));
    return page;
}

static bool validImageType(const httplib::MultipartFormData& file, string& extension) {
    if (file.content_type == "image/jpeg" || file.content_type == "image/jpg") {
        extension = ".jpg";
        return true;
    }
    if (file.content_type == "image/png") {
        extension = ".png";
        return true;
    }
    return false;
}

static bool saveUpload(const httplib::MultipartFormData& file, const string& folder,
                       const string& prefix, string& savedName) {
    string extension;
    if (!validImageType(file, extension) || file.content.size() > 5 * 1024 * 1024) {
        return false;
    }

    fs::path directory = fs::path(getDataDirectory()) / folder;
    error_code error;
    fs::create_directories(directory, error);
    if (error) return false;

    savedName = prefix + "-" + makeSalt() + extension;
    ofstream output(directory / savedName, ios::binary);
    if (!output) return false;
    output.write(file.content.data(), static_cast<streamsize>(file.content.size()));
    return output.good();
}

static string currentDate() {
    time_t now = time(nullptr);
    tm* local = localtime(&now);
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", local);
    return buffer;
}

int main() {
    httplib::Server server;
    server.set_payload_max_length(6 * 1024 * 1024);
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
        user.verification = "unverified";
        user.salt = makeSalt();
        user.passwordHash = hashPassword(user.salt, password);

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
        if (!findUser(login, user) || hashPassword(user.salt, password) != user.passwordHash) {
            showAuthPage(res, "signin.html", "Wrong email or password. Please try again.");
            return;
        }
        startSession(res, login);
    });

    server.Get("/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        string login = loggedInUser(req);
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        res.set_content(renderDashboard(user), "text/html; charset=utf-8");
    });

    server.Get("/verify", [](const httplib::Request& req, httplib::Response& res) {
        string login = loggedInUser(req);
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        res.set_content(renderVerifyPage(user, ""), "text/html; charset=utf-8");
    });

    server.Post("/verify", [](const httplib::Request& req, httplib::Response& res) {
        string login = loggedInUser(req);
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }

        string phone = cleanField(req.get_param_value("phone"));
        string address = cleanField(req.get_param_value("address"));
        string idType = cleanField(req.get_param_value("id-type"));
        if (phone.empty() || address.empty() || idType.empty()) {
            res.set_content(renderVerifyPage(user, "Enter your mobile number and address, then choose an ID type."), "text/html; charset=utf-8");
            res.status = 400;
            return;
        }
        if (!req.has_file("id-photo")) {
            res.set_content(renderVerifyPage(user, "Upload a photo of the front of your valid ID."), "text/html; charset=utf-8");
            res.status = 400;
            return;
        }

        const auto& photo = req.get_file_value("id-photo");
        string savedPhoto;
        if (!saveUpload(photo, "verification", "resident-id", savedPhoto)) {
            res.set_content(renderVerifyPage(user, "Use a JPG or PNG image up to 5 MB."), "text/html; charset=utf-8");
            res.status = 400;
            return;
        }

        user.phone = phone;
        user.address = address;
        user.idType = idType;
        user.idPhotoFile = savedPhoto;
        user.verification = "pending";
        if (!updateUser(user)) {
            res.set_content(renderVerifyPage(user, "Your information could not be saved. Please try again."), "text/html; charset=utf-8");
            res.status = 500;
            return;
        }
        res.set_redirect("/dashboard");
    });

    server.Get("/filecomplaint", [](const httplib::Request& req, httplib::Response& res) {
        string login = loggedInUser(req);
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        if (safeStatus(user) != "verified") {
            res.set_redirect("/verify");
            return;
        }
        res.set_content(renderComplaintPage(user, ""), "text/html; charset=utf-8");
    });

    server.Post("/complaints", [](const httplib::Request& req, httplib::Response& res) {
        string login = loggedInUser(req);
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        if (safeStatus(user) != "verified") {
            res.set_redirect("/verify");
            return;
        }

        string category = cleanField(req.get_param_value("category"));
        string location = cleanField(req.get_param_value("location"));
        string details = cleanField(req.get_param_value("description"));
        const vector<string> categories = {"garbage", "noise", "lighting", "flooding", "others"};
        if (find(categories.begin(), categories.end(), category) == categories.end()
            || location.empty() || details.empty()) {
            res.status = 400;
            res.set_content(renderComplaintPage(user, "Choose a category and enter the location and details."), "text/html; charset=utf-8");
            return;
        }

        Complaint complaint;
        complaint.id = "AGAP-" + toLower(makeSalt().substr(0, 8));
        complaint.login = login;
        complaint.category = category;
        complaint.location = location;
        complaint.details = details;
        complaint.status = "Pending";
        complaint.createdAt = currentDate();

        if (req.has_file("photo") && !req.get_file_value("photo").content.empty()) {
            string savedPhoto;
            if (!saveUpload(req.get_file_value("photo"), "complaints", complaint.id, savedPhoto)) {
                res.status = 400;
                res.set_content(renderComplaintPage(user, "Use a JPG or PNG photo up to 5 MB, or remove the photo and submit without it."), "text/html; charset=utf-8");
                return;
            }
            complaint.photoFile = savedPhoto;
        }

        if (!addComplaint(complaint)) {
            res.status = 500;
            res.set_content(renderComplaintPage(user, "Your complaint could not be saved. Please try again."), "text/html; charset=utf-8");
            return;
        }
        res.set_redirect("/dashboard");
    });

    server.Get("/signout", [](const httplib::Request& req, httplib::Response& res) {
        deleteSession(getSessionToken(req));
        res.set_header("Set-Cookie", "session=; Path=/; Max-Age=0");
        res.set_redirect("/");
    });

    int port = 8080;
    const char* portValue = getenv("PORT");
    if (portValue != nullptr) port = stoi(portValue);

    cout << "AGAP server is running on port " << port << endl;
    server.listen("0.0.0.0", port);
}
