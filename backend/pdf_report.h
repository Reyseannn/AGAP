#ifndef PDF_REPORT_H
#define PDF_REPORT_H

#include <string>
using namespace std;

// Everything needed to print one complaint as a one-page PDF report.
struct ComplaintReportData {
    string id, title, category, status, filedDate;
    string residentName, residentEmail, residentPhone, residentAddress;
    string location, description;
    string photoPath;  // path to a JPEG file to embed; leave empty for no photo
};

// Writes a one-page PDF report to outPath. Returns false if the file
// couldn't be created. No external PDF library is used — this builds the
// raw PDF bytes by hand, including embedding the JPEG photo directly
// (a JPEG's own compressed bytes can be dropped straight into a PDF image
// object; the PDF format has a filter, /DCTDecode, made exactly for this).
bool generateComplaintPdf(const string& outPath, const ComplaintReportData& data);

#endif