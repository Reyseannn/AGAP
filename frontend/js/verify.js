const fileInput = document.getElementById("idphoto");
const uploadText = document.getElementById("uploadText");

fileInput.addEventListener("change", function () {
  if (fileInput.files.length > 0) {
    uploadText.textContent = "Selected: " + fileInput.files[0].name;
  } else {
    uploadText.textContent = "Tap to upload a clear photo of the front of the ID";
  }
});