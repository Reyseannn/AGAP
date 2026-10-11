#include "httplib.h"
#include "auth.h"
#include "storage.h"
#include "staff_storage.h"
#include "sample_complaints.h"
#include "pdf_report.h"
#include "announcement_storage.h"
#include "complaint_storage.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace std;

// ---------- Small helper functions ----------

// Reads a whole file into a string. If the file can't be found, this returns
// a visible error page instead of silently sending back an empty page — a
// blank white screen in the browser gives no clue what went wrong, but this
// does. The most common cause: the server wasn't started from inside the
// backend folder, so its "../frontend/..." paths don't point anywhere real.
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

// Replaces every "from" inside "text" with "to".
static string replaceAll(string text, const string& from, const string& to) {
    size_t position = 0;
    while ((position = text.find(from, position)) != string::npos) {
        text.replace(position, from.length(), to);
        position += to.length();
    }
    return text;
}

// Stops a name like <b>Juan</b> from becoming real HTML on the page.
static string escapeHtml(string text) {
    text = replaceAll(text, "&", "&amp;");  // must be first
    text = replaceAll(text, "<", "&lt;");
    text = replaceAll(text, ">", "&gt;");
    text = replaceAll(text, "\"", "&quot;");
    return text;
}

// Removes tabs/newlines (they would break users.txt) and spaces at the ends.
static string cleanField(string text) {
    for (char& c : text) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    size_t start = text.find_first_not_of(' ');
    if (start == string::npos) return "";
    size_t end = text.find_last_not_of(' ');
    return text.substr(start, end - start + 1);
}

// Builds initials like "Juan Dela Cruz" -> "JC" (first letter of the first and last word).
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

// Reads one named cookie's value out of the Cookie header.
// Residents use the "session" cookie, staff use a separate "admin_session"
// cookie, so a resident login and a staff login never interfere with each other.
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

static string getSessionToken(const httplib::Request& req) {
    return getCookie(req, "session");
}

static string getAdminSessionToken(const httplib::Request& req) {
    return getCookie(req, "admin_session");
}

// Turns a complaint's status word into the matching badge CSS class
// (these classes already exist in style.css, used on the resident side too).
static string statusBadgeClassFor(const string& status) {
    if (status == "Pending") return "badge-pending";
    if (status == "In progress") return "badge-progress";
    if (status == "Resolved") return "badge-resolved";
    if (status == "Rejected") return "badge-rejected";
    return "badge-pending";
}

// Same idea, but for a resident's verification status (different words,
// shown to staff on the Residents pages).
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

// Today's date written out like the Figma shows it: "Oct 3, 2026".
// Built from separate pieces (not one strftime format) because the "no leading
// zero on the day" flag isn't the same on every compiler, and this way works
// everywhere.
static string todayFormatted() {
    time_t now = time(nullptr);
    tm local = *localtime(&now);

    char monthBuffer[8];
    strftime(monthBuffer, sizeof(monthBuffer), "%b", &local);  // "Oct"

    return string(monthBuffer) + " " + to_string(local.tm_mday) + ", " + to_string(local.tm_year + 1900);
}

// Sends sign in / sign up page again, with an error message shown on it.
static void showAuthPage(httplib::Response& res, const string& file, const string& error) {
    string page = readFile("../frontend/" + file);
    string banner = "";
    if (!error.empty()) {
        banner = "<div class=\"alert-error\">" + escapeHtml(error) + "</div>";
    }
    page = replaceAll(page, "<!--ERROR-->", banner);
    res.set_content(page, "text/html");
}

// Builds verify.html for one user: fills in their saved phone/address (so they
// don't retype them), the "you already have a pending request" notice, and an
// optional error banner. Used both for a normal page visit and for showing the
// form again after a mistake.
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

// Logs the user in: creates a session, gives the browser its cookie, goes to the dashboard.
static void startSession(httplib::Response& res, const string& login) {
    string token = createSession(login);
    res.set_header("Set-Cookie", "session=" + token + "; Path=/; HttpOnly; SameSite=Lax");
    res.set_redirect("/dashboard");
}

// Turns the saved "verification" field into the CSS class dashboard.html expects.
// "unverified" (a brand-new account) reuses the Figma's welcome screen, state-new.
static string stateClassFor(const string& verification) {
    if (verification == "unverified") return "state-new";
    if (verification == "pending")    return "state-pending";
    if (verification == "rejected")   return "state-rejected";
    return "state-verified";
}

// Keeps only letters, digits, '.', '_' and '-', so a login can't be used to escape the folder
// (for example "../../something"). Used to build a safe file name for the uploaded ID photo.
static string safeFileName(const string& text) {
    string result;
    for (char c : text) {
        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') result += c;
        else result += '_';
    }
    return result;
}

