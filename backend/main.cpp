#include "httplib.h"
#include "auth.h"
#include "storage.h"
#include "staff_storage.h"
#include "named_list_storage.h"
#include "pdf_report.h"
#include "announcement_storage.h"
#include "complaint_storage.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
using namespace std;


static string readFile(const string& path) {
    ifstream file(path);
    if (!file) {
        return "<!DOCTYPE html><html><body style='font-family:sans-serif;padding:40px;max-width:600px'>"
               "<h1>File not found</h1>"
               "<p>The server could not find this file:</p>"
               "<p><code>" + path + "</code></p>"
               "<p>This almost always means the server wasn't started from inside the "
               "<code>backend</code> folder. Close this terminal, open a new one, run "
               "<code>cd backend</code>, then start the server again from there.</p>"
               "</body></html>";
    }
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

static string initialsOf(const string& fullName) {
    string first, last;
    stringstream words(fullName);
    string word;
    while (words >> word) {
        if (first.empty()) first = word;
        last = word;
    }
    string initials;
    if (!first.empty()) initials += toupper((unsigned char)first[0]);
    if (!last.empty() && last != first) initials += toupper((unsigned char)last[0]);
    return initials.empty() ? "?" : initials;
}

static string toLower(string text) {
    transform(text.begin(), text.end(), text.begin(), ::tolower);
    return text;
}

static string getCookie(const httplib::Request& req, const string& name) {
    string cookies = req.get_header_value("Cookie");
    stringstream parts(cookies);
    string part;
    while (getline(parts, part, ';')) {
        size_t start = part.find_first_not_of(' ');
        if (start == string::npos) continue;
        part = part.substr(start);
        size_t equals = part.find('=');
        if (equals == string::npos) continue;
        if (part.substr(0, equals) == name) return part.substr(equals + 1);
    }
    return "";
}

static string getSessionToken(const httplib::Request& req) {
    return getCookie(req, "session");
}

static string getAdminSessionToken(const httplib::Request& req) {
    return getCookie(req, "admin_session");
}

static string statusBadgeClassFor(const string& status) {
    if (status == "Pending") return "badge-pending";
    if (status == "In progress") return "badge-progress";
    if (status == "Resolved") return "badge-resolved";
    if (status == "Rejected") return "badge-rejected";
    return "badge-pending";
}

static string residentStatusLabel(const string& verification) {
    if (verification == "pending") return "Awaiting review";
    if (verification == "verified") return "Verified";
    if (verification == "rejected") return "Rejected";
    return "Not submitted";
}
static string residentStatusBadgeClass(const string& verification) {
    if (verification == "pending") return "badge-pending";
    if (verification == "verified") return "badge-resolved";
    if (verification == "rejected") return "badge-rejected";
    return "badge-pending";
}

static const string TEAMS_FILE = "../data/teams.txt";
static const string PUROKS_FILE = "../data/puroks.txt";

static string todayFormatted() {
    time_t now = time(nullptr);
    tm local = *localtime(&now);

    char monthBuffer[8];
    strftime(monthBuffer, sizeof(monthBuffer), "%b", &local);

    return string(monthBuffer) + " " + to_string(local.tm_mday) + ", " + to_string(local.tm_year + 1900);
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

static void showVerifyPage(httplib::Response& res, const User& user, const string& error) {
    string page = readFile("../frontend/verify.html");

    string errorBanner = "";
    if (!error.empty()) {
        errorBanner = "<div class=\"alert-error\">" + escapeHtml(error) + "</div>";
    }
    page = replaceAll(page, "<!--ERROR-->", errorBanner);

    string pendingNotice = "";
    if (user.verification == "pending") {
        pendingNotice = "<p class=\"notice-pending\">You already have a request waiting for review. "
                         "Submitting this form again will replace it.</p>";
    }
    page = replaceAll(page, "<!--PENDING_NOTICE-->", pendingNotice);

    page = replaceAll(page, "{{phone}}", escapeHtml(user.phone));
    page = replaceAll(page, "{{address}}", escapeHtml(user.address));
    res.set_content(page, "text/html");
}

static void startSession(httplib::Response& res, const string& login) {
    string token = createSession(login);
    res.set_header("Set-Cookie", "session=" + token + "; Path=/; HttpOnly; SameSite=Lax");
    res.set_redirect("/dashboard");
}

static string stateClassFor(const string& verification) {
    if (verification == "unverified") return "state-new";
    if (verification == "pending")    return "state-pending";
    if (verification == "rejected")   return "state-rejected";
    if (verification == "verified")   return "state-verified";
    return "state-new";
}

static string verificationBannerFor(const string& verification, const string& rejectionReason) {
    if (verification == "pending") {
        return "<div class=\"banner banner-warn\">Your verification request is waiting for admin review. "
               "You can file complaints after it is approved.</div>";
    }
    if (verification == "verified") {
        return "<div class=\"banner banner-success\">Your account is approved. You can file complaints.</div>";
    }
    if (verification == "rejected") {
        string reason = rejectionReason.empty() ? "Please upload a clearer photo of your ID." : rejectionReason;
        return "<div class=\"banner banner-error\"><span>Verification was not approved: " +
               escapeHtml(reason) + "</span> <a href=\"/verify\">Upload your ID again</a></div>";
    }
    return "<div class=\"banner banner-warn\">Verify your account by submitting your details and ID. "
           "An admin must approve it before you can file a complaint.</div>";
}

static string safeFileName(const string& text) {
    string result;
    for (char c : text) {
        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') result += c;
        else result += '_';
    }
    return result;
}

static void showComplaintPage(httplib::Response& res, const User& user, const string& error) {
    string page = readFile("../frontend/filecomplaint.html");
    string banner = "";
    if (!error.empty()) {
        banner = "<div class=\"alert-error\">" + escapeHtml(error) + "</div>";
    }
    page = replaceAll(page, "{{errorBanner}}", banner);
    page = replaceAll(page, "{{fullname}}", escapeHtml(user.fullName));
    page = replaceAll(page, "{{phone}}", escapeHtml(user.phone));
    page = replaceAll(page, "{{initials}}", escapeHtml(initialsOf(user.fullName)));
    res.set_content(page, "text/html");
}

static string complaintTitle(const Complaint& c) {
    return c.category + " - " + c.location;
}

static string statusKeyFor(const string& status) {
    if (status == "In progress") return "progress";
    if (status == "Resolved") return "resolved";
    if (status == "Rejected") return "rejected";
    return "pending";
}

static bool isValidStatus(const string& status) {
    return status == "Pending" || status == "In progress" || status == "Resolved" || status == "Rejected";
}

static string csvCell(string text) {
    if (!text.empty() && (text[0] == '=' || text[0] == '+' || text[0] == '-' || text[0] == '@')) {
        text = "'" + text;
    }
    text = replaceAll(text, "\"", "\"\"");
    return "\"" + text + "\"";
}

static string monthKeyFor(const string& filedDate) {
    static const vector<string> months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (filedDate.size() < 9) return "";
    string shortMonth = filedDate.substr(0, 3);
    int month = 0;
    for (size_t i = 0; i < months.size(); ++i) if (months[i] == shortMonth) month = (int)i + 1;
    if (!month) return "";
    size_t comma = filedDate.find(',');
    if (comma == string::npos || comma + 2 >= filedDate.size()) return "";
    string year = filedDate.substr(comma + 2);
    stringstream key;
    key << year << '-';
    if (month < 10) key << '0';
    key << month;
    return key.str();
}

static string currentMonthKey() {
    time_t now = time(nullptr);
    tm local = *localtime(&now);
    stringstream key;
    key << local.tm_year + 1900 << '-';
    if (local.tm_mon + 1 < 10) key << '0';
    key << local.tm_mon + 1;
    return key.str();
}

static string monthLabel(const string& key) {
    if (key.size() != 7 || key[4] != '-') return key;
    static const vector<string> months = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    int month = atoi(key.substr(5, 2).c_str());
    if (month < 1 || month > 12) return key;
    return months[month - 1] + " " + key.substr(0, 4);
}

static bool requireStaff(const httplib::Request& req, httplib::Response& res, Staff& staff) {
    string login = loginFromSession(getAdminSessionToken(req));
    if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
        res.set_redirect("/signin.html");
        return false;
    }
    return true;
}

static string residentsNavBadge() {
    VerificationCounts counts = countResidentsByVerification();
    return (counts.pending > 0) ? "<span class=\"nav-count\">" + to_string(counts.pending) + "</span>" : "";
}

static string fillAdminShell(string page, const Staff& staff) {
    page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
    page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
    page = replaceAll(page, "{{residentsNavBadge}}", residentsNavBadge());
    return page;
}

static bool moveIntoBackendFolder(const char* programPath) {
    error_code ignored;
    if (filesystem::exists("../frontend", ignored)) return true;

    vector<filesystem::path> candidates;
    candidates.push_back(filesystem::path("backend"));
    candidates.push_back(filesystem::absolute(programPath, ignored).parent_path());
    for (const filesystem::path& folder : candidates) {
        if (filesystem::exists(folder / ".." / "frontend", ignored)) {
            filesystem::current_path(folder, ignored);
            return !ignored;
        }
    }
    return false;
}

int main(int argc, char** argv) {
    if (!moveIntoBackendFolder(argc > 0 ? argv[0] : "")) {
        cerr << "ERROR: could not find the frontend folder. Start the server from inside the "
                "backend folder (or use server.bat)." << endl;
        return 1;
    }

    error_code ignored;
    filesystem::create_directories("../data/ids", ignored);
    filesystem::create_directories("../data/complaint_photos", ignored);
    filesystem::create_directories("../data/reports", ignored);

    ensureDefaultAdmin();
    cout << "Data folder: " << filesystem::absolute("../data", ignored).lexically_normal().string() << endl;

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
        Staff existingStaff;
        if (findUser(login, existing) || findStaff(login, existingStaff)) {
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
            showAuthPage(res, "signup.html", "Could not save your account. Please look at the server window for the file it could not write to.");
            return;
        }
        startSession(res, login);
    });

    server.Post("/signin", [](const httplib::Request& req, httplib::Response& res) {
        string login = toLower(cleanField(req.get_param_value("login")));
        string password = req.get_param_value("password");

        Staff staff;
        if (findStaff(login, staff)) {
            if (hashPassword(staff.salt, password) != staff.passwordHash) {
                showAuthPage(res, "signin.html", "Wrong email or password. Please try again.");
                return;
            }
            if (staff.status != "active") {
                showAuthPage(res, "signin.html", "This account has been deactivated. Contact the administrator.");
                return;
            }
            string token = createSession(login);
            res.set_header("Set-Cookie", "admin_session=" + token + "; Path=/; HttpOnly; SameSite=Lax");
            res.set_redirect("/admin/dashboard");
            return;
        }

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
        string phoneDisplay = user.phone.empty() ? "Not added yet" : user.phone;
        string addressDisplay = user.address.empty() ? "Not added yet" : user.address;

        string rejectionMessage = user.rejectionReason.empty()
            ? "Please upload a clearer photo of your ID."
            : user.rejectionReason;

        string page = readFile("../frontend/dashboard.html");
        page = replaceAll(page, "{{firstname}}", escapeHtml(firstName));
        page = replaceAll(page, "{{fullname}}", escapeHtml(user.fullName));
        page = replaceAll(page, "{{initials}}", escapeHtml(initialsOf(user.fullName)));
        page = replaceAll(page, "{{login}}", escapeHtml(user.login));
        page = replaceAll(page, "{{phone}}", escapeHtml(phoneDisplay));
        page = replaceAll(page, "{{address}}", escapeHtml(addressDisplay));
        page = replaceAll(page, "{{rejectionReason}}", escapeHtml(rejectionMessage));
        page = replaceAll(page, "{{state}}", stateClassFor(user.verification));
        page = replaceAll(page, "{{verificationBanner}}",
                          verificationBannerFor(user.verification, user.rejectionReason));

        vector<Announcement> announcements = listAnnouncements();
        string announcementRows = "";
        for (size_t i = 0; i < announcements.size() && i < 4; i++) {
            announcementRows += "<div class=\"announce-item\"><strong>" + escapeHtml(announcements[i].title) +
                                 "</strong><p>Posted " + escapeHtml(announcements[i].postedDate) + "</p></div>";
        }
        if (announcementRows.empty()) {
            announcementRows = "<p class=\"muted\">No announcements yet.</p>";
        }
        page = replaceAll(page, "{{announcementRows}}", announcementRows);

        string complaintRows = "";
        vector<Complaint> mine = listComplaintsFor(user.login);
        for (size_t i = 0; i < mine.size() && i < 4; i++) {
            complaintRows += "<div class=\"complaint\"><div><strong>" + escapeHtml(mine[i].category) + " - " +
                              escapeHtml(mine[i].location) + "</strong><p>" + escapeHtml(mine[i].id) + " | " +
                              escapeHtml(mine[i].filedDate) + "</p></div><span class=\"badge " +
                              statusBadgeClassFor(mine[i].status) + "\">" + escapeHtml(mine[i].status) +
                              "</span><a class=\"view-all\" href=\"/complaint?id=" + escapeHtml(mine[i].id) + "\">View details</a></div>";
        }
        if (complaintRows.empty()) {
            complaintRows = "<p class=\"muted\">You have not filed any complaints yet.</p>";
        }
        page = replaceAll(page, "{{complaintRows}}", complaintRows);
        res.set_content(page, "text/html");
    });

    server.Get("/verify", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        if (user.verification == "verified") {
            res.set_redirect("/dashboard");
            return;
        }
        showVerifyPage(res, user, "");
    });

    server.Post("/verify", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        if (user.verification == "verified") {
            res.set_redirect("/dashboard");
            return;
        }

        string phone = cleanField(req.form.get_field("phone"));
        string address = cleanField(req.form.get_field("address"));
        string idType = cleanField(req.form.get_field("idtype"));
        if (idType.empty()) idType = "Barangay ID";

        if (phone.empty() || address.empty()) {
            user.phone = phone;
            user.address = address;
            showVerifyPage(res, user, "Please fill in your phone number and address.");
            return;
        }

        if (!req.is_multipart_form_data() || !req.form.has_file("idphoto")) {
            user.phone = phone;
            user.address = address;
            showVerifyPage(res, user, "Please choose a photo of your ID.");
            return;
        }
        httplib::FormData photo = req.form.get_file("idphoto");

        if (photo.content.empty()) {
            user.phone = phone;
            user.address = address;
            showVerifyPage(res, user, "Please choose a photo of your ID.");
            return;
        }
        if (photo.content_type != "image/jpeg" && photo.content_type != "image/png") {
            user.phone = phone;
            user.address = address;
            showVerifyPage(res, user, "Please upload a JPG or PNG image.");
            return;
        }
        const size_t MAX_SIZE = 5 * 1024 * 1024;
        if (photo.content.size() > MAX_SIZE) {
            user.phone = phone;
            user.address = address;
            showVerifyPage(res, user, "That photo is too large. Please use one under 5 MB.");
            return;
        }

        string extension = (photo.content_type == "image/png") ? ".png" : ".jpg";

        filesystem::create_directories("../data/ids");
        string path = "../data/ids/" + safeFileName(login) + extension;

        ofstream out(path, ios::binary);
        if (!out) {
            user.phone = phone;
            user.address = address;
            showVerifyPage(res, user, "Could not save your photo. Please try again.");
            return;
        }
        out.write(photo.content.data(), photo.content.size());
        out.close();

        if (!saveVerificationRequest(login, phone, address, idType, "pending")) {
            showVerifyPage(res, user, "Could not save your verification request. Please try again.");
            return;
        }
        res.set_redirect("/dashboard");
    });

    server.Get("/my-complaints", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        string filter = req.has_param("status") ? req.get_param_value("status") : "active";
        if (filter != "active" && filter != "resolved" && filter != "all") filter = "active";
        string rows = "";
        vector<Complaint> userComplaints = listComplaintsFor(user.login);
        int activeCount = 0, resolvedCount = 0;
        for (const Complaint& c : userComplaints) {
            if (c.status == "Pending" || c.status == "In progress") ++activeCount;
            if (c.status == "Resolved") ++resolvedCount;
        }
        for (const Complaint& c : userComplaints) {
            if (filter == "active" && (c.status == "Resolved" || c.status == "Rejected")) continue;
            if (filter == "resolved" && c.status != "Resolved") continue;
            string categoryIcon = c.category == "Garbage" ? "▤" : c.category == "Noise" ? "◖" : c.category == "Road and lighting" ? "☼" : c.category == "Flooding" ? "≋" : "•••";
            string categoryStyle = c.category == "Garbage" ? "category-garbage" : c.category == "Noise" ? "category-noise" : c.category == "Road and lighting" ? "category-lighting" : c.category == "Flooding" ? "category-flooding" : "category-other";
            int steps = c.status == "Resolved" ? 3 : c.status == "In progress" ? 2 : c.status == "Rejected" ? 0 : 1;
            string progress = c.status == "Rejected" ? "" : "<div class=\"mini-progress\">";
            for (int i = 0; i < steps; ++i) progress += "<i class=\"done\"></i>";
            for (int i = steps; i < 3 && c.status != "Rejected"; ++i) progress += "<i></i>";
            if (c.status != "Rejected") progress += "</div>";
            rows += "<article class=\"complaint-list-card status-" + statusKeyFor(c.status) + "\"><span class=\"resident-complaint-icon " + categoryStyle + "\" aria-hidden=\"true\">" + categoryIcon +
                    "</span><div class=\"resident-complaint-copy\"><h2>" + escapeHtml(c.description.empty() ? c.category : c.description) + "</h2><p>" + escapeHtml(c.id) + " | " + escapeHtml(c.category) + " | Filed " + escapeHtml(c.filedDate) +
                    "</p><p>" + escapeHtml(c.location) + "</p></div><div class=\"resident-complaint-state\"><span class=\"badge " + statusBadgeClassFor(c.status) + "\">" + escapeHtml(c.status) +
                    "</span>" + progress + "</div><a href=\"/complaint?id=" + escapeHtml(c.id) + "\">View details &rsaquo;</a></article>";
        }
        if (rows.empty()) {
            if (userComplaints.empty()) rows = "<div class=\"resident-empty\"><h2>No complaints yet</h2><p>When you submit a complaint, it will appear here.</p><a class=\"btn btn-yellow\" href=\"/file-complaint\">File a complaint</a></div>";
            else rows = "<div class=\"resident-empty\"><h2>No complaints in this view</h2><p>Try another status filter to see your reports.</p></div>";
        }

        string page = readFile("../frontend/my_complaints.html");
        page = replaceAll(page, "{{initials}}", escapeHtml(initialsOf(user.fullName)));
        page = replaceAll(page, "{{activeTab}}", filter == "active" ? "active" : "");
        page = replaceAll(page, "{{resolvedTab}}", filter == "resolved" ? "active" : "");
        page = replaceAll(page, "{{allTab}}", filter == "all" ? "active" : "");
        page = replaceAll(page, "{{activeCount}}", to_string(activeCount));
        page = replaceAll(page, "{{resolvedCount}}", to_string(resolvedCount));
        page = replaceAll(page, "{{allCount}}", to_string((int)userComplaints.size()));
        page = replaceAll(page, "{{listRows}}", rows);
        res.set_content(page, "text/html");
    });

    auto showResidentComplaint = [](const httplib::Request& req, httplib::Response& res, bool receipt) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) { res.set_redirect("/signin.html"); return; }
        string id = req.has_param("id") ? req.get_param_value("id") : "";
        Complaint complaint;
        if (id.empty() || !findComplaint(id, complaint) || complaint.login != user.login) {
            res.status = 404;
            res.set_content("<main style=\"font-family:sans-serif;max-width:640px;margin:60px auto;padding:20px\"><h1>Complaint not found</h1><p>This complaint could not be found in your account.</p><a href=\"/my-complaints\">Back to my complaints</a></main>", "text/html");
            return;
        }
        string page = readFile(receipt ? "../frontend/complaint_submitted.html" : "../frontend/complaint_detail.html");
        page = replaceAll(page, "{{initials}}", escapeHtml(initialsOf(user.fullName)));
        page = replaceAll(page, "{{complaintId}}", escapeHtml(complaint.id));
        if (receipt) { res.set_content(page, "text/html"); return; }
        string statusMessage = complaint.status == "Pending" ? "Your complaint has been received and is waiting for review." :
            complaint.status == "In progress" ? "The barangay team is working on this complaint." :
            complaint.status == "Resolved" ? "This complaint has been marked resolved." : "This complaint was marked rejected.";
        const vector<pair<string, string>> fields = {
            {"{{complaintTitle}}", escapeHtml(complaintTitle(complaint))}, {"{{statusClass}}", statusBadgeClassFor(complaint.status)},
            {"{{status}}", escapeHtml(complaint.status)}, {"{{filedDate}}", escapeHtml(complaint.filedDate)},
            {"{{category}}", escapeHtml(complaint.category)}, {"{{location}}", escapeHtml(complaint.location)},
            {"{{description}}", escapeHtml(complaint.description)}, {"{{statusMessage}}", statusMessage}
        };
        for (const auto& field : fields) page = replaceAll(page, field.first, field.second);
        res.set_content(page, "text/html");
    };
    server.Get("/complaint", [showResidentComplaint](const httplib::Request& req, httplib::Response& res) { showResidentComplaint(req, res, false); });
    server.Get("/complaint-submitted", [showResidentComplaint](const httplib::Request& req, httplib::Response& res) { showResidentComplaint(req, res, true); });

    server.Get("/announcements", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        string rows = "";
        for (const Announcement& a : listAnnouncements()) {
            rows += "<article class=\"announcement-card\"><div class=\"announcement-accent\"></div><div class=\"announcement-content\"><h2>" + escapeHtml(a.title) + "</h2><span class=\"announcement-category\">" +
                    escapeHtml(a.category) + "</span><small>Posted " + escapeHtml(a.postedDate) + "</small><p>" + escapeHtml(a.message) + "</p></div></article>";
        }
        if (rows.empty()) rows = "<div class=\"resident-empty\"><h2>No announcements yet</h2><p>New barangay updates will appear here.</p></div>";

        string page = readFile("../frontend/resident_list.html");
        page = replaceAll(page, "{{initials}}", escapeHtml(initialsOf(user.fullName)));
        page = replaceAll(page, "{{pageTitle}}", "Announcements");
        page = replaceAll(page, "{{pageIntro}}", "News from the barangay, newest first.");
        page = replaceAll(page, "{{listRows}}", rows);
        res.set_content(page, "text/html");
    });

    server.Get("/file-complaint", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        if (user.verification != "verified") {
            res.set_redirect("/verify");
            return;
        }
        showComplaintPage(res, user, "");
    });

    server.Post("/file-complaint", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        if (user.verification != "verified") {
            res.set_redirect("/verify");
            return;
        }

        string categoryKey = cleanField(req.form.get_field("category"));
        string location = cleanField(req.form.get_field("location"));
        string description = cleanField(req.form.get_field("description"));

        string category;
        if (categoryKey == "garbage") category = "Garbage";
        else if (categoryKey == "noise") category = "Noise";
        else if (categoryKey == "lighting") category = "Road and lighting";
        else if (categoryKey == "flooding") category = "Flooding";
        else if (categoryKey == "others") category = "Others";
        else {
            showComplaintPage(res, user, "Please choose what the problem is.");
            return;
        }
        if (location.empty() || description.empty()) {
            showComplaintPage(res, user, "Please fill in where it is and what happened.");
            return;
        }

        string id = nextComplaintId();
        string photoFile = "";

        if (req.is_multipart_form_data() && req.form.has_file("photo")) {
            httplib::FormData photo = req.form.get_file("photo");
            if (!photo.content.empty()) {
                if (photo.content_type != "image/jpeg" && photo.content_type != "image/png") {
                    showComplaintPage(res, user, "Please upload a JPG or PNG image.");
                    return;
                }
                if (photo.content.size() > 5 * 1024 * 1024) {
                    showComplaintPage(res, user, "That photo is too large. Please use one under 5 MB.");
                    return;
                }
                photoFile = safeFileName(id) + (photo.content_type == "image/png" ? ".png" : ".jpg");
                filesystem::create_directories("../data/complaint_photos");
                ofstream out("../data/complaint_photos/" + photoFile, ios::binary);
                if (!out) {
                    showComplaintPage(res, user, "Could not save your photo. Please try again.");
                    return;
                }
                out.write(photo.content.data(), photo.content.size());
            }
        }

        Complaint complaint;
        complaint.id = id;
        complaint.login = login;
        complaint.category = category;
        complaint.location = location;
        complaint.description = description;
        complaint.status = "Pending";
        complaint.filedDate = todayFormatted();
        complaint.photoFile = photoFile;
        if (!addComplaint(complaint)) {
            showComplaintPage(res, user, "Could not save your complaint. Please try again.");
            return;
        }
        res.set_redirect("/complaint-submitted?id=" + id);
    });

    server.Get("/id-photo", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        if (login.empty()) {
            res.status = 404;
            return;
        }

        string base = "../data/ids/" + safeFileName(login);
        string jpgPath = base + ".jpg";
        string pngPath = base + ".png";

        string path, contentType;
        if (filesystem::exists(jpgPath)) { path = jpgPath; contentType = "image/jpeg"; }
        else if (filesystem::exists(pngPath)) { path = pngPath; contentType = "image/png"; }
        else { res.status = 404; return; }

        ifstream file(path, ios::binary);
        stringstream bytes;
        bytes << file.rdbuf();
        res.set_content(bytes.str(), contentType);
    });


    server.Get("/admin/", [](const httplib::Request& req, httplib::Response& res) {
        res.set_redirect("/admin");
    });

    server.Get("/admin", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (!login.empty() && findStaff(login, staff) && staff.status == "active") {
            res.set_redirect("/admin/dashboard");
        } else {
            res.set_redirect("/signin.html");
        }
    });

    server.Get("/admin/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }

        VerificationCounts counts = countResidentsByVerification();

        string notice = "";
        string navBadge = "";
        if (counts.pending > 0) {
            notice = "<div class=\"notice-row\">"
                      "<span>! <strong>" + to_string(counts.pending) + " resident" + (counts.pending == 1 ? "" : "s") +
                      "</strong> are waiting for ID verification. "
                      "They cannot file complaints until staff approve their ID.</span>"
                      "<a href=\"/admin/residents\">Review</a></div>";
            navBadge = "<span class=\"nav-count\">" + to_string(counts.pending) + "</span>";
        }

        vector<Complaint> allComplaints = listAllComplaints();
        int pendingComplaints = 0, progressComplaints = 0, resolvedComplaints = 0;
        const char* categories[] = {"Garbage", "Noise", "Road and lighting", "Flooding", "Others"};
        int categoryCounts[5] = {0, 0, 0, 0, 0};
        string recentRows = "";
        for (size_t i = 0; i < allComplaints.size(); i++) {
            const Complaint& c = allComplaints[i];
            if (c.status == "In progress") progressComplaints++;
            else if (c.status == "Resolved") resolvedComplaints++;
            else if (c.status != "Rejected") pendingComplaints++;
            for (int k = 0; k < 5; k++) {
                if (c.category == categories[k]) categoryCounts[k]++;
            }
            if (i < 5) {
                User resident;
                string residentName = findUser(c.login, resident) ? resident.fullName : c.login;
                recentRows += "<tr><td>" + escapeHtml(residentName) + "</td><td>" + escapeHtml(c.category) + "</td><td>" +
                              escapeHtml(c.filedDate) + "</td><td><span class=\"badge " + statusBadgeClassFor(c.status) + "\">" +
                              escapeHtml(c.status) + "</span></td></tr>";
            }
        }
        if (recentRows.empty()) {
            recentRows = "<tr><td colspan=\"4\" class=\"muted\">No complaints have been filed yet.</td></tr>";
        }
        string categoryBars = "";
        for (int k = 0; k < 5; k++) {
            int percent = allComplaints.empty() ? 0 : (categoryCounts[k] * 100) / (int)allComplaints.size();
            categoryBars += "<div class=\"cat-bar-row\"><span class=\"cat-label\">" + string(categories[k]) +
                            "</span><div class=\"cat-bar-track\"><div class=\"cat-bar-fill\" style=\"width:" + to_string(percent) +
                            "%\"></div></div><span class=\"cat-num\">" + to_string(categoryCounts[k]) + "</span></div>";
        }

        string page = fillAdminShell(readFile("../frontend/staff/dashboard_admin.html"), staff);
        page = replaceAll(page, "{{totalComplaints}}", to_string(allComplaints.size()));
        page = replaceAll(page, "{{pendingComplaints}}", to_string(pendingComplaints));
        page = replaceAll(page, "{{pendingComplaintsPlural}}", pendingComplaints == 1 ? "" : "s");
        page = replaceAll(page, "{{progressComplaints}}", to_string(progressComplaints));
        page = replaceAll(page, "{{resolvedComplaints}}", to_string(resolvedComplaints));
        page = replaceAll(page, "{{recentRows}}", recentRows);
        page = replaceAll(page, "{{categoryBars}}", categoryBars);
        page = replaceAll(page, "{{verifiedCount}}", to_string(counts.verified));
        page = replaceAll(page, "{{verifiedPlural}}", counts.verified == 1 ? "" : "s");
        page = replaceAll(page, "{{todayDate}}", todayFormatted());
        page = replaceAll(page, "{{pendingCount}}", to_string(counts.pending));
        page = replaceAll(page, "{{pendingPlural}}", counts.pending == 1 ? "" : "s");
        page = replaceAll(page, "{{verificationNotice}}", notice);
        res.set_content(page, "text/html");
    });

    server.Get("/admin/complaints", [](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;

        int pending = 0, progress = 0, resolved = 0, rejected = 0;
        string rows = "";
        vector<Complaint> all = listAllComplaints();
        for (const Complaint& c : all) {
            if (c.status == "In progress") progress++;
            else if (c.status == "Resolved") resolved++;
            else if (c.status == "Rejected") rejected++;
            else pending++;

            User resident;
            string residentName = findUser(c.login, resident) ? resident.fullName : c.login;
            rows += "<tr data-status=\"" + statusKeyFor(c.status) + "\" data-category=\"" + escapeHtml(c.category) + "\">"
                    "<td>" + escapeHtml(c.id) + "</td><td>" + escapeHtml(complaintTitle(c)) + "</td>"
                    "<td>" + escapeHtml(c.category) + "</td><td>" + escapeHtml(residentName) + "</td>"
                    "<td>" + escapeHtml(c.filedDate) + "</td>"
                    "<td><span class=\"badge " + statusBadgeClassFor(c.status) + "\">" + escapeHtml(c.status) + "</span></td>"
                    "<td><a href=\"/admin/complaints/" + escapeHtml(c.id) + "\">View &rsaquo;</a></td></tr>";
        }
        if (rows.empty()) {
            rows = "<tr><td colspan=\"7\" class=\"muted\">No complaints have been filed yet.</td></tr>";
        }

        string page = fillAdminShell(readFile("../frontend/staff/complaints_admin.html"), staff);
        page = replaceAll(page, "{{allCount}}", to_string(all.size()));
        page = replaceAll(page, "{{pendingCount}}", to_string(pending));
        page = replaceAll(page, "{{progressCount}}", to_string(progress));
        page = replaceAll(page, "{{resolvedCount}}", to_string(resolved));
        page = replaceAll(page, "{{rejectedCount}}", to_string(rejected));
        page = replaceAll(page, "{{complaintRows}}", rows);
        res.set_content(page, "text/html");
    });

    server.Get("/admin/complaints/export.csv", [](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;

        string csv = "ID,Category,Location,Description,Status,Filed on,Resident,Email,Phone,Address\n";
        for (const Complaint& c : listAllComplaints()) {
            User resident;
            bool known = findUser(c.login, resident);
            csv += csvCell(c.id) + "," + csvCell(c.category) + "," + csvCell(c.location) + "," +
                   csvCell(c.description) + "," + csvCell(c.status) + "," + csvCell(c.filedDate) + "," +
                   csvCell(known ? resident.fullName : c.login) + "," + csvCell(c.login) + "," +
                   csvCell(known ? resident.phone : "") + "," + csvCell(known ? resident.address : "") + "\n";
        }
        res.set_header("Content-Disposition", "attachment; filename=\"agap-complaints.csv\"");
        res.set_content(csv, "text/csv");
    });

    server.Get(R"(/admin/complaints/([A-Za-z0-9\-]+))", [](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;

        Complaint complaint;
        if (!findComplaint(req.matches[1], complaint)) {
            res.status = 404;
            res.set_content("<h1>Complaint not found</h1><p><a href=\"/admin/complaints\">Back to complaints</a></p>", "text/html");
            return;
        }

        User resident;
        bool known = findUser(complaint.login, resident);

        string statusOptions = "";
        for (string word : {"Pending", "In progress", "Resolved", "Rejected"}) {
            statusOptions += "<option" + string(word == complaint.status ? " selected" : "") + ">" + word + "</option>";
        }

        string photoBlock = complaint.photoFile.empty()
            ? "<p class=\"muted\">No photo was attached.</p>"
            : "<img src=\"/admin/complaints/" + escapeHtml(complaint.id) + "/photo\" alt=\"Photo from the resident\" "
              "class=\"complaint-photo\" style=\"margin-top:10px\">";

        string page = fillAdminShell(readFile("../frontend/staff/complaint_details_admin.html"), staff);
        page = replaceAll(page, "{{id}}", escapeHtml(complaint.id));
        page = replaceAll(page, "{{title}}", escapeHtml(complaintTitle(complaint)));
        page = replaceAll(page, "{{category}}", escapeHtml(complaint.category));
        page = replaceAll(page, "{{status}}", escapeHtml(complaint.status));
        page = replaceAll(page, "{{statusBadgeClass}}", statusBadgeClassFor(complaint.status));
        page = replaceAll(page, "{{statusOptions}}", statusOptions);
        page = replaceAll(page, "{{filedDate}}", escapeHtml(complaint.filedDate));
        page = replaceAll(page, "{{residentName}}", escapeHtml(known ? resident.fullName : complaint.login));
        page = replaceAll(page, "{{residentEmail}}", escapeHtml(complaint.login));
        page = replaceAll(page, "{{residentPhone}}", escapeHtml(known && !resident.phone.empty() ? resident.phone : "Not added"));
        page = replaceAll(page, "{{residentAddress}}", escapeHtml(known && !resident.address.empty() ? resident.address : "Not added"));
        page = replaceAll(page, "{{location}}", escapeHtml(complaint.location));
        page = replaceAll(page, "{{description}}", escapeHtml(complaint.description));
        page = replaceAll(page, "{{photoBlock}}", photoBlock);
        res.set_content(page, "text/html");
    });

    server.Post(R"(/admin/complaints/([A-Za-z0-9\-]+)/status)", [](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;

        string id = req.matches[1];
        string status = cleanField(req.get_param_value("status"));
        if (!isValidStatus(status) || !updateComplaintStatus(id, status)) {
            res.status = 400;
            res.set_content("Could not change that complaint's status.", "text/plain");
            return;
        }
        res.set_redirect("/admin/complaints/" + id);
    });

    server.Get(R"(/admin/complaints/([A-Za-z0-9\-]+)/photo)", [](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;

        Complaint complaint;
        if (!findComplaint(req.matches[1], complaint) || complaint.photoFile.empty()) {
            res.status = 404;
            res.set_content("No photo.", "text/plain");
            return;
        }
        string path = "../data/complaint_photos/" + safeFileName(complaint.photoFile);
        ifstream file(path, ios::binary);
        if (!file) {
            res.status = 404;
            res.set_content("No photo.", "text/plain");
            return;
        }
        stringstream bytes;
        bytes << file.rdbuf();
        bool isPng = complaint.photoFile.size() > 4 && complaint.photoFile.substr(complaint.photoFile.size() - 4) == ".png";
        res.set_content(bytes.str(), isPng ? "image/png" : "image/jpeg");
    });

    server.Get(R"(/admin/complaints/([A-Za-z0-9\-]+)/report\.pdf)", [](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;

        string id = req.matches[1];
        Complaint complaint;
        if (!findComplaint(id, complaint)) {
            res.status = 404;
            res.set_content("Complaint not found.", "text/plain");
            return;
        }
        User resident;
        bool known = findUser(complaint.login, resident);

        ComplaintReportData reportData;
        reportData.id = complaint.id;
        reportData.title = complaintTitle(complaint);
        reportData.category = complaint.category;
        reportData.status = complaint.status;
        reportData.filedDate = complaint.filedDate;
        reportData.residentName = known ? resident.fullName : complaint.login;
        reportData.residentEmail = complaint.login;
        reportData.residentPhone = known ? resident.phone : "";
        reportData.residentAddress = known ? resident.address : "";
        reportData.location = complaint.location;
        reportData.description = complaint.description;
        if (!complaint.photoFile.empty() && complaint.photoFile.size() > 4 &&
            complaint.photoFile.substr(complaint.photoFile.size() - 4) == ".jpg") {
            reportData.photoPath = "../data/complaint_photos/" + safeFileName(complaint.photoFile);
        }

        filesystem::create_directories("../data/reports");
        string pdfPath = "../data/reports/" + safeFileName(id) + ".pdf";

        if (!generateComplaintPdf(pdfPath, reportData)) {
            res.status = 500;
            res.set_content("Could not generate the PDF report.", "text/plain");
            return;
        }

        ifstream pdfFile(pdfPath, ios::binary);
        stringstream pdfBytes;
        pdfBytes << pdfFile.rdbuf();
        res.set_header("Content-Disposition", "inline; filename=\"" + id + "-report.pdf\"");
        res.set_content(pdfBytes.str(), "application/pdf");
    });

    auto filteredReportComplaints = [](const httplib::Request& req) {
        string month = req.has_param("month") ? req.get_param_value("month") : currentMonthKey();
        string category = req.has_param("category") ? req.get_param_value("category") : "all";
        if (month.size() != 7 || month[4] != '-') month = currentMonthKey();
        if (category != "all" && category != "Garbage" && category != "Noise" && category != "Road and lighting" && category != "Flooding" && category != "Others") category = "all";
        vector<Complaint> selected;
        for (const Complaint& c : listAllComplaints()) {
            if (monthKeyFor(c.filedDate) != month) continue;
            if (category != "all" && c.category != category) continue;
            selected.push_back(c);
        }
        return selected;
    };
    server.Get("/admin/reports", [filteredReportComplaints](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;
        vector<string> categories = {"Garbage", "Noise", "Road and lighting", "Flooding", "Others"};
        string month = req.has_param("month") ? req.get_param_value("month") : currentMonthKey();
        if (month.size() != 7 || month[4] != '-') month = currentMonthKey();
        string category = req.has_param("category") ? req.get_param_value("category") : "all";
        bool knownCategory = category == "all";
        for (const string& item : categories) if (category == item) knownCategory = true;
        if (!knownCategory) category = "all";
        vector<Complaint> allMonth;
        for (const Complaint& c : listAllComplaints()) if (monthKeyFor(c.filedDate) == month) allMonth.push_back(c);
        vector<Complaint> selected = filteredReportComplaints(req);
        if (req.has_param("category") && category == "all") {
            selected.clear();
            for (const Complaint& c : allMonth) selected.push_back(c);
        }
        int resolved = 0, open = 0, pending = 0, progress = 0, rejected = 0;
        for (const Complaint& c : selected) {
            if (c.status == "Resolved") ++resolved;
            else if (c.status == "Pending") { ++open; ++pending; }
            else if (c.status == "In progress") { ++open; ++progress; }
            else if (c.status == "Rejected") ++rejected;
        }
        int count = (int)selected.size();
        int rate = count ? (resolved * 100 / count) : 0;
        string monthOptions;
        set<string> availableMonths;
        availableMonths.insert(currentMonthKey());
        for (const Complaint& c : listAllComplaints()) {
            string key = monthKeyFor(c.filedDate);
            if (!key.empty()) availableMonths.insert(key);
        }
        for (auto it = availableMonths.rbegin(); it != availableMonths.rend(); ++it)
            monthOptions += "<option value=\"" + escapeHtml(*it) + "\"" + (*it == month ? " selected" : "") + ">" + escapeHtml(monthLabel(*it)) + "</option>";
        string categoryOptions = "<option value=\"all\"" + string(category == "all" ? " selected" : "") + ">All categories</option>";
        for (const string& item : categories) categoryOptions += "<option value=\"" + escapeHtml(item) + "\"" + (category == item ? " selected" : "") + ">" + escapeHtml(item) + "</option>";
        string categoryRows;
        for (const string& item : categories) {
            int filed = 0, done = 0, stillOpen = 0;
            for (const Complaint& c : selected) if (c.category == item) {
                ++filed;
                if (c.status == "Resolved") ++done;
                if (c.status == "Pending" || c.status == "In progress") ++stillOpen;
            }
            categoryRows += "<tr><td>" + escapeHtml(item) + "</td><td>" + to_string(filed) + "</td><td>" + to_string(done) + "</td><td>" + to_string(stillOpen) + "</td></tr>";
        }
        int denominator = max(count, 1);
        string statusBars, statusLegend;
        const vector<pair<string, pair<int, string>>> statuses = {
            {"Pending", {pending, "status-pending"}}, {"In progress", {progress, "status-progress"}},
            {"Resolved", {resolved, "status-resolved"}}, {"Rejected", {rejected, "status-rejected"}}
        };
        for (const auto& entry : statuses) {
            int percent = entry.second.first * 100 / denominator;
            statusBars += "<span class=\"" + entry.second.second + "\" style=\"width:" + to_string(percent) + "%\" title=\"" + escapeHtml(entry.first) + ": " + to_string(entry.second.first) + "\"></span>";
            statusLegend += "<span><i class=\"" + entry.second.second + "\"></i>" + escapeHtml(entry.first) + " <strong>" + to_string(entry.second.first) + "</strong></span>";
        }
        map<string, int> puroks;
        for (const Complaint& c : selected) {
            string place = c.location;
            string lower = toLower(place);
            size_t p = lower.find("purok");
            if (p != string::npos) {
                size_t end = place.find_first_of(",;", p);
                place = place.substr(p, end == string::npos ? string::npos : end - p);
            } else place = "Unspecified";
            ++puroks[place];
        }
        string purokBars;
        int maxPurok = 1;
        for (const auto& item : puroks) maxPurok = max(maxPurok, item.second);
        for (const auto& item : puroks) purokBars += "<div class=\"purok-row\"><span class=\"purok-name\">" + escapeHtml(item.first) + "</span><div class=\"purok-track\"><div class=\"purok-fill\" style=\"width:" + to_string(item.second * 100 / maxPurok) + "%\"></div></div><span class=\"purok-count\">" + to_string(item.second) + "</span></div>";
        if (purokBars.empty()) purokBars = "<p class=\"empty-report\">No complaints for this period.</p>";
        string page = fillAdminShell(readFile("../frontend/staff/reports_admin.html"), staff);
        const vector<pair<string, string>> replacements = {
            {"{{monthOptions}}", monthOptions}, {"{{categoryOptions}}", categoryOptions},
            {"{{monthValue}}", escapeHtml(month)}, {"{{categoryValue}}", escapeHtml(category)},
            {"{{filedCount}}", to_string(count)}, {"{{resolvedCount}}", to_string(resolved)},
            {"{{resolutionRate}}", to_string(rate)}, {"{{openCount}}", to_string(open)},
            {"{{monthLabel}}", escapeHtml(monthLabel(month))}, {"{{categoryRows}}", categoryRows},
            {"{{statusBars}}", statusBars}, {"{{statusLegend}}", statusLegend}, {"{{purokBars}}", purokBars}
        };
        for (const auto& replacement : replacements) page = replaceAll(page, replacement.first, replacement.second);
        res.set_content(page, "text/html");
    });
    server.Get("/admin/reports/export.csv", [filteredReportComplaints](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireStaff(req, res, staff)) return;
        string csv = "ID,Category,Location,Description,Status,Filed date\n";
        for (const Complaint& c : filteredReportComplaints(req))
            csv += csvCell(c.id) + "," + csvCell(c.category) + "," + csvCell(c.location) + "," + csvCell(c.description) + "," + csvCell(c.status) + "," + csvCell(c.filedDate) + "\n";
        res.set_header("Content-Disposition", "attachment; filename=agap-complaint-report.csv");
        res.set_content(csv, "text/csv; charset=utf-8");
    });
    auto requireAdminRole = [&](const httplib::Request& req, httplib::Response& res, Staff& staffOut) -> bool {
        string login = loginFromSession(getAdminSessionToken(req));
        if (login.empty() || !findStaff(login, staffOut) || staffOut.status != "active") {
            res.set_redirect("/signin.html");
            return false;
        }
        if (staffOut.role != "admin") {
            res.status = 403;
            res.set_content("<h1>Administrator only</h1><p>Your staff account doesn't have permission "
                             "to view this page.</p><p><a href=\"/admin/dashboard\">Back to dashboard</a></p>",
                             "text/html");
            return false;
        }
        return true;
    };

    
    server.Get("/admin/settings", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;

        string tab = req.get_param_value("tab");
        if (tab.empty()) tab = "staff";

        string tabContent;

        if (tab == "staff") {
            string rows = "";
            for (const Staff& s : listAllStaff()) {
                bool isSelf = (s.login == staff.login);
                string toggleAction = (s.status == "active") ? "deactivate" : "activate";
                string toggleLabel = (s.status == "active") ? "Deactivate" : "Activate";
                string confirmAttr = (toggleAction == "deactivate")
                    ? " onsubmit=\"return confirm('Deactivate this account? They can no longer sign in. You can activate it again later.')\""
                    : "";
                string actionsHtml =
                    "<form method=\"post\" action=\"/admin/settings/staff/" + escapeHtml(s.login) + "/reset-password\" "
                    "style=\"display:inline-flex;gap:6px;align-items:center;margin-right:10px\" "
                    "onsubmit=\"return confirm('Reset this staff account password?')\">"
                    "<input type=\"password\" name=\"newPassword\" placeholder=\"New password\" required minlength=\"6\" maxlength=\"128\" "
                    "style=\"width:130px;padding:5px 8px;font-size:12px;border:1px solid var(--border);border-radius:4px\">"
                    "<button type=\"submit\" class=\"link-button\">Reset</button></form>";
                if (isSelf) {
                    actionsHtml += " <span class=\"muted\">(you)</span>";
                } else {
                    actionsHtml +=
                        "<form method=\"post\" action=\"/admin/settings/staff/" + escapeHtml(s.login) + "/" + toggleAction + "\" "
                        "style=\"display:inline\"" + confirmAttr + ">"
                        "<button type=\"submit\" class=\"link-button\">" + toggleLabel + "</button></form>";
                }

                rows += "<tr><td><span class=\"avatar\" style=\"width:28px;height:28px;font-size:11px\">" +
                        escapeHtml(initialsOf(s.fullName)) + "</span> " + escapeHtml(s.fullName) + "</td>"
                        "<td>" + escapeHtml(s.login) + "</td>"
                        "<td>" + escapeHtml(s.role == "admin" ? "Administrator" : "Staff") + "</td>"
                        "<td><span class=\"badge " + (s.status == "active" ? "badge-resolved" : "badge-rejected") +
                        "\">" + escapeHtml(s.status == "active" ? "Active" : "Inactive") + "</span></td>"
                        "<td>" + actionsHtml + "</td></tr>";
            }

            string addError = "";
            if (req.get_param_value("error") == "missing_fields") {
                addError = "<div class=\"alert-error\">Please fill in every field.</div>";
            } else if (req.get_param_value("error") == "duplicate") {
                addError = "<div class=\"alert-error\">That email is already registered.</div>";
            } else if (req.get_param_value("error") == "save_failed") {
                addError = "<div class=\"alert-error\">Could not save the requested change.</div>";
            } else if (req.get_param_value("error") == "invalid_password") {
                addError = "<div class=\"alert-error\">Password must be 6 to 128 characters.</div>";
            }

            tabContent =
                "<div class=\"admin-panel\"><h3>Staff accounts</h3>"
                "<table class=\"admin-table\"><thead><tr><th>Name</th><th>Email</th><th>Role</th>"
                "<th>Status</th><th>Actions</th></tr></thead><tbody>" + rows + "</tbody></table>"
                "<p class=\"table-note\">Only the administrator can add, deactivate or reset staff accounts. "
                "Staff sign in with the email and temporary password the administrator sets.</p></div>"

                "<div class=\"admin-panel\">" + addError + "<h3>Add staff</h3>"
                "<form method=\"post\" action=\"/admin/settings/staff/add\">"
                "<div class=\"field\"><label>Full name</label><input type=\"text\" name=\"fullname\" required></div>"
                "<div class=\"field\"><label>Email</label><input type=\"email\" name=\"login\" required></div>"
                "<div class=\"field\"><label>Temporary password</label>"
                "<input type=\"password\" name=\"password\" minlength=\"6\" placeholder=\"At least 6 characters\" required></div>"
                "<div class=\"field\"><label>Role</label><select name=\"role\">"
                "<option value=\"staff\">Staff</option><option value=\"admin\">Administrator</option></select></div>"
                "<button type=\"submit\" class=\"btn btn-blue btn-full\">Add staff</button></form></div>";

        } else if (tab == "teams") {
            string teamRows = "";
            for (const string& team : listNamedItems(TEAMS_FILE)) {
                teamRows += "<div class=\"attention-item\"><span>" + escapeHtml(team) + "</span>"
                            "<form method=\"post\" action=\"/admin/settings/teams/remove\" onsubmit=\"return confirm('Remove this team?')\">"
                            "<input type=\"hidden\" name=\"name\" value=\"" + escapeHtml(team) + "\">"
                            "<button type=\"submit\" class=\"link-button\">Remove</button></form></div>";
            }
            if (teamRows.empty()) teamRows = "<p class=\"muted\">No response teams yet.</p>";

            string purokRows = "";
            for (const string& purok : listNamedItems(PUROKS_FILE)) {
                purokRows += "<div class=\"attention-item\"><span>" + escapeHtml(purok) + "</span>"
                             "<form method=\"post\" action=\"/admin/settings/puroks/remove\" onsubmit=\"return confirm('Remove this purok?')\">"
                             "<input type=\"hidden\" name=\"name\" value=\"" + escapeHtml(purok) + "\">"
                             "<button type=\"submit\" class=\"link-button\">Remove</button></form></div>";
            }
            if (purokRows.empty()) purokRows = "<p class=\"muted\">No puroks yet.</p>";

            tabContent =
                "<div class=\"admin-grid\"><section><div class=\"admin-panel\"><h3>Response teams</h3>" + teamRows +
                "<form method=\"post\" action=\"/admin/settings/teams/add\" style=\"display:flex;gap:8px;margin-top:14px\">"
                "<input type=\"text\" name=\"name\" placeholder=\"Team name\" required style=\"flex:1\">"
                "<button type=\"submit\" class=\"btn btn-blue\">Add team</button></form></div></section>"
                "<aside><div class=\"admin-panel\"><h3>Puroks</h3>" + purokRows +
                "<form method=\"post\" action=\"/admin/settings/puroks/add\" style=\"display:flex;gap:8px;margin-top:14px\">"
                "<input type=\"text\" name=\"name\" placeholder=\"Purok name\" required style=\"flex:1\">"
                "<button type=\"submit\" class=\"btn btn-blue\">Add purok</button></form></div></aside></div>"
                "<p class=\"sample-tag\" style=\"display:block;margin-top:14px\">These lists are stored for "
                "future use. They aren't connected to the complaint \"Assign to\" field or address fields yet.</p>";

        } else {
            string label = (tab == "categories") ? "Categories" : "Barangay info";
            tabContent = "<div class=\"admin-panel\"><h3>" + label + "</h3>"
                         "<p class=\"muted\">This tab wasn't built in this session. Complaint categories "
                         "are currently fixed in the code (Garbage, Noise, Road and lighting, Flooding, Others) "
                         "rather than editable here, and the barangay's contact details are hardcoded in the "
                         "footer of each page rather than stored and editable.</p></div>";
        }

        VerificationCounts counts = countResidentsByVerification();
        string navBadge = (counts.pending > 0)
            ? "<span class=\"nav-count\">" + to_string(counts.pending) + "</span>"
            : "";

        string page = readFile("../frontend/staff/settings_admin.html");
        page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
        page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
        page = replaceAll(page, "{{residentsNavBadge}}", navBadge);
        page = replaceAll(page, "{{tabStaffActive}}", tab == "staff" ? "active" : "");
        page = replaceAll(page, "{{tabTeamsActive}}", tab == "teams" ? "active" : "");
        page = replaceAll(page, "{{tabCategoriesActive}}", tab == "categories" ? "active" : "");
        page = replaceAll(page, "{{tabInfoActive}}", tab == "info" ? "active" : "");
        page = replaceAll(page, "{{tabContent}}", tabContent);
        res.set_content(page, "text/html");
    });

    
    server.Post("/admin/settings/staff/add", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;

        string fullName = cleanField(req.get_param_value("fullname"));
        string login = toLower(cleanField(req.get_param_value("login")));
        string password = req.get_param_value("password");
        string role = req.get_param_value("role");
        if (role != "admin") role = "staff";

        if (fullName.empty() || login.empty() || password.length() < 6) {
            res.set_redirect("/admin/settings?tab=staff&error=missing_fields");
            return;
        }
        Staff existing;
        User existingResident;
        size_t at = login.find('@');
        if (at == string::npos || at == 0 || at + 1 >= login.size()) {
            res.set_redirect("/admin/settings?tab=staff&error=missing_fields");
            return;
        }
        if (findStaff(login, existing) || findUser(login, existingResident)) {
            res.set_redirect("/admin/settings?tab=staff&error=duplicate");
            return;
        }

        Staff newStaff;
        newStaff.login = login;
        newStaff.fullName = fullName;
        newStaff.role = role;
        newStaff.salt = makeSalt();
        newStaff.passwordHash = hashPassword(newStaff.salt, password);
        newStaff.status = "active";
        if (!addStaff(newStaff)) {
            res.set_redirect("/admin/settings?tab=staff&error=save_failed");
            return;
        }
        res.set_redirect("/admin/settings?tab=staff");
    });

    
    server.Post(R"(/admin/settings/staff/([^/]+)/reset-password)", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;

        string targetLogin = req.matches[1];
        string newPassword = req.get_param_value("newPassword");
        if (newPassword.length() < 6 || newPassword.length() > 128) {
            res.set_redirect("/admin/settings?tab=staff&error=invalid_password");
            return;
        }
        string salt = makeSalt();
        if (!setStaffPassword(targetLogin, salt, hashPassword(salt, newPassword))) {
            res.set_redirect("/admin/settings?tab=staff&error=save_failed");
            return;
        }
        res.set_redirect("/admin/settings?tab=staff");
    });

    
    
    
    
    server.Post(R"(/admin/settings/staff/([^/]+)/deactivate)", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;
        string targetLogin = req.matches[1];
        if (targetLogin != staff.login && !setStaffStatus(targetLogin, "inactive")) {
            res.set_redirect("/admin/settings?tab=staff&error=save_failed");
            return;
        }
        res.set_redirect("/admin/settings?tab=staff");
    });
    server.Post(R"(/admin/settings/staff/([^/]+)/activate)", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;
        if (!setStaffStatus(req.matches[1], "active")) {
            res.set_redirect("/admin/settings?tab=staff&error=save_failed");
            return;
        }
        res.set_redirect("/admin/settings?tab=staff");
    });

    
    server.Post("/admin/settings/teams/add", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;
        string name = cleanField(req.get_param_value("name"));
        if (!name.empty() && !addNamedItem(TEAMS_FILE, name)) {
            res.set_redirect("/admin/settings?tab=teams&error=save_failed");
            return;
        }
        res.set_redirect("/admin/settings?tab=teams");
    });
    server.Post("/admin/settings/teams/remove", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;
        removeNamedItem(TEAMS_FILE, req.get_param_value("name"));
        res.set_redirect("/admin/settings?tab=teams");
    });
    server.Post("/admin/settings/puroks/add", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;
        string name = cleanField(req.get_param_value("name"));
        if (!name.empty() && !addNamedItem(PUROKS_FILE, name)) {
            res.set_redirect("/admin/settings?tab=teams&error=save_failed");
            return;
        }
        res.set_redirect("/admin/settings?tab=teams");
    });
    server.Post("/admin/settings/puroks/remove", [&requireAdminRole](const httplib::Request& req, httplib::Response& res) {
        Staff staff;
        if (!requireAdminRole(req, res, staff)) return;
        removeNamedItem(PUROKS_FILE, req.get_param_value("name"));
        res.set_redirect("/admin/settings?tab=teams");
    });

    server.Get("/admin/residents", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }

        VerificationCounts counts = countResidentsByVerification();
        int allCount = counts.pending + counts.verified + counts.rejected;
        string navBadge = (counts.pending > 0)
            ? "<span class=\"nav-count\">" + to_string(counts.pending) + "</span>"
            : "";

        string rows = "";
        for (const User& resident : listAllUsers()) {
            if (resident.verification == "unverified") continue;

            rows += "<tr data-status=\"" + escapeHtml(resident.verification) + "\">"
                    "<td><span class=\"avatar\" style=\"width:28px;height:28px;font-size:11px\">"
                    + escapeHtml(initialsOf(resident.fullName)) + "</span> "
                    + escapeHtml(resident.fullName) + "</td>"
                    "<td>" + escapeHtml(resident.phone) + "</td>"
                    "<td>" + escapeHtml(resident.address) + "</td>"
                    "<td>" + escapeHtml(resident.idType.empty() ? "—" : resident.idType) + "</td>"
                    "<td><span class=\"badge " + residentStatusBadgeClass(resident.verification) + "\">"
                    + residentStatusLabel(resident.verification) + "</span></td>"
                    "<td><a href=\"/admin/residents/" + escapeHtml(resident.login) + "\">Review &rsaquo;</a></td>"
                    "</tr>";
        }
        if (rows.empty()) {
            rows = "<tr><td colspan=\"6\" class=\"muted\">No residents have submitted an ID yet.</td></tr>";
        }

        string page = readFile("../frontend/staff/residents_admin.html");
        page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
        page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
        page = replaceAll(page, "{{residentsNavBadge}}", navBadge);
        page = replaceAll(page, "{{allCount}}", to_string(allCount));
        page = replaceAll(page, "{{pendingCount}}", to_string(counts.pending));
        page = replaceAll(page, "{{verifiedCount}}", to_string(counts.verified));
        page = replaceAll(page, "{{rejectedCount}}", to_string(counts.rejected));
        page = replaceAll(page, "{{residentRows}}", rows);
        res.set_content(page, "text/html");
    });

    server.Get(R"(/admin/residents/([^/]+))", [](const httplib::Request& req, httplib::Response& res) {
        string staffLogin = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (staffLogin.empty() || !findStaff(staffLogin, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }

        string residentLogin = req.matches[1];
        User resident;
        if (!findUser(residentLogin, resident)) {
            res.status = 404;
            res.set_content("<h1>Resident not found</h1><p><a href=\"/admin/residents\">Back to residents</a></p>", "text/html");
            return;
        }

        VerificationCounts counts = countResidentsByVerification();
        string navBadge = (counts.pending > 0)
            ? "<span class=\"nav-count\">" + to_string(counts.pending) + "</span>"
            : "";

        string photoInfo = "No ID photo on file.";
        string jpgPath = "../data/ids/" + safeFileName(residentLogin) + ".jpg";
        string pngPath = "../data/ids/" + safeFileName(residentLogin) + ".png";
        string photoPath = filesystem::exists(jpgPath) ? jpgPath : (filesystem::exists(pngPath) ? pngPath : "");
        if (!photoPath.empty()) {
            string kind = (photoPath == jpgPath) ? "JPG" : "PNG";
            double kb = filesystem::file_size(photoPath) / 1024.0;
            char sizeBuffer[32];
            if (kb < 1024) snprintf(sizeBuffer, sizeof(sizeBuffer), "%d KB", (int)kb);
            else snprintf(sizeBuffer, sizeof(sizeBuffer), "%.1f MB", kb / 1024.0);
            photoInfo = kind + ", " + sizeBuffer;
        }

        string reasonPanel = "";
        if (resident.verification == "rejected" && !resident.rejectionReason.empty()) {
            reasonPanel = "<div class=\"admin-panel\"><h3>Previous rejection reason</h3><p>"
                           + escapeHtml(resident.rejectionReason) + "</p></div>";
        }

        string errorBanner = "";
        if (req.get_param_value("error") == "reason_required") {
            errorBanner = "<div class=\"alert-error\">Please give a reason before rejecting.</div>";
        }

        string page = readFile("../frontend/staff/resident_review_admin.html");
        page = replaceAll(page, "{{errorBanner}}", errorBanner);
        page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
        page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
        page = replaceAll(page, "{{residentsNavBadge}}", navBadge);
        page = replaceAll(page, "{{login}}", escapeHtml(resident.login));
        page = replaceAll(page, "{{fullName}}", escapeHtml(resident.fullName));
        page = replaceAll(page, "{{phone}}", escapeHtml(resident.phone.empty() ? "—" : resident.phone));
        page = replaceAll(page, "{{address}}", escapeHtml(resident.address.empty() ? "—" : resident.address));
        page = replaceAll(page, "{{idType}}", escapeHtml(resident.idType.empty() ? "—" : resident.idType));
        page = replaceAll(page, "{{photoInfo}}", escapeHtml(photoInfo));
        page = replaceAll(page, "{{statusLabel}}", residentStatusLabel(resident.verification));
        page = replaceAll(page, "{{statusBadgeClass}}", residentStatusBadgeClass(resident.verification));
        page = replaceAll(page, "{{reasonPanel}}", reasonPanel);
        res.set_content(page, "text/html");
    });

    server.Post(R"(/admin/residents/([^/]+)/approve)", [](const httplib::Request& req, httplib::Response& res) {
        string staffLogin = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (staffLogin.empty() || !findStaff(staffLogin, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }
        string residentLogin = req.matches[1];
        setVerificationDecision(residentLogin, "verified", "");
        res.set_redirect("/admin/residents");
    });

    server.Post(R"(/admin/residents/([^/]+)/reject)", [](const httplib::Request& req, httplib::Response& res) {
        string staffLogin = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (staffLogin.empty() || !findStaff(staffLogin, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }
        string residentLogin = req.matches[1];
        string reason = cleanField(req.get_param_value("reason"));

        if (reason.empty()) {
            res.set_redirect("/admin/residents/" + residentLogin + "?error=reason_required");
            return;
        }
        setVerificationDecision(residentLogin, "rejected", reason);
        res.set_redirect("/admin/residents");
    });

    server.Get(R"(/admin/residents/([^/]+)/id-photo)", [](const httplib::Request& req, httplib::Response& res) {
        string staffLogin = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (staffLogin.empty() || !findStaff(staffLogin, staff) || staff.status != "active") {
            res.status = 404;
            return;
        }

        string residentLogin = req.matches[1];
        string base = "../data/ids/" + safeFileName(residentLogin);
        string jpgPath = base + ".jpg";
        string pngPath = base + ".png";

        string path, contentType;
        if (filesystem::exists(jpgPath)) { path = jpgPath; contentType = "image/jpeg"; }
        else if (filesystem::exists(pngPath)) { path = pngPath; contentType = "image/png"; }
        else { res.status = 404; return; }

        ifstream file(path, ios::binary);
        stringstream bytes;
        bytes << file.rdbuf();
        res.set_content(bytes.str(), contentType);
    });

    server.Get("/admin/announcements", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }

        VerificationCounts counts = countResidentsByVerification();
        string navBadge = (counts.pending > 0)
            ? "<span class=\"nav-count\">" + to_string(counts.pending) + "</span>"
            : "";

        string list = "";
        for (const Announcement& a : listAnnouncements()) {
            list += "<div class=\"announcement-row\">"
                    "<div><strong>" + escapeHtml(a.title) + "</strong>"
                    "<p class=\"muted\">" + escapeHtml(a.category) + " &middot; Posted " + escapeHtml(a.postedDate) + "</p>"
                    "<p>" + escapeHtml(a.message) + "</p></div>"
                    "<form method=\"post\" action=\"/admin/announcements/" + escapeHtml(a.id) + "/delete\">"
                    "<button type=\"submit\" class=\"link-button\">Delete</button></form>"
                    "</div>";
        }
        if (list.empty()) {
            list = "<p class=\"muted\">No announcements posted yet.</p>";
        }

        string errorBanner = "";
        if (req.get_param_value("error") == "missing_fields") {
            errorBanner = "<div class=\"alert-error\">Please fill in the title and message.</div>";
        }

        string page = readFile("../frontend/staff/announcements_admin.html");
        page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
        page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
        page = replaceAll(page, "{{residentsNavBadge}}", navBadge);
        page = replaceAll(page, "{{announcementList}}", list);
        page = replaceAll(page, "{{errorBanner}}", errorBanner);
        res.set_content(page, "text/html");
    });

    server.Post("/admin/announcements/add", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }

        string title = cleanField(req.get_param_value("title"));
        string category = cleanField(req.get_param_value("category"));
        string message = cleanField(req.get_param_value("message"));
        if (category.empty()) category = "General";

        if (title.empty() || message.empty()) {
            res.set_redirect("/admin/announcements?error=missing_fields");
            return;
        }

        Announcement a;
        a.id = to_string(time(nullptr));
        a.title = title;
        a.category = category;
        a.message = message;
        a.postedDate = todayFormatted();
        addAnnouncement(a);
        res.set_redirect("/admin/announcements");
    });

    server.Post(R"(/admin/announcements/([^/]+)/delete)", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }
        deleteAnnouncement(req.matches[1]);
        res.set_redirect("/admin/announcements");
    });

    server.Get("/admin/signout", [](const httplib::Request& req, httplib::Response& res) {
        deleteSession(getAdminSessionToken(req));
        res.set_header("Set-Cookie", "admin_session=; Path=/; Max-Age=0");
        res.set_redirect("/signin.html");
    });

    server.Get("/signout", [](const httplib::Request& req, httplib::Response& res) {
        deleteSession(getSessionToken(req));
        res.set_header("Set-Cookie", "session=; Path=/; Max-Age=0");
        res.set_redirect("/");
    });

    cout << "AGAP is running at http://localhost:8080" << endl;
    server.listen("localhost", 8080);
}
