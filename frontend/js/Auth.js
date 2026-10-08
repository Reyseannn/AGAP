const form = document.querySelector(".auth-card");

if (form && window.location.hostname.endsWith(".vercel.app")) {
  const backendPath = new URL(form.action).pathname;
  form.action = backendPath === "/signup"
    ? "/api/auth/signup"
    : "/api/auth/signin";
}

const toggle = document.querySelector(".show-password");

if (toggle) {
  toggle.addEventListener("change", function () {
    document.querySelectorAll(".password-field").forEach(function (field) {
      field.type = toggle.checked ? "text" : "password";
    });
  });
}