// Shows the file-a-complaint form again, with an optional error banner on it.
static void showComplaintPage(httplib::Response& res, const User& user, const string& error) {
    string page = readFile("../frontend/filecomplaint.html");
    string banner = "";
    if (!error.empty()) {
        banner = "<div class=\"alert-error\">" + escapeHtml(error) + "</div>";
    }
    page = replaceAll(page, "{{errorBanner}}", banner);
    page = replaceAll(page, "{{fullname}}", escapeHtml(user.fullName));
    page = replaceAll(page, "{{phone}}", escapeHtml(user.phone));
    res.set_content(page, "text/html");
}

// ---------- The server ----------

int main() {
    ensureDefaultAdmin();  // creates one starter admin account if data/staff.txt doesn't exist yet

    httplib::Server server;

    // Serves the HTML, CSS and JS files from the frontend folder.
    server.set_mount_point("/", "../frontend");

    // SIGN UP: the form on signup.html sends its data here.
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

    // SIGN IN: one shared form (signin.html) for both residents and staff.
    // A login is sent to the admin dashboard only if it's a real row in
    // data/staff.txt — a file the public /signup form can never write to.
    // Nothing about the email address itself (like its domain) grants access;
    // only actually being a staff account does.
    server.Post("/signin", [](const httplib::Request& req, httplib::Response& res) {
        string login = toLower(cleanField(req.get_param_value("login")));
        string password = req.get_param_value("password");

        // Check staff first. If this email belongs to a staff account, it's
        // authenticated against the staff table only — not also tried as a
        // resident — which keeps the two account types from blurring together.
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

        // Not staff — check the resident table as before.
        User user;
        bool found = findUser(login, user);

        // Same message for "no such account" and "wrong password" in every case
        // above and below, so nobody can guess which emails exist or which are staff.
        if (!found || hashPassword(user.salt, password) != user.passwordHash) {
            showAuthPage(res, "signin.html", "Wrong email or password. Please try again.");
            return;
        }
        startSession(res, login);
    });

    // DASHBOARD (temporary version, we build the real one in Step 6).
    server.Get("/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));

        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");  // not logged in
            return;
        }

        string firstName = user.fullName.substr(0, user.fullName.find(' '));
        string phoneDisplay = user.phone.empty() ? "Not added yet" : user.phone;
        string addressDisplay = user.address.empty() ? "Not added yet" : user.address;

        // When rejected, show the real reason staff gave instead of a generic
        // message. Falls back to a generic line only for old test data saved
        // before rejection reasons existed.
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

        // Real announcements, newest first, same list staff post from /admin/announcements.
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

        // The resident's own real complaints (data/complaints.txt), newest first, max 4.
        string complaintRows = "";
        vector<Complaint> mine = listComplaintsFor(user.login);
        for (size_t i = 0; i < mine.size() && i < 4; i++) {
            complaintRows += "<div class=\"complaint\"><div><strong>" + escapeHtml(mine[i].category) + " - " +
                              escapeHtml(mine[i].location) + "</strong><p>" + escapeHtml(mine[i].id) + " | " +
                              escapeHtml(mine[i].filedDate) + "</p></div><span class=\"badge " +
                              statusBadgeClassFor(mine[i].status) + "\">" + escapeHtml(mine[i].status) + "</span></div>";
        }
        if (complaintRows.empty()) {
            complaintRows = "<p class=\"muted\">You have not filed any complaints yet.</p>";
        }
        page = replaceAll(page, "{{complaintRows}}", complaintRows);
        res.set_content(page, "text/html");
    });

    // VERIFY ACCOUNT page (GET): shows the form with the resident's saved phone/address filled in.
    server.Get("/verify", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        showVerifyPage(res, user, "");
    });

    // VERIFY ACCOUNT (POST): the form on verify.html sends the phone, address and ID photo here.
    server.Post("/verify", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");  // not logged in
            return;
        }

        // Multipart forms (needed because this form also carries a file) keep their text
        // fields in req.form, not in req.get_param_value like a plain POST form does.
        string phone = cleanField(req.form.get_field("phone"));
        string address = cleanField(req.form.get_field("address"));
        string idType = cleanField(req.form.get_field("idtype"));
        if (idType.empty()) idType = "Barangay ID";  // the form's own default option

        if (phone.empty() || address.empty()) {
            user.phone = phone;      // show back whatever they already typed
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
        const size_t MAX_SIZE = 5 * 1024 * 1024;  // 5 MB, matches the Figma
        if (photo.content.size() > MAX_SIZE) {
            user.phone = phone;
            user.address = address;
            showVerifyPage(res, user, "That photo is too large. Please use one under 5 MB.");
            return;
        }

        // Pick the file extension from the content type, not from the uploaded file name
        // (the file name comes from the user's computer and shouldn't be trusted).
        string extension = (photo.content_type == "image/png") ? ".png" : ".jpg";

        filesystem::create_directories("../data/ids");  // does nothing if it already exists
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

        saveVerificationRequest(login, phone, address, idType, "pending");
        res.set_redirect("/dashboard");
    });

    // FILE A COMPLAINT: only residents whose ID was approved (verified) can file.
    // The page has no ".html" in its URL for the same reason /verify doesn't (README, section 7).
    server.Get("/file-complaint", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getSessionToken(req));
        User user;
        if (login.empty() || !findUser(login, user)) {
            res.set_redirect("/signin.html");
            return;
        }
        if (user.verification != "verified") {
            res.set_redirect("/verify");  // must get the ID approved first
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

        // The form has a file input, so its text fields come from req.form (README, bug #5).
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

        // The photo is optional. Same checks as the ID photo upload.
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
        res.set_redirect("/dashboard#recent-complaints");
    });

    // ID PHOTO: shows the logged-in resident's own uploaded ID photo (never anyone else's).
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

    // ===================== ADMIN (staff-only area) =====================

    // Someone typing "/admin/" with a trailing slash should land in the same place
    // as "/admin" (no slash), not see a 404. Send them to the real route.
    server.Get("/admin/", [](const httplib::Request& req, httplib::Response& res) {
        res.set_redirect("/admin");
    });

    // "/admin" is no longer its own sign-in page — there's one shared sign-in
    // form now (signin.html / POST /signin, above). This route only decides
    // where to send a visitor: a signed-in staff member goes straight to the
    // dashboard; anyone else (signed out, or signed in only as a resident) is
    // sent to the shared sign-in page. This is also what stops a signed-out
    // visitor, or a resident, from reaching anything admin-only by guessing
    // the URL: there is no path through this check without a valid, active
    // row in data/staff.txt.
    server.Get("/admin", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (!login.empty() && findStaff(login, staff) && staff.status == "active") {
            res.set_redirect("/admin/dashboard");
        } else {
            res.set_redirect("/signin.html");
        }
    });

    // ADMIN DASHBOARD.
    server.Get("/admin/dashboard", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");  // not signed in as staff
            return;
        }

        // The only genuinely live numbers on this dashboard right now: real counts
        // read straight from data/users.txt. Complaint counts are still sample data,
        // since complaints aren't saved to a file yet (see the HTML comment there).
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

        string page = readFile("../frontend/staff/dashboard_admin.html");
        page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
        page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
        page = replaceAll(page, "{{todayDate}}", todayFormatted());
        page = replaceAll(page, "{{pendingCount}}", to_string(counts.pending));
        page = replaceAll(page, "{{pendingPlural}}", counts.pending == 1 ? "" : "s");
        page = replaceAll(page, "{{verificationNotice}}", notice);
        page = replaceAll(page, "{{residentsNavBadge}}", navBadge);
        res.set_content(page, "text/html");
    });

    // COMPLAINTS LIST (sample data for now — see the comment in complaints_admin.html).
    server.Get("/admin/complaints", [](const httplib::Request& req, httplib::Response& res) {
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

        string page = readFile("../frontend/staff/complaints_admin.html");
        page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
        page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
        page = replaceAll(page, "{{residentsNavBadge}}", navBadge);
        res.set_content(page, "text/html");
    });

    // COMPLAINT DETAILS page (sample data for now — see sample_complaints.cpp).
    // "([A-Za-z0-9\-]+)" matches one path segment like "AGAP-0005" (no slash in
    // it), so this never matches the PDF route below, which has an extra "/" in it.
    server.Get(R"(/admin/complaints/([A-Za-z0-9\-]+))", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }

        string id = req.matches[1];
        SampleComplaint complaint;
        if (!findSampleComplaint(id, complaint)) {
            res.status = 404;
            res.set_content("<h1>Complaint not found</h1><p><a href=\"/admin/complaints\">Back to complaints</a></p>", "text/html");
            return;
        }

        VerificationCounts counts = countResidentsByVerification();
        string navBadge = (counts.pending > 0)
            ? "<span class=\"nav-count\">" + to_string(counts.pending) + "</span>"
            : "";

        string page = readFile("../frontend/staff/complaint_details_admin.html");
        page = replaceAll(page, "{{staffName}}", escapeHtml(staff.fullName));
        page = replaceAll(page, "{{staffInitials}}", escapeHtml(initialsOf(staff.fullName)));
        page = replaceAll(page, "{{residentsNavBadge}}", navBadge);
        page = replaceAll(page, "{{id}}", escapeHtml(complaint.id));
        page = replaceAll(page, "{{title}}", escapeHtml(complaint.title));
        page = replaceAll(page, "{{category}}", escapeHtml(complaint.category));
        page = replaceAll(page, "{{status}}", escapeHtml(complaint.status));
        page = replaceAll(page, "{{statusBadgeClass}}", statusBadgeClassFor(complaint.status));
        page = replaceAll(page, "{{filedDate}}", escapeHtml(complaint.filedDate));
        page = replaceAll(page, "{{residentName}}", escapeHtml(complaint.residentName));
        page = replaceAll(page, "{{residentEmail}}", escapeHtml(complaint.residentEmail));
        page = replaceAll(page, "{{residentPhone}}", escapeHtml(complaint.residentPhone));
        page = replaceAll(page, "{{residentAddress}}", escapeHtml(complaint.residentAddress));
        page = replaceAll(page, "{{location}}", escapeHtml(complaint.location));
        page = replaceAll(page, "{{description}}", escapeHtml(complaint.description));
        res.set_content(page, "text/html");
    });

    // COMPLAINT PDF REPORT: builds the PDF on the spot and sends it straight back.
    // "inline" lets the browser preview it in a tab; the browser's own save
    // button still downloads it, so one link covers both "view" and "download".
    server.Get(R"(/admin/complaints/([A-Za-z0-9\-]+)/report\.pdf)", [](const httplib::Request& req, httplib::Response& res) {
        string login = loginFromSession(getAdminSessionToken(req));
        Staff staff;
        if (login.empty() || !findStaff(login, staff) || staff.status != "active") {
            res.set_redirect("/signin.html");
            return;
        }

        string id = req.matches[1];
        SampleComplaint complaint;
        if (!findSampleComplaint(id, complaint)) {
            res.status = 404;
            res.set_content("Complaint not found.", "text/plain");
            return;
        }

        ComplaintReportData reportData;
        reportData.id = complaint.id;
        reportData.title = complaint.title;
        reportData.category = complaint.category;
        reportData.status = complaint.status;
        reportData.filedDate = complaint.filedDate;
        reportData.residentName = complaint.residentName;
        reportData.residentEmail = complaint.residentEmail;
        reportData.residentPhone = complaint.residentPhone;
        reportData.residentAddress = complaint.residentAddress;
        reportData.location = complaint.location;
        reportData.description = complaint.description;
        reportData.photoPath = "../frontend/assets/sample_complaint_photo.jpg";

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

    // RESIDENTS LIST — real data, read straight from data/users.txt. Accounts
    // that never submitted anything ("unverified") aren't shown here, since
    // there's nothing for staff to review yet, same as the Figma.
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

    // RESIDENT REVIEW page — one resident's full verification request.
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

        // Shows the real photo file's type and size if it exists, so staff can
        // see what the resident actually uploaded (matches the Figma's
        // "JPG, 2.1 MB" line), instead of a guess.
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

    // APPROVE a resident's verification request.
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

    // REJECT a resident's verification request. A reason is required —
    // without one, this sends staff back to the review page with an error
    // instead of silently rejecting with no explanation for the resident.
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

    // RESIDENT ID PHOTO (admin view) — unlike the resident-facing /id-photo
    // route, this one is keyed by a login in the URL rather than the
    // visitor's own session, because staff need to view ANY resident's
    // photo. It's still staff-only: the login check above guards this whole
    // section, so a signed-out visitor or a resident can't reach it.
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

    // ANNOUNCEMENTS — staff can view and post; residents see the newest 4 on
    // their dashboard (see the /dashboard route above).
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

    // POST a new announcement.
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
        a.id = to_string(time(nullptr));  // seconds since 1970 — unique enough for this project
        a.title = title;
        a.category = category;
        a.message = message;
        a.postedDate = todayFormatted();
        addAnnouncement(a);
        res.set_redirect("/admin/announcements");
    });

    // DELETE an announcement.
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

    // STAFF SIGN OUT.
    server.Get("/admin/signout", [](const httplib::Request& req, httplib::Response& res) {
        deleteSession(getAdminSessionToken(req));
        res.set_header("Set-Cookie", "admin_session=; Path=/; Max-Age=0");
        res.set_redirect("/admin");
    });

    // SIGN OUT: forget the session and clear the cookie.
    server.Get("/signout", [](const httplib::Request& req, httplib::Response& res) {
        deleteSession(getSessionToken(req));
        res.set_header("Set-Cookie", "session=; Path=/; Max-Age=0");
        res.set_redirect("/");
    });

    int port = 8080;
    const char* portValue = getenv("PORT");
    if (portValue != nullptr) {
        port = stoi(portValue);
    }

    cout << "AGAP server is running on port " << port << endl;
    server.listen("0.0.0.0", port);
}