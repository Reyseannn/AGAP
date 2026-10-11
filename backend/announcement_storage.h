#ifndef ANNOUNCEMENT_STORAGE_H
#define ANNOUNCEMENT_STORAGE_H

#include <string>
#include <vector>
using namespace std;

// One announcement. Each field is one column in data/announcements.txt.
struct Announcement {
    string id;          // unique number (as text) — used to delete it later
    string title;
    string category;    // e.g. "General", "Health", "Event"
    string message;
    string postedDate;  // already formatted, e.g. "Oct 3, 2026"
};

// Returns every announcement, NEWEST FIRST.
vector<Announcement> listAnnouncements();

// Adds a new announcement at the end of the file. Returns false if it can't be written.
bool addAnnouncement(const Announcement& announcement);

// Removes the announcement with this id. Returns false if the file can't be written.
bool deleteAnnouncement(const string& id);

#endif
