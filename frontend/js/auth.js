const toggle = document.querySelector(".show-password");

if (toggle) {
  toggle.addEventListener("change", function () {
    document.querySelectorAll(".password-field").forEach(function (field) {
      field.type = toggle.checked ? "text" : "password";
    });
  });
}
