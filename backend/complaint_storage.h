#ifndef COMPLAINT_STORAGE_H
#define COMPLAINT_STORAGE_H

#include <string>
#include <vector>
using namespace std;

// One complaint a resident filed. Each field is one column in data/complaints.txt.
struct Complaint {
    string id;           // e.g. "AGAP-0001"
    string login;        // the resident who filed it (their email/login)
    string category;     // Garbage, Noise, Road and lighting, Flooding or Others
    string location;
    string description;
    string status;       // Pending, In progress, Resolved or Rejected
    string filedDate;    // e.g. "Oct 3, 2026"
    string photoFile;    // file name inside data/complaint_photos/, or empty for no photo
};

// Makes the next free complaint ID, like "AGAP-0008".
string nextComplaintId();

// Adds a new complaint at the end of data/complaints.txt. Returns false if it can't be written.
bool addComplaint(const Complaint& complaint);

// Every complaint filed by one resident, newest first. Used on the resident dashboard.
vector<Complaint> listComplaintsFor(const string& login);

// Every complaint from every resident, newest first. Used by the admin pages.
vector<Complaint> listAllComplaints();

// Looks for one complaint by ID. Returns true and fills "found" if it exists.
bool findComplaint(const string& id, Complaint& found);

// Changes one complaint's status (Pending, In progress, Resolved or Rejected).
// Rewrites the whole file, since a text file can't edit one line in place.
bool updateComplaintStatus(const string& id, const string& newStatus);

#endif
