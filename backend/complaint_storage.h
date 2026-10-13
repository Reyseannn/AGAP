#ifndef COMPLAINT_STORAGE_H
#define COMPLAINT_STORAGE_H

#include <string>
#include <vector>
using namespace std;

struct Complaint {
    string id;
    string login;
    string category;
    string location;
    string description;
    string status;
    string filedDate;
    string photoFile;
};

string nextComplaintId();

bool addComplaint(const Complaint& complaint);

vector<Complaint> listComplaintsFor(const string& login);

vector<Complaint> listAllComplaints();

bool findComplaint(const string& id, Complaint& found);

bool updateComplaintStatus(const string& id, const string& newStatus);

#endif
