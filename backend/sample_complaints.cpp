    #include "sample_complaints.h"
    #include <vector>
    using namespace std;

    static const vector<SampleComplaint> SAMPLE_COMPLAINTS = {
        { "AGAP-0007", "Stray dogs near the chapel", "Others", "Pending", "Oct 3, 2026",
        "Pedro Santos", "pedro.santos@email.com", "0917 222 1001", "Purok 4, Brgy. Sample",
        "Near the chapel, Purok 4, Brgy. Sample",
        "Several stray dogs have been gathering near the chapel and barking at night." },

        { "AGAP-0006", "Pothole on Rizal Street", "Road and lighting", "Pending", "Oct 3, 2026",
        "Ana Reyes", "ana.reyes@email.com", "0917 222 1002", "Purok 2, Brgy. Sample",
        "Rizal Street, Purok 2, Brgy. Sample",
        "There is a large pothole on Rizal Street that is dangerous for motorcycles at night." },

        { "AGAP-0005", "Garbage not collected for a week", "Garbage", "Pending", "Oct 3, 2026",
        "Juan Dela Cruz", "juan.delacruz@email.com", "0917 123 4567", "Purok 3, Brgy. Sample",
        "Purok 3, Brgy. Sample (home address)",
        "Garbage has not been collected for a week." },

        { "AGAP-0004", "Loud karaoke at night", "Noise", "Pending", "Oct 3, 2026",
        "Juan Dela Cruz", "juan.delacruz@email.com", "0917 123 4567", "Purok 3, Brgy. Sample",
        "Purok 5, Brgy. Sample",
        "Karaoke is very loud every night after 10 PM." },

        { "AGAP-0003", "Uncollected garbage on Purok 3", "Garbage", "In progress", "Oct 2, 2026",
        "Juan Dela Cruz", "juan.delacruz@email.com", "0917 123 4567", "Purok 3, Brgy. Sample",
        "Purok 3, Brgy. Sample",
        "Garbage has not been collected for a week." },

        { "AGAP-0002", "Broken streetlight near the school", "Road and lighting", "In progress", "Sep 30, 2026",
        "Juan Dela Cruz", "juan.delacruz@email.com", "0917 123 4567", "Purok 3, Brgy. Sample",
        "Near Sample Elementary School",
        "The streetlight near the school has been broken for several days." },

        { "AGAP-0001", "Flooding on Mabini Street", "Flooding", "Resolved", "Sep 27, 2026",
        "Juan Dela Cruz", "juan.delacruz@email.com", "0917 123 4567", "Purok 3, Brgy. Sample",
        "Mabini Street, Brgy. Sample",
        "Flooding occurs on Mabini Street whenever it rains heavily." },
    };

    bool findSampleComplaint(const string& id, SampleComplaint& found) {
        for (const SampleComplaint& c : SAMPLE_COMPLAINTS) {
            if (c.id == id) { found = c; return true; }
        }
        return false;
    }