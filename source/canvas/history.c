#include "history.h"
#include "app_state.h"
#include "brush_engine.h"
#include "document.h"
#include "mascot.h"
#include "page_view.h"
#include "render.h"
#include "thumbnails.h"
#include "undo.h"

// Only the swapped tiles are redrawn, padded like the renderer pads the layer.
static void redraw_swapped_tiles(IRect rect, int layerIndex)
{
	g_unsaved = true;
	if (rect.x1 <= rect.x0 || layerIndex < 0 || layerIndex >= g_document.layerCount) {
		return;
	}
	rect = render_pad_on_page(rect, render_layer_padding(&g_document.layers[layerIndex].props));
	page_view_mark_dirty(rect);
	page_view_redraw_top_rect();
	thumbnails_update_rect(layerIndex, rect);
}

void history_undo(void)
{
	IRect swappedRect;
	int layerIndex = -1;
	if (!brush_stroke_active() && undo_apply_undo(&swappedRect, &layerIndex)) {
		redraw_swapped_tiles(swappedRect, layerIndex);
	} else {
		app_toast("Nothing to undo");
		mascot_event(MASCOT_REFUSED);
	}
}

void history_redo(void)
{
	IRect swappedRect;
	int layerIndex = -1;
	if (!brush_stroke_active() && undo_apply_redo(&swappedRect, &layerIndex)) {
		redraw_swapped_tiles(swappedRect, layerIndex);
	} else {
		app_toast("Nothing to redo");
		mascot_event(MASCOT_REFUSED);
	}
}
