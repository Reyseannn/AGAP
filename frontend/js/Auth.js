const toggle = document.querySelector(".show-password");
    toggle.addEventListener("change", function () {
    document.querySelectorAll(".password-field").forEach(function (box) {
        box.type = toggle.checked ? "text" : "password";
});
    });