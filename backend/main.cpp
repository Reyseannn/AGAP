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

static string safeStatus(const User& user);

static string getCookie(const httplib::Request& req, const string& name) {
    string cookies = req.get_header_value("Cookie");
    string key = name + "=";
    size_t start = cookies.find(key);
    if (start == string::npos) return "";
    start += key.length();
    size_t end = cookies.find(';', start);
    if (end == string::npos) return cookies.substr(start);
    return cookies.substr(start, end - start);
}

static string base64Encode(const string& input) {
    static const string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string output;
    int value = 0;
    int bits = -6;
    for (unsigned char c : input) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            output.push_back(alphabet[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) output.push_back(alphabet[((value << 8) >> (bits + 8)) & 0x3F]);
    while (output.size() % 4) output.push_back('=');
    return output;
}

static bool adminCredentialsSet() {
    const char* username = getenv("AGAP_ADMIN_USER");
    const char* password = getenv("AGAP_ADMIN_PASSWORD");
    return username && username[0] && password && password[0];
}

static bool adminAuthorized(const httplib::Request& req) {
    const char* username = getenv("AGAP_ADMIN_USER");
    const char* password = getenv("AGAP_ADMIN_PASSWORD");
    if (!username || !password || !username[0] || !password[0]) return false;
    string expected = "Basic " + base64Encode(string(username) + ":" + password);
    return req.get_header_value("Authorization") == expected;
}

static bool validAdminSession(const httplib::Request& req, string& token) {
    token = getCookie(req, "agap_admin_session");
    const char* username = getenv("AGAP_ADMIN_USER");
    return username && loginFromSession(token) == string("admin:") + username;
}

static void requireAdmin(httplib::Response& res) {
    res.status = 401;
    res.set_header("WWW-Authenticate", "Basic realm=\"AGAP verification review\", charset=\"UTF-8\"");
    res.set_content("Admin credentials are required.", "text/plain; charset=utf-8");
}

static string renderAdminPage(const string& csrfToken) {
    string page = "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>AGAP ID review</title>"
        "<style>body{font:15px Arial,sans-serif;color:#12285a;background:#f4f6f9;margin:0}main{max-width:900px;margin:32px auto;padding:0 20px}h1{font-size:24px}.card{background:#fff;border:1px solid #d5deea;border-radius:10px;padding:18px;margin:16px 0}.card img{display:block;max-width:100%;max-height:420px;object-fit:contain;margin:14px 0;border:1px solid #d5deea}.muted{color:#6b7587}.actions{display:flex;gap:10px}.actions button{padding:10px 16px;border:0;border-radius:6px;font-weight:700;cursor:pointer}.approve{background:#16824d;color:#fff}.reject{background:#b3261e;color:#fff}</style><main><h1>AGAP resident ID review</h1><p>Review the uploaded ID and resident details before approving the account.</p>";
    vector<User> users = listUsers();
    int count = 0;
    for (const User& user : users) {
        if (safeStatus(user) != "pending") continue;
        ++count;
        page += "<section class=\"card\"><h2>" + escapeHtml(user.fullName) + "</h2><p><strong>Mobile:</strong> "
            + escapeHtml(user.phone) + "</p><p><strong>Address:</strong> " + escapeHtml(user.address)
            + "</p><p><strong>ID type:</strong> " + escapeHtml(user.idType) + "</p>";
        if (!user.idPhotoFile.empty()) {
            page += "<img alt=\"Uploaded resident ID\" src=\"/admin/verification-photo?name="
                + escapeHtml(user.idPhotoFile) + "\">";
        } else {
            page += "<p class=\"muted\">No ID photo is available.</p>";
        }
        page += "<form method=\"post\" action=\"/admin/verifications\"><input type=\"hidden\" name=\"login\" value=\""
            + escapeHtml(user.login) + "\"><input type=\"hidden\" name=\"csrf\" value=\"" + escapeHtml(csrfToken)
            + "\"><div class=\"actions\"><button class=\"approve\" name=\"decision\" value=\"verified\">Approve</button>"
            "<button class=\"reject\" name=\"decision\" value=\"rejected\">Reject</button></div></form></section>";
    }
    if (count == 0) page += "<section class=\"card\"><p>No pending ID submissions.</p></section>";
    page += "</main></html>";
    return page;
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
    page = replaceAll(page, "{{verification_state}}", status);
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
    bool validSignature = false;
    if (extension == ".jpg" && file.content.size() >= 3) {
        validSignature = static_cast<unsigned char>(file.content[0]) == 0xFF
            && static_cast<unsigned char>(file.content[1]) == 0xD8
            && static_cast<unsigned char>(file.content[2]) == 0xFF;
    } else if (extension == ".png" && file.content.size() >= 8) {
        const unsigned char signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        validSignature = true;
        for (size_t i = 0; i < 8; ++i) {
            if (static_cast<unsigned char>(file.content[i]) != signature[i]) validSignature = false;
        }
    }
    if (!validSignature) return false;

    fs::path directory = fs::path(getDataDirectory()) / folder;
    error_code error;
    fs::create_directories(directory, error);
    if (error) return false;

    savedName = prefix + "-" + makeSalt() + extension;
    ofstream output(directory / savedName, ios::binary);
    if (!output) return false;
    output.write(file.content.data(), static_cast<streamsize>(file.content.size()));
    output.close();
    if (!output) return false;
    error_code permissionError;
    fs::permissions(directory / savedName, fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace, permissionError);
    return !permissionError;
}

static string currentDate() {
    time_t now = time(nullptr);
    tm localTime{};
    localtime_r(&now, &localTime);
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d", &localTime);
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

    server.Get("/admin/verifications", [](const httplib::Request& req, httplib::Response& res) {
        if (!adminCredentialsSet()) {
            res.status = 503;
            res.set_content("Set AGAP_ADMIN_USER and AGAP_ADMIN_PASSWORD in the Render environment before using verification review.", "text/plain; charset=utf-8");
            return;
        }
        if (!adminAuthorized(req)) {
            requireAdmin(res);
            return;
        }

        string oldToken = getCookie(req, "agap_admin_session");
        if (!oldToken.empty()) deleteSession(oldToken);
        const char* username = getenv("AGAP_ADMIN_USER");
        string token = createSession(string("admin:") + username);
        res.set_header("Set-Cookie", "agap_admin_session=" + token + "; Path=/admin; HttpOnly; SameSite=Strict; Secure");
        res.set_header("Cache-Control", "no-store");
        res.set_content(renderAdminPage(token), "text/html; charset=utf-8");
    });

    server.Get("/admin/verification-photo", [](const httplib::Request& req, httplib::Response& res) {
        if (!adminAuthorized(req)) {
            requireAdmin(res);
            return;
        }
        string token;
        if (!validAdminSession(req, token)) {
            res.status = 403;
            res.set_content("Open the verification review page first.", "text/plain; charset=utf-8");
            return;
        }

        string fileName = req.get_param_value("name");
        if (fileName.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.") != string::npos
            || fileName.find("..") != string::npos) {
            res.status = 400;
            return;
        }

        bool belongsToResident = false;
        for (const User& user : listUsers()) {
            if (user.idPhotoFile == fileName) {
                belongsToResident = true;
                break;
            }
        }
        if (!belongsToResident) {
            res.status = 404;
            return;
        }

        ifstream input(fs::path(getDataDirectory()) / "verification" / fileName, ios::binary);
        if (!input) {
            res.status = 404;
            return;
        }
        string image((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
        string contentType = fileName.size() >= 4 && fileName.substr(fileName.size() - 4) == ".png"
            ? "image/png" : "image/jpeg";
        res.set_header("Cache-Control", "no-store");
        res.set_content(image, contentType);
    });

    server.Post("/admin/verifications", [](const httplib::Request& req, httplib::Response& res) {
        if (!adminCredentialsSet()) {
            res.status = 503;
            res.set_content("Admin review is not configured.", "text/plain; charset=utf-8");
            return;
        }
        if (!adminAuthorized(req)) {
            requireAdmin(res);
            return;
        }

        string token;
        if (!validAdminSession(req, token) || req.get_param_value("csrf") != token) {
            res.status = 403;
            res.set_content("The review form expired. Reload the page and try again.", "text/plain; charset=utf-8");
            return;
        }

        string login = toLower(cleanField(req.get_param_value("login")));
        string decision = req.get_param_value("decision");
        User user;
        if (!findUser(login, user) || safeStatus(user) != "pending"
            || (decision != "verified" && decision != "rejected")) {
            res.status = 400;
            res.set_content("This verification request cannot be updated.", "text/plain; charset=utf-8");
            return;
        }
        user.verification = decision;
        if (!updateUser(user)) {
            res.status = 500;
            res.set_content("Could not save the review decision.", "text/plain; charset=utf-8");
            return;
        }
        res.set_redirect("/admin/verifications");
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
