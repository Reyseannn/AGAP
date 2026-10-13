#include "pdf_report.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>
using namespace std;


static string pdfEscape(const string& text) {
    string out;
    for (char c : text) {
        if (c == '(' || c == ')' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

static vector<string> wrapText(const string& text, size_t maxChars) {
    vector<string> lines;
    string line, word;
    stringstream words(text);
    while (words >> word) {
        if (line.empty()) line = word;
        else if (line.length() + 1 + word.length() <= maxChars) line += " " + word;
        else { lines.push_back(line); line = word; }
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
    return lines;
}

static bool readJpegSize(const vector<unsigned char>& bytes, int& width, int& height) {
    if (bytes.size() < 4 || bytes[0] != 0xFF || bytes[1] != 0xD8) return false;

    size_t pos = 2;
    while (pos + 4 <= bytes.size()) {
        if (bytes[pos] != 0xFF) { pos++; continue; }
        unsigned char marker = bytes[pos + 1];

        if (marker >= 0xC0 && marker <= 0xC3) {
            if (pos + 9 > bytes.size()) return false;
            height = (bytes[pos + 5] << 8) | bytes[pos + 6];
            width  = (bytes[pos + 7] << 8) | bytes[pos + 8];
            return true;
        }
        if (marker == 0xD8 || marker == 0xD9) { pos += 2; continue; }
        if (pos + 4 > bytes.size()) return false;
        size_t segmentLength = (bytes[pos + 2] << 8) | bytes[pos + 3];
        pos += 2 + segmentLength;
    }
    return false;
}

bool generateComplaintPdf(const string& outPath, const ComplaintReportData& data) {
    vector<unsigned char> photoBytes;
    int photoWidth = 0, photoHeight = 0;
    bool hasPhoto = false;

    if (!data.photoPath.empty()) {
        ifstream photoFile(data.photoPath, ios::binary);
        if (photoFile) {
            photoBytes.assign(istreambuf_iterator<char>(photoFile), istreambuf_iterator<char>());
            hasPhoto = readJpegSize(photoBytes, photoWidth, photoHeight);
            if (!hasPhoto) photoBytes.clear();
        }
    }

    stringstream content;
    content << "BT /F1 16 Tf 50 740 Td (AGAP Complaint Report) Tj ET\n";

    int y = 712;
    auto writeLine = [&](const string& text, int size = 11) {
        content << "BT /F1 " << size << " Tf 50 " << y << " Td ("
                 << pdfEscape(text) << ") Tj ET\n";
        y -= (size >= 12 ? 20 : 16);
    };

    writeLine(data.id + "  |  " + data.title, 13);
    writeLine("Category: " + data.category + "     Status: " + data.status);
    writeLine("Filed: " + data.filedDate);
    y -= 6;
    writeLine("Resident", 12);
    writeLine("Name: " + data.residentName);
    writeLine("Email: " + data.residentEmail);
    writeLine("Mobile number: " + data.residentPhone);
    writeLine("Home address: " + data.residentAddress);
    y -= 6;
    writeLine("Location of the problem", 12);
    writeLine(data.location);
    y -= 6;
    writeLine("What happened", 12);
    for (const string& line : wrapText(data.description, 85)) writeLine(line);

    if (hasPhoto) {
        y -= 10;
        writeLine("Photo", 12);

        double maxW = 220.0, maxH = 165.0;
        double scale = min(maxW / photoWidth, maxH / photoHeight);
        double drawW = photoWidth * scale;
        double drawH = photoHeight * scale;
        double photoY = (y - 6) - drawH;

        content << "q " << drawW << " 0 0 " << drawH << " 50 " << photoY << " cm /Im1 Do Q\n";
    }

    string contentStream = content.str();

    vector<string> objects;

    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");

    string resources = "<< /Font << /F1 4 0 R >>";
    if (hasPhoto) resources += " /XObject << /Im1 6 0 R >>";
    resources += " >>";
    objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                       "/Resources " + resources + " /Contents 5 0 R >>");

    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

    objects.push_back("<< /Length " + to_string(contentStream.size()) + " >>\nstream\n"
                       + contentStream + "\nendstream");

    if (hasPhoto) {
        string imageDict = "<< /Type /XObject /Subtype /Image /Width " + to_string(photoWidth) +
                            " /Height " + to_string(photoHeight) +
                            " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode "
                            "/Length " + to_string(photoBytes.size()) + " >>";
        string imageObj = imageDict + "\nstream\n";
        imageObj.append(photoBytes.begin(), photoBytes.end());
        imageObj += "\nendstream";
        objects.push_back(imageObj);
    }

    ofstream out(outPath, ios::binary);
    if (!out) return false;

    string header = "%PDF-1.4\n";
    out << header;
    size_t offset = header.size();
    vector<size_t> offsets;

    for (size_t i = 0; i < objects.size(); i++) {
        offsets.push_back(offset);
        string objText = to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
        out << objText;
        offset += objText.size();
    }

    size_t xrefStart = offset;
    out << "xref\n0 " << (objects.size() + 1) << "\n";
    out << "0000000000 65535 f \n";
    char buf[32];
    for (size_t off : offsets) {
        snprintf(buf, sizeof(buf), "%010zu 00000 n \n", off);
        out << buf;
    }

    out << "trailer\n<< /Size " << (objects.size() + 1) << " /Root 1 0 R >>\n";
    out << "startxref\n" << xrefStart << "\n%%EOF";

    return true;
}