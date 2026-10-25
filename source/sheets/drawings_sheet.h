// The Drawings page (file manager) and the Export page.
#pragma once

int drawings_sheet_export_format(void); // the format the Export page shows (EXPORT_*)

void drawings_sheet_open(void); // the sheet opens at once; saving and listing the drawings follow as a job
void drawings_sheet_files(int sheetLeft);
void drawings_sheet_export(int sheetLeft);
