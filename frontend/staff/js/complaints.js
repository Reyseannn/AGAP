// Filters the complaints table by status tab, category dropdown, and search text.
// This all happens in the browser because the table is still sample data (see the
// HTML comment above the table). Once complaints are saved on the server, this same
// job should move to C++: the filters would become URL query parameters like
// /admin/complaints?status=pending&category=Garbage, and the server would only send
// the matching rows, instead of hiding rows that were already sent to the browser.

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

searchBox.addEventListener("input", applyFilters);
categoryFilter.addEventListener("change", applyFilters);