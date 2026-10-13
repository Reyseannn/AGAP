# AGAP — Handoff README

This file is for picking up this project in a **new chat**, with no memory of the
conversation that built it. Read this whole file before changing anything —
it tells you what's built, how to run it, and what's still missing.

---

## 1. What AGAP is

AGAP (Community Complaint System) is a school project: a barangay (local
community) complaint-reporting website.

- **Residents** sign up, verify their identity with an ID photo, and (eventually)
  file and track complaints about local problems (garbage, flooding, road damage, etc).
- **Staff/admin** sign in to a separate admin area to review complaints, verify
  resident IDs, post announcements, and see reports.

**Tech stack, by requirement of the project owner — do not change these:**
- Frontend: plain HTML + CSS. JavaScript only where something genuinely can't
  be done without it (a few small scripts, see section 4).
- Backend: C++ (compiled with g++, `-std=c++17`), using the
  [cpp-httplib](https://github.com/yhirose/cpp-httplib) single-header library
  for the web server and [PicoSHA2](https://github.com/okdshin/PicoSHA2) for
  password hashing.
- Storage: **plain tab-separated `.txt` files**, not a database. **No SQLite —
  the project owner explicitly does not want it.**
- Style: `using namespace std;` is used throughout (the owner's explicit
  preference), code is kept simple and heavily commented, no frameworks.

The person running this project is a **beginner programmer** and needs to be
able to explain and defend every part of this code. Keep explanations simple,
avoid advanced C++ patterns, and don't add complexity without being asked.

---

## 2. Project structure

```
AGAP/
├── README.md                          ← this file
├── server.bat                         Windows: stops old server, compiles, runs (see section 3)
├── .gitignore                         keeps agap.exe and data/ (resident data) out of git
├── backend/
│   ├── main.cpp                       All routes. The biggest file — see section 5.
│   ├── storage.h / storage.cpp        Resident accounts (data/users.txt)
│   ├── staff_storage.h / .cpp         Staff/admin accounts (data/staff.txt)
│   ├── auth.h / auth.cpp              Password hashing + login sessions (shared by
│   │                                   residents AND staff, different cookie names)
│   ├── complaint_storage.h / .cpp     Complaints (data/complaints.txt) — filing, listing,
│   │                                   status changes
│   ├── announcement_storage.h / .cpp  Announcements (data/announcements.txt)
│   ├── file_util.h / .cpp             Shared by all storage files: appends a line safely
│   │                                   (creates data/ if missing, fixes a missing final newline)
│   │                                   and cleans Windows "\r" / Notepad BOM from lines read
│   ├── pdf_report.h / .cpp            Hand-written PDF generator (no library)
│   ├── httplib.h                      third-party, single header — https://github.com/yhirose/cpp-httplib
│   └── picosha2.h                     third-party, single header — https://github.com/okdshin/PicoSHA2
├── data/                              Created at runtime, NOT in git. Starts empty:
│   ├── users.txt, staff.txt, complaints.txt, announcements.txt
│   ├── ids/                           resident ID photos
│   ├── complaint_photos/              photos attached to complaints
│   └── reports/                       generated PDF reports
└── frontend/
    ├── index.html                     Public landing page
    ├── signin.html                    SHARED sign-in form — residents AND staff
    ├── signup.html                    Resident sign-up (no public staff sign-up, on purpose)
    ├── dashboard.html                 Resident dashboard (4 states, see section 6)
    ├── verify.html                    Resident ID verification form
    ├── filecomplaint.html             File-a-complaint form (verified residents only)
    ├── resident_list.html             One template used by /my-complaints and /announcements
    ├── css/style.css                  Shared stylesheet for the whole site
    ├── js/auth.js                     Show/hide password checkbox
    ├── js/verify.js                   Shows the chosen file's name on the upload box
    └── staff/                         Admin area. Public URLs are /admin/..., but the files
        │                               live in frontend/staff/ — see section 7 for why.
        ├── dashboard_admin.html
        ├── complaints_admin.html
        ├── complaint_details_admin.html   details + status form + photo + PDF link
        ├── residents_admin.html
        ├── resident_review_admin.html     Approve/Reject (reason required to reject)
        ├── announcements_admin.html
        ├── coming_soon_admin.html         placeholder for Reports and Settings
        ├── css/admin.css                  built ON TOP OF ../css/style.css
        ├── js/complaints.js               client-side tabs/search/filter for the table
        └── js/residents.js                client-side status-tab filter
```

**`backend/httplib.h` and `backend/picosha2.h` must be real, raw files.** If compiling fails
with `httplib.h: No such file or directory`, they're missing. If it fails with `string does
not name a type` or `picosha2 has not been declared`, they're corrupted — usually from saving
a GitHub *webpage* instead of the *raw* file. Re-download them.

---

## 3. How to build, run, and test

**Easiest (Windows):** double-click `server.bat` in the project folder. It stops any old
copy of the server, compiles, and runs.

**Compile by hand** (run from inside the `backend` folder — this matters, see section 9):

```
g++ main.cpp storage.cpp auth.cpp staff_storage.cpp pdf_report.cpp announcement_storage.cpp complaint_storage.cpp file_util.cpp -o agap.exe -std=c++17 -lws2_32
```

(On Linux/macOS, drop `-lws2_32`, add `-pthread`, and name the output `agap`.)

**Run** (also from inside `backend`):

```
agap.exe
```

The server creates `data/` (and `ids/`, `complaint_photos/`, `reports/` inside it) by itself,
and prints `Data folder: <full path>` when it starts — that is where residents are saved.
`data/` is not in git, so a fresh copy of the project starts without it. If the server is
started from the wrong folder, it finds the `backend` folder on its own.

The very first time it runs, if `data/staff.txt` doesn't exist yet, the server creates one
starter admin account and prints the password **to the console only**:

```
Email:    admin@barangay.gov
Password: admin123
```

Change this once a real staff-management UI exists (Settings, not yet built — see section 10).

**Open the site:** `http://localhost:8080`
**Admin area:** `http://localhost:8080/admin` (redirects to the shared sign-in
page if you're not signed in as staff)

**Basic manual test checklist:**
1. Sign up a resident on the public site → lands on the dashboard, "new account" state.
2. Submit the Verify form with phone, address, and a real JPG/PNG → status becomes "pending".
3. Sign in at `/signin.html` with the staff admin account → lands on `/admin/dashboard`.
   Open Residents, review the resident, and Approve → their dashboard becomes "verified".
4. As the resident, click "File a complaint", fill in the form (photo optional) → it appears
   under "My recent complaints" on the dashboard and at `/my-complaints`.
5. As staff, open Complaints → the complaint is listed. View it, change its status, and open
   "View / Download PDF report".
6. Post an announcement under Announcements → it shows on the resident dashboard.
7. Sign out, visit `/admin` directly → should bounce to `/signin.html`.
8. As a verified resident, the three banners at the top of the dashboard (pending / rejected /
   "Account created") must be hidden; for an unverified, pending or rejected resident only the
   matching banner shows. Sign out and in again — the state stays the same (it is read from
   `data/users.txt` on every dashboard request).

---

## 4. Where JavaScript is used, and why (kept to a minimum on purpose)

- `frontend/js/auth.js` — toggles password fields between hidden/visible when
  "Show password" is checked. Can't be done in CSS/HTML alone.
- `frontend/js/verify.js` — shows the chosen file's name on the ID-upload box.
- `frontend/staff/js/complaints.js` — live search/filter/tabs on the admin Complaints
  table. The rows are real (built by the server); this script only shows/hides rows that
  were already sent, and opens the tab named in `?status=pending`.
- `frontend/staff/js/residents.js` — the status tabs on the admin Residents table.

Everything else — the FAQ accordion, all forms, all page navigation — is plain
HTML with no JavaScript.

---

## 5. Features built so far, step by step

### Resident side
1. Landing page (`index.html`) — matches the resident Figma.
2. Sign up / sign in — passwords are salted + hashed with PicoSHA2, never stored
   in plain text. Sessions are random tokens kept in memory (`auth.cpp`), tied
   to a cookie named `session`. **Sessions reset when the server restarts** —
   that's expected, not a bug.
3. Resident dashboard (`dashboard.html`) — ONE file with FOUR states (new /
   pending / rejected / verified) controlled by a `data-show="..."` attribute
   system — see section 8's bug writeup for why it's built this way.
4. ID verification (`verify.html`) — collects phone, address (both required),
   and an ID photo (JPG/PNG, max 5 MB). Saves the photo to `data/ids/<login>.jpg`
   (or `.png`), and updates the resident's status to `pending`. The form
   pre-fills the resident's existing phone/address so they don't retype them.
5. `/id-photo` route — serves back only the logged-in resident's own ID photo.
   No one can view another resident's photo by changing a URL parameter —
   there isn't one; it's derived entirely from the session.

### Admin side (all under `/admin`)
6. Admin sign-in — originally a separate form/route, **later merged into the
   shared resident sign-in form** (see section 6). The dedicated admin sign-in
   page (`signin_admin.html`) was deleted once this happened — if you see it
   referenced anywhere, that reference is stale and should be removed.
7. Admin dashboard — navy sidebar + yellow topbar layout matching the admin
   Figma. The "residents waiting for ID verification" notice and count are
   **real**, computed live from `data/users.txt`. The complaint summary cards,
   recent list and category bars are **real** too, computed from `data/complaints.txt`.
8. Complaints list — **real** (`data/complaints.txt`). Newest first, with working
   tabs/search/category filter, an Export button (CSV), and counts on every tab.
9. Complaint details — real data, the resident's photo, a status form (Pending / In progress /
   Resolved / Rejected), and a **real, working, hand-written PDF generator**
   (`pdf_report.cpp`) that embeds a JPEG photo directly into the PDF with no external library.
   (PNG photos show on the details page but are left out of the PDF.) The PDF generator was
   tested with an actual PDF parser (`pypdf`) and by rendering it to an image.
10. Resident verification review — **fully real, no sample data.** Staff see
    an actual list of residents (`/admin/residents`) filtered by real
    verification status, review their real phone/address/ID type/uploaded
    photo (`/admin/residents/<email>`), and Approve or Reject (rejecting
    requires a reason, enforced server-side). The resident's own dashboard
    then shows that exact rejection reason. This required adding two columns
    to `data/users.txt` (`rejectionReason`, `idType`) — old files are
    automatically padded/upgraded, nothing needs migrating by hand.
11. Announcements — also **fully real**. Staff post/delete announcements at
    `/admin/announcements` (`data/announcements.txt`), and the resident
    dashboard's "Announcements" panel — previously 4 hardcoded items — now
    shows the real newest 4. **Editing an existing announcement was not
    built** (only post + delete); the Figma shows an "Edit" link that has no
    working route behind it yet. Residents can read the full list at `/announcements`.
12. File a complaint (resident side) — `/file-complaint`. Only residents whose ID was approved
    can file; others are sent to `/verify`. Saves category, location, description and an
    optional JPG/PNG photo (max 5 MB, in `data/complaint_photos/`). Residents see their own
    complaints on the dashboard and at `/my-complaints`.
13. Reports and Settings — sidebar links go to a "Coming soon" page (not built, section 10).

### Login architecture (important — changed partway through)
10. Originally residents and staff had **two completely separate login
    systems** (different forms, different routes, different cookies). This was
    later merged into **one shared sign-in form** — see section 6 for exactly
    how this works now, and section 9 for why the change was made.

---

## 6. Current login & dashboard behavior (read this before touching auth code)

**There is now only ONE sign-in form**: `frontend/signin.html`, posting to
`POST /signin`. Both residents and staff use it. There is no separate admin
sign-in page anymore.

What `POST /signin` does, in order:
1. Looks up the email in `data/staff.txt` first (via `findStaff`).
   - If found: checks the password against the staff hash. If correct and the
     account's status is `active`, creates a session, sets a cookie named
     **`admin_session`**, and redirects to `/admin/dashboard`.
   - If the staff password is wrong, or the account is `inactive`, shows an
     error — it does **not** fall through to try the resident table.
2. If the email isn't in `data/staff.txt` at all, checks `data/users.txt` (the
   resident table) exactly as before — sets a cookie named **`session`**, redirects
   to `/dashboard`.
3. If neither matches, or any password is wrong: shows "Wrong email or
   password" — the **same message** in every failure case, so nobody can tell
   from the error whether an email exists, or whether it's a staff or resident
   account.

**Why staff accounts can't be self-granted:** `data/staff.txt` is only ever
written to by the one-time startup seeding in `staff_storage.cpp`
(`ensureDefaultAdmin()`). The public `/signup` route only ever writes to
`data/users.txt`. There is currently **no admin UI to create more staff
accounts** — that's part of the unbuilt Settings page (section 10). Until
then, more staff accounts would have to be added by hand-editing
`data/staff.txt` in the same tab-separated format `addStaff()` writes, or
by writing one, using `makeSalt()` / `hashPassword()` from `auth.cpp`.

**An earlier design considered identifying admins by email domain** (e.g.
anything ending in `admin.gov`). **This was deliberately rejected** — since
resident sign-up is public, anyone could grant themselves admin by just using
that email pattern. If asked to revisit this, don't build it as the security
check; the `data/staff.txt` table is the only thing that should decide
admin access.

**`GET /admin`** is now just a dispatcher, not a page: valid staff session →
`/admin/dashboard`; anyone else → `/signin.html`. Every admin-only route
(`/admin/dashboard`, `/admin/complaints`, complaint details, the PDF route)
checks for a valid, active staff session and redirects to `/signin.html` if
that check fails.

**Resident dashboard states** (`dashboard.html`, one file, four looks):
`unverified` (shown as `state-new` — the welcome screen) → `pending` → `verified`
or `rejected`. The `<body>` tag's class is filled in by the server
(`stateClassFor()` in `main.cpp`) from the resident's real saved status.

---

## 7. Bugs found and fixed (read this before re-introducing similar patterns)

These aren't hypothetical — each was caught by actually running the server
and testing, not just by compiling.

1. **`storage.h` missing `std::` / wrong file contents on the user's machine.**
   Cause: manual retyping drifted from the given file. Fix: always replace the
   whole file rather than hand-editing when debugging a "doesn't compile" report.

2. **`picosha2.h` / `httplib.h` not real files.** Cause: saving a GitHub
   *webpage* instead of the *raw file* link. Symptom: `'picosha2' has not been
   declared`, or `string does not name a type`. Fix: always download via the
   raw.githubusercontent.com link or "Download raw file" button.

3. **`/verify.html` route never ran.** Cause: `httplib`'s static file server
   checks for a matching file **before** any custom route, for every GET
   request. Since a real file `verify.html` existed on disk, it always won
   over the custom `Get("/verify.html", ...)` route, which silently never
   fired. **Fix pattern used everywhere since:** any page that needs
   server-side logic (filling in placeholders, checking login) is reached by a
   URL **with no file extension** (`/verify`, `/dashboard`, `/admin/dashboard`)
   so there's no file on disk with that exact name to collide with. Keep
   following this pattern for any new dynamic page.

4. **Same bug, but a whole folder.** `frontend/admin/` being a real directory
   made `GET /admin` permanently redirect (301) to `/admin/` before the custom
   route ever ran. Fix: the folder was renamed to `frontend/staff/` — the
   public URLs (`/admin`, `/admin/dashboard`, ...) are unrelated to this
   folder name, so this was a pure disk-layout fix. **If you ever add a new
   folder under `frontend/`, check it doesn't share a name with a route.**

5. **Multipart form fields read with the wrong function.** `req.get_param_value()`
   only works for a normal (urlencoded) POST body. The Verify form also
   uploads a file, so the browser sends it as `multipart/form-data`, and text
   fields in that format must be read with `req.form.get_field()` instead.
   Using the wrong one silently returned empty strings — phone/address looked
   like they were being ignored, with no error. Watch for this on any future
   form that mixes a file upload with text fields.

6. **CSS logic bug: elements tagged with multiple states could never show.**
   The original dashboard used classes like `class="v-verified v-pending v-rejected"`
   with a hiding rule that required **every** listed state to match
   simultaneously — which is impossible, since an account only has one status
   at a time. This silently broke the "Good day, ..." heading, the Edit link,
   and the verified badge in every state. **Fixed by switching to a
   `data-show="pending rejected verified"` attribute** matched with CSS
   `~=` (whitespace-token) selectors, which correctly means "show if the
   current state is ANY of these." This is the pattern now used throughout
   `dashboard.html` and should be reused for any future multi-state UI,
   not the old multi-class approach.

7. **Browser redirect-loop that looked like a server bug but wasn't.** After
   the folder rename (#4), a user's browser kept showing
   `ERR_TOO_MANY_REDIRECTS` on `/admin/`. The server was verified correct via
   direct `curl` tracing (every path resolves in ≤2 hops). The actual causes,
   found one at a time:
   - Chrome had cached the **old, now-fixed** 301 redirect from before the
     folder rename.
   - **Multiple copies of `agap.exe` were running at once** on Windows
     (`netstat -ano | findstr :8080` showed 3 listeners) — `Ctrl+C` in one
     terminal doesn't reliably kill the process on Windows, so repeated
     recompile/rerun cycles left old binaries running underneath. Requests
     were being served unpredictably by whichever old build happened to
     answer. Fixed by `taskkill /IM agap.exe /F` before restarting. **If a fix
     doesn't seem to take effect, check for stray processes before assuming
     the code is wrong.**

8. **Blank white screen instead of an error.** Cause: `agap.exe` started from
   the wrong working directory (not inside `backend/`), so every
   `readFile("../frontend/...")` call silently failed and returned an empty
   string, which was then sent to the browser as a valid but empty 200 page.
   **Fixed in `readFile()` itself** (in `main.cpp`): it now returns a visible
   HTML error page naming the exact missing path and explaining the folder
   issue, instead of returning `""`. This fix covers **every** page the server
   reads, since they all go through this one function — don't bypass it with
   a raw `ifstream` elsewhere, or this safety net won't apply there.

9. **Files named the same but with different capitals** (`Auth.js` vs `auth.js`, `Data/` vs
   `data/`). Windows doesn't care, but Linux and most hosts do, and git can get confused by a
   rename that only changes the case. Always use the exact names in section 2 (`js/auth.js`,
   `data/`) everywhere they're referenced.

---

## 8. Real vs. sample data — everything is real now

Complaint filing and storage are built (`complaint_storage.cpp`, `data/complaints.txt`), and
the admin Complaints pages, the admin dashboard cards/recent list/category bars, and the PDF
reports all read real complaints. `sample_complaints.*` and the placeholder photo were deleted.

**Everything is real:** resident accounts, verification and the review flow, staff accounts,
announcements, complaints (including status changes and photos), ID photo storage, and the
PDF generator.

If numbers look wrong on a fresh checkout, remember `data/` starts empty: a new install shows
zeros and "No complaints have been filed yet" until residents file some.

---

## 9. Environment notes (specific to how this has been tested)

- Developed/tested on **Windows with MinGW g++**, path containing non-ASCII
  characters and OneDrive sync — this has not caused problems so far, but be
  aware of it if file-path bugs come up.
- **The server must be started from inside the `backend/` folder.** Every file
  path in `main.cpp` is relative (`../frontend/...`, `../data/...`). `server.bat` in the project root does `cd backend`, stops old copies, compiles and runs
  in one step — prefer it so nobody has to `cd` manually.
- `%-d` (no-leading-zero day) in `strftime` is a GNU/BSD extension not
  guaranteed on MinGW — `todayFormatted()` in `main.cpp` deliberately avoids
  it, building the date from separate `strftime("%b", ...)` + `tm_mday` +
  `tm_year` pieces instead. Don't "simplify" this back to a single format string.
- All new C++ files follow the existing project convention: `using namespace std;`
  at the top, after `#include`s.

---

## 10. Known issues, unfinished work, and suggested next steps

**Not built yet:**
- Reports page (date/category filters, resolution stats, export). Sidebar link shows "Coming soon".
- Settings — staff account management (add/deactivate/reset password), teams & puroks,
  categories, barangay info — **and confirmation pop-ups** before destructive actions
  (reject, suspend, delete, deactivate). Reject and Delete announcements already work and are
  guarded server-side; what's missing is the confirmation *dialog* the Figma shows.
- Editing an existing announcement (only post + delete exist).
- Assigning complaints to staff/teams, and notes/comments on a complaint.
- Residents can't open a single complaint (only a list); no notification when status changes
  beyond what they see next time they open their dashboard (no email, by design).

**Smaller loose ends:**
- No password-reset flow for residents (removed early on, by the project owner's choice).
- No way to add staff accounts except by hand-editing `data/staff.txt` or waiting for Settings.
- The starter admin password (`admin123`) should be changed once there's a way to do it.
- Complaint IDs are `AGAP-` + (number of complaints + 1). Complaints are never deleted, so IDs
  stay unique; if deleting is ever added, switch to a counter.
- `data/users.txt` is 9 columns: `login, fullName, phone, address, salt, passwordHash,
  verification, rejectionReason, idType`. Every read auto-pads short old lines (`FIELD_COUNT`
  in `storage.cpp`). `data/complaints.txt` columns: `id, login, category, location,
  description, status, filedDate, photoFile`.
- Sessions live in memory — they reset when the server restarts.
- Hosting: this project previously had Vercel/Render/Docker deployment files; they were
  removed on purpose. It runs locally only (`localhost:8080`).
- Git: `backend/agap.exe` and `data/` are in `.gitignore`. Older commits still contain an
  early `Data/users.txt`.

**If you're picking this up fresh:** ask the project owner which step they want next —
likely Reports or Settings (with the confirmation dialogs as a quick first win).

---

## 11. Updating this README

Whenever this project changes in a new session, update the relevant section
above — especially sections 5 (features), 7 (bugs), 8 (real data),
and 10 (known issues) — so the next handoff stays accurate. Don't leave this
file describing features or fixes that no longer match the actual code.

---

## 12. Bug: residents could not be created or opened (admin worked) — fixed

Symptom: sign-up redirected to the dashboard, which immediately bounced back to the sign-in
page, and signing in with that account failed. Staff login still worked.

Causes found by reproducing it:
- `users.txt` saved by hand (e.g. in Notepad) with **no newline at the end**. The next sign-up
  was appended to the *end of the last line*, so the new account was glued onto an old one
  (the old row grew from 7 to 15 columns) and could never be found again.
- A **UTF-8 BOM** at the start of a data file made the first login in it never match.
- A **missing `data/` folder** (it is not in git) made every save fail with no message on the
  console — including creating the starter admin.
- Starting the server **outside `backend/`** pointed `../data` at the wrong place.

Fix: `file_util.cpp` (`appendLine`, `cleanLine`) is used by every storage file; `main()` creates
`data/` and finds the backend folder; failures print `ERROR: could not write to <full path>`.
**Never edit the data files by hand while the server is running, and keep the final newline.**

---

## 13. Bugs: wrong login cookie, banners always visible, admin sign-out page — fixed

- **Cookie mix-up.** `getCookie("session")` also matched inside `admin_session=...`, so a browser
  holding both cookies sent the staff token as the resident session. `getCookie()` now splits on
  `;` and matches the exact cookie name.
- **Verification banners never hid.** `.banner { display:flex }` came later in `style.css` than
  `[data-show] { display:none }` with the same specificity, so every banner showed for everyone.
  The rules are now `body [data-show]` (hidden) and `body.state-... [data-show~="..."]` (shown).
  The state class comes from the stored `verification` value via `stateClassFor()` in `main.cpp`
  (`unverified` → new, `pending`, `rejected`, `verified`; anything unknown falls back to new).
- **Admin sign-out.** `/admin/signout` clears the `admin_session` cookie, deletes the token on the
  server, and redirects to `/signin.html` (the shared sign-in page). Resident `/signout` still
  goes to `/`. After sign-out, `/admin/...` pages redirect to `/signin.html` and an old
  admin token is rejected.
- **Notes.** `backend/named_list_storage.h/.cpp` and `frontend/staff/settings_admin.html` are
  empty placeholders and are not part of the build. Code comments were removed from the source
  files; this README is where explanations live.

