// Shows the chosen file's name on the upload box instead of the default "Tap to upload" text.
// This is only a visual convenience, so it's one small script rather than part of the C++ backend.
const fileInput = document.getElementById("idphoto");
const uploadText = document.getElementById("uploadText");

fileInput.addEventListener("change", function () {
  if (fileInput.files.length > 0) {
    uploadText.textContent = "Selected: " + fileInput.files[0].name;
  } else {
    uploadText.textContent = "Tap to upload a clear photo of the front of the ID";
  }
});