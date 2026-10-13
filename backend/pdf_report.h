#ifndef PDF_REPORT_H
#define PDF_REPORT_H

#include <string>
using namespace std;

struct ComplaintReportData {
    string id, title, category, status, filedDate;
    string residentName, residentEmail, residentPhone, residentAddress;
    string location, description;
    string photoPath;
};

bool generateComplaintPdf(const string& outPath, const ComplaintReportData& data);

#endif