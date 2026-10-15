// The look of the current layer (opacity, blend, and for shape layers the edge, outline and drop shadow) and
// the group page (opacity, blend, visibility, ungroup, merge, delete).
#pragma once

void layer_look_sheet_open_group(int groupId); // opens the group page of that group
void layer_look_sheet_layer(int sheetLeft); // the current layer
void layer_look_sheet_group(int sheetLeft);
