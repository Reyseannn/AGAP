
const rows = Array.from(document.querySelectorAll("#residentsBody tr"));
const tabs = document.querySelectorAll("#statusTabs .tab");
const resultCount = document.getElementById("resultCount");

tabs.forEach(function (tab) {
  tab.addEventListener("click", function () {
    tabs.forEach(function (t) { t.classList.remove("active"); });
    tab.classList.add("active");
    const status = tab.dataset.status;

    let visibleCount = 0;
    rows.forEach(function (row) {
      const visible = status === "all" || row.dataset.status === status;
      row.style.display = visible ? "" : "none";
      if (visible) visibleCount++;
    });

    resultCount.textContent = "Showing " + visibleCount + " of " + rows.length + " residents";
  });
});