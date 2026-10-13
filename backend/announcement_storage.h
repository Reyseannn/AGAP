#ifndef ANNOUNCEMENT_STORAGE_H
#define ANNOUNCEMENT_STORAGE_H

#include <string>
#include <vector>
using namespace std;

struct Announcement {
    string id;
    string title;
    string category;
    string message;
    string postedDate;
};

vector<Announcement> listAnnouncements();

bool addAnnouncement(const Announcement& announcement);

bool deleteAnnouncement(const string& id);

#endif
