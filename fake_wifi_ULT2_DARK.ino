#include <WiFi.h>
#include <WebServer.h>
#include "SD_MMC.h"

const char* ssid = "ESP32-Test-Text";
const char* password = "12345678";

WebServer server(80);

String userText = "";
String sdStatus = "";
const int ledPin = 2;  // GPIO 2; built-in LED (active LOW)

// ------------------- Helpers -------------------

String urlDecode(String input) {
  input.replace("+", " ");
  int index = 0;
  while ((index = input.indexOf("%", index)) != -1) {
    if (index + 2 < input.length()) {
      String hex = input.substring(index + 1, index + 3);
      char c = (char) strtol(hex.c_str(), nullptr, 16);
      input.replace("%" + hex, String(c));
    }
    index++;
  }
  return input;
}

String sanitizeFilename(String fname) {
  fname = urlDecode(fname);
  if (!fname.startsWith("/")) fname = "/" + fname;
  return fname;
}

bool isImage(String fname) {
  fname.toLowerCase();
  return fname.endsWith(".jpg") || fname.endsWith(".jpeg") || fname.endsWith(".png");
}

// ------------------- File Listing -------------------

String listFilesHTML() {
  String html = "<h2>Files on SD Card</h2><ul>";
  File root = SD_MMC.open("/");
  if (!root) return "<p>Failed to open root directory</p>";

  File file = root.openNextFile();
  while (file) {
    String fname = String(file.name());
    html += "<li>";

    if (isImage(fname)) {
      html += "<img src='/image?file=" + fname + "' width='100' style='vertical-align:middle;margin-right:10px'>";
    }

    html += fname + " (" + String(file.size()) + " bytes) ";
    html += "<a href='/open?file=" + fname + "'>Open</a> ";
    html += "<a href='/download?file=" + fname + "'>Download</a> ";
    html += "<a href='/delete?file=" + fname + "' onclick='return confirm(\"Delete " + fname + "?\");'>Delete</a> ";
    html += "<a href='/rename?file=" + fname + "'>Rename</a>";
    html += "</li>";

    file = root.openNextFile();
  }
  html += "</ul>";
  return html;
}

// ------------------- Handlers -------------------

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>ESP32 SD Manager</title>";

  // DARK MODE CSS
  html += "<style>";
  html += "body { background-color: #121212; color: #e0e0e0; font-family: Arial, sans-serif; }";
  html += "h1, h2 { color: #ffffff; }";
  html += "a { color: #80cbc4; text-decoration: none; }";
  html += "a:hover { text-decoration: underline; }";
  html += "textarea { background-color: #1e1e1e; color: #e0e0e0; border: 1px solid #555; }";
  html += "input[type='submit'] { background-color: #333; color: #fff; border: none; padding: 5px 10px; cursor: pointer; }";
  html += "input[type='submit']:hover { background-color: #555; }";
  html += "ul { list-style-type: none; padding-left: 0; }";
  html += "li { margin-bottom: 10px; }";
  html += "pre { background-color: #1e1e1e; color: #e0e0e0; padding: 10px; border-radius: 5px; }";
  html += "form { margin-bottom: 20px; }";
  html += "img { border: 1px solid #444; margin-right: 10px; }";
  html += "</style>";

  html += "</head><body>";
  html += "<h1>ESP32 Text Editor & SD Manager</h1>";

  html += "<p><b>SD Card Status:</b> " + sdStatus + "</p>";
  html += listFilesHTML();

  // Text editor
  html += "<h2>Editor</h2>";
  html += "<form action='/submit' method='POST'>";
  html += "<textarea name='text' rows='10' cols='30'>" + userText + "</textarea><br><br>";
  html += "<input type='submit' value='Submit'>";
  html += "</form>";

  // Upload form
  html += "<h2>Upload File</h2>";
  html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
  html += "<input type='file' name='upload'><br><br>";
  html += "<input type='submit' value='Upload'>";
  html += "</form>";

  html += "<h2>Last Submitted Text:</h2>";
  html += "<pre>" + userText + "</pre>";

  html += "</body></html>";

  server.send(200, "text/html", html);

  // LED: ON if text exists, OFF if empty (trimmed)
  String trimmedText = userText;
  trimmedText.trim();
  digitalWrite(ledPin, trimmedText.length() > 0 ? LOW : HIGH);
}

