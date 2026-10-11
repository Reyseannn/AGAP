#ifndef SAMPLE_COMPLAINTS_H
#define SAMPLE_COMPLAINTS_H

#include <string>
using namespace std;

// TEMPORARY sample data. Residents can't actually file a complaint yet (that
// button is still a "#" placeholder on their dashboard), so there is no real
// complaints.txt to read from. The admin Complaints pages use this fixed set
// instead, matching the Figma, until complaint storage is built the same way
// storage.cpp was built for residents. Delete this whole file at that point.
struct SampleComplaint {
    string id, title, category, status, filedDate;
    string residentName, residentEmail, residentPhone, residentAddress;
    string location, description;
};

// Looks up one sample complaint by ID (e.g. "AGAP-0005"). Returns false if
// there's no sample complaint with that ID.
bool findSampleComplaint(const string& id, SampleComplaint& found);

#endif