// "Show password" checkbox: switches every password box in the form between hidden and visible.
const toggle = document.querySelector(".show-password");

if (toggle) {
  toggle.addEventListener("change", function () {
    document.querySelectorAll(".password-field").forEach(function (field) {
      field.type = toggle.checked ? "text" : "password";
    });
  });
}