void handleSubmit() {
  if (server.hasArg("text")) {
    userText = server.arg("text");

    // Save text to SD card
    File file = SD_MMC.open("/text.txt", FILE_WRITE);
    if (file) {
      file.println(userText);
      file.close();
    }

    // LED update
    userText.trim();  // modifies in place
    digitalWrite(ledPin, userText.length() > 0 ? LOW : HIGH);
  }

  server.sendHeader("Location", "/");
  server.send(303); // Redirect
}

void handleOpen() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String filename = sanitizeFilename(server.arg("file"));
  File file = SD_MMC.open(filename, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "File not found: " + filename);
    return;
  }

  if (filename.endsWith(".txt")) {
    String content = "<h2>Content of " + filename + "</h2><pre>";
    while (file.available()) content += (char)file.read();
    content += "</pre><a href='/'>Back</a>";
    file.close();
    server.send(200, "text/html", content);
  } else {
    file.close();
    server.sendHeader("Location", "/download?file=" + filename);
    server.send(303);
  }
}

void handleDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }
  String filename = sanitizeFilename(server.arg("file"));
  File file = SD_MMC.open(filename, FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "File not found: " + filename);
    return;
  }

  int lastSlash = filename.lastIndexOf('/');
  String shortName = filename.substring(lastSlash + 1);
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + shortName + "\"");
  server.streamFile(file, "application/octet-stream");
  file.close();
}

void handleDelete() {
  if (!server.hasArg("file")) return server.send(400, "text/plain", "Missing file parameter");
  String filename = sanitizeFilename(server.arg("file"));
  if (SD_MMC.exists(filename)) SD_MMC.remove(filename);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleRenameForm() {
  if (!server.hasArg("file")) return server.send(400, "text/plain", "Missing file parameter");
  String filename = sanitizeFilename(server.arg("file"));
  String html = "<h2>Rename File: " + filename + "</h2>";
  html += "<form action='/renameSubmit' method='POST'>";
  html += "<input type='hidden' name='oldname' value='" + filename + "'>";
  html += "New name: <input type='text' name='newname'><br><br>";
  html += "<input type='submit' value='Rename'></form>";
  html += "<a href='/'>Back</a>";
  server.send(200, "text/html", html);
}

void handleRenameSubmit() {
  if (!server.hasArg("oldname") || !server.hasArg("newname")) return server.send(400, "text/plain", "Missing parameters");
  String oldname = sanitizeFilename(server.arg("oldname"));
  String newname = sanitizeFilename(server.arg("newname"));
  if (SD_MMC.exists(newname)) return server.send(400, "text/plain", "New filename already exists");
  SD_MMC.rename(oldname, newname);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File uploadFile;

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    uploadFile = SD_MMC.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    server.sendHeader("Location", "/");
    server.send(303);
  }
}

void handleImage() {
  if (!server.hasArg("file")) return server.send(400, "text/plain", "Missing file parameter");
  String filename = sanitizeFilename(server.arg("file"));
  File file = SD_MMC.open(filename, FILE_READ);
  if (!file) return server.send(404, "text/plain", "File not found: " + filename);

  String contentType = "image/jpeg";
  if (filename.endsWith(".png")) contentType = "image/png";

  server.streamFile(file, contentType);
  file.close();
}

// ------------------- Setup -------------------

void setup() {
  Serial.begin(115200);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH); // LED OFF initially (active LOW)

  if (!SD_MMC.begin()) sdStatus = "SD Card FAILED to mount!";
  else sdStatus = "SD Card mounted successfully!";

  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/open", handleOpen);
  server.on("/download", handleDownload);
  server.on("/delete", handleDelete);
  server.on("/rename", handleRenameForm);
  server.on("/renameSubmit", HTTP_POST, handleRenameSubmit);
  server.on("/upload", HTTP_POST, [](){ server.send(200); }, handleUpload);
  server.on("/image", handleImage);

  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();
}