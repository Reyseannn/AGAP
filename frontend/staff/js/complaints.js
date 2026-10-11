// Filters the complaints table by status tab, category dropdown, and search text.
// The rows are real (built by the server from data/complaints.txt). This script only
// shows/hides rows that were already sent, so the page filters instantly. A link such
// as /admin/complaints?status=pending opens with that tab already selected.

const rows = Array.from(document.querySelectorAll("#complaintsBody tr"));
const tabs = document.querySelectorAll("#statusTabs .tab");
const searchBox = document.getElementById("searchBox");
const categoryFilter = document.getElementById("categoryFilter");
const resultCount = document.getElementById("resultCount");

let activeStatus = "all";

function applyFilters() {
  const searchText = searchBox.value.trim().toLowerCase();
  const category = categoryFilter.value;
  let visibleCount = 0;

  rows.forEach(function (row) {
    const matchesStatus = activeStatus === "all" || row.dataset.status === activeStatus;
    const matchesCategory = category === "all" || row.dataset.category === category;
    const rowText = row.textContent.toLowerCase();
    const matchesSearch = searchText === "" || rowText.includes(searchText);

    const visible = matchesStatus && matchesCategory && matchesSearch;
    row.style.display = visible ? "" : "none";
    if (visible) visibleCount++;
  });

  resultCount.textContent = "Showing " + visibleCount + " of " + rows.length + " complaints";
}

tabs.forEach(function (tab) {
  tab.addEventListener("click", function () {
    tabs.forEach(function (t) { t.classList.remove("active"); });
    tab.classList.add("active");
    activeStatus = tab.dataset.status;
    applyFilters();
  });
});

// Open with the tab named in the URL (?status=pending), if there is one.
const wantedStatus = new URLSearchParams(window.location.search).get("status");
tabs.forEach(function (tab) {
  if (wantedStatus && tab.dataset.status === wantedStatus) {
    tabs.forEach(function (t) { t.classList.remove("active"); });
    tab.classList.add("active");
    activeStatus = wantedStatus;
  }
});
applyFilters();

searchBox.addEventListener("input", applyFilters);
categoryFilter.addEventListener("change", applyFilters);