const SHEET_NAME = "Sheet1";
const SPREADSHEET_ID = "PASTE_SPREADSHEET_ID_HERE";

function doPost(e) {
  try {
    const spreadsheet = getSpreadsheet_();
    let sheet = spreadsheet.getSheetByName(SHEET_NAME);
    if (!sheet) {
      sheet = spreadsheet.insertSheet(SHEET_NAME);
    }

    if (sheet.getLastRow() === 0) {
      sheet.appendRow(["Timestamp", "Event", "UID", "Note", "Device time (ms)"]);
    }

    const payload = JSON.parse(e && e.postData && e.postData.contents ? e.postData.contents : "{}");
    sheet.appendRow([
      new Date(),
      String(payload.event || ""),
      String(payload.uid || ""),
      String(payload.note || ""),
      String(payload.time || "")
    ]);

    return jsonResponse_({ result: "success" });
  } catch (error) {
    return jsonResponse_({ result: "error", error: String(error) });
  }
}

function doGet() {
  try {
    const spreadsheet = getSpreadsheet_();
    return jsonResponse_({ result: "success", spreadsheet: spreadsheet.getName() });
  } catch (error) {
    return jsonResponse_({ result: "error", error: String(error) });
  }
}

function getSpreadsheet_() {
  const activeSpreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  if (activeSpreadsheet) {
    return activeSpreadsheet;
  }

  if (!SPREADSHEET_ID || SPREADSHEET_ID === "PASTE_SPREADSHEET_ID_HERE") {
    throw new Error("Set SPREADSHEET_ID to the ID from the Google Sheets URL.");
  }

  return SpreadsheetApp.openById(SPREADSHEET_ID);
}

function jsonResponse_(value) {
  return ContentService
    .createTextOutput(JSON.stringify(value))
    .setMimeType(ContentService.MimeType.JSON);
}
