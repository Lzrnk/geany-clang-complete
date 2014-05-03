/*
 * ui.cpp - a Geany plugin to provide code completion using clang
 *
 * Copyright (C) 2014 Noto, Yuta <nonotetau(at)gmail(dot)com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ui.hpp"

#include <string.h>
#include <gdk/gdkkeysyms.h>

#include "preferences.hpp"

namespace cc{

namespace detail {
	static void get_note_height(GtkWidget* widget, int* height) {
		GtkAllocation allocation;
		gtk_widget_get_allocation(widget, &allocation);
		//g_print("container %d %d %d %d", allocation.x, allocation.y, allocation.width, allocation.height);
		if( *height < allocation.height ) { *height = allocation.height; }
	}
}

void get_suggestion_window_coord(int* show_x, int* show_y) {
	GtkWidget* main_window = geany_data->main_widgets->window;
	GtkWidget* notebook_widget = geany_data->main_widgets->notebook;
	GtkAllocation rect_mainwindow;
	gtk_widget_get_allocation(main_window, &rect_mainwindow);
	//g_print("mainwin %d %d %d %d",
		//rect_mainwindow.x, rect_mainwindow.y, rect_mainwindow.width, rect_mainwindow.height);

	GtkAllocation rect_notebook;
	gtk_widget_get_allocation(notebook_widget, &rect_notebook);
	//g_print("notebook %d %d %d %d", rect_notebook.x, rect_notebook.y,
		//rect_notebook.width, rect_notebook.height);

	gint x_origin, y_origin;
	GdkWindow* gdk_window =  gtk_widget_get_window(GTK_WIDGET(main_window));
	gdk_window_get_origin(gdk_window, &x_origin, &y_origin);
	//g_print("origin %d %d", x_origin, y_origin);

	ScintillaObject* sci = document_get_current()->editor->sci;

	int margin_width = 0;
	for(int i=0; i<5; i++) {
		int pix = scintilla_send_message(sci, SCI_GETMARGINWIDTHN, i, 0);
		margin_width += pix;
	}
	//g_print("sci margin_width %d", margin_width);

	int x_scroll_offset = scintilla_send_message(sci, SCI_GETXOFFSET, 0, 0);
	//g_print("sci x_scroll_offset %d", x_scroll_offset);

	int screen_lines = scintilla_send_message(sci, SCI_LINESONSCREEN, 0, 0);
	//g_print("sci screen lines %d", screen_lines);

	int top_line = scintilla_send_message(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
	//g_print("sci top_line %d", top_line); // real line (including line wrapped)

	//int tab_border = gtk_notebook_get_tab_hborder( GTK_NOTEBOOK(geany_data->main_widgets->notebook) );
	//g_print("tab_border %d", tab_border);

	int note_height = 0;
	gtk_container_foreach( GTK_CONTAINER(notebook_widget),
		(GtkCallback)detail::get_note_height, &note_height);
	int note_tab_height = rect_notebook.height - note_height;

	// Scintilla says "Currently all lines are the same height."
	int text_height = scintilla_send_message(sci, SCI_TEXTHEIGHT, 0, 0);
	//g_print("sci text_height %d", text_height);

	// calc caret-height in a note
	int cur_line = sci_get_current_line(sci);
	int wrapped_line = 0;
	int invisible_line = 0;
	for(int i=0; i<cur_line; i++) {
		if( !sci_get_line_is_visible(sci, i) ) { invisible_line++; }
		else {
			int wrap_count = scintilla_send_message(sci, SCI_WRAPCOUNT, i, 0);
			wrapped_line += wrap_count - 1;
		}
	}
	//g_print("invis %d wrap %d", invisible_line, wrapped_line);
	cur_line += wrapped_line - invisible_line;
	int diff_line = cur_line - top_line;

	// calc caret-width in a note
	int pos = sci_get_current_position(sci);
	int line= sci_get_current_line(sci);
	int ls_pos = sci_get_position_from_line(sci, line);
	int text_width = 0;
	if( ls_pos < pos ) {
		int space_pix = scintilla_send_message(sci, SCI_TEXTWIDTH, STYLE_DEFAULT, (sptr_t)" ");
		int tab_pix = space_pix * sci_get_tab_width(sci);
		//g_print("space_pix %d tab_pix %d", space_pix, tab_pix);
		gchar* line_str = sci_get_contents_range(sci, ls_pos, pos);
		if( g_utf8_validate(line_str, -1, NULL) ) {
			gchar* letter = NULL;
			gchar* next = line_str;
			while(next[0]) {
				letter = next;
				next = g_utf8_next_char(next);
				if( letter[0] == '\t' ) {
					int mod = text_width % tab_pix;
					text_width += (mod == 0 ? tab_pix : tab_pix - mod);
				}
				else {
					int styleID = sci_get_style_at(sci,  ls_pos + (letter - line_str));
					gchar tmp = next[0]; //for making a letter from a string
					next[0] = '\0';
					int tw = scintilla_send_message(sci, SCI_TEXTWIDTH, styleID, (sptr_t)letter);
					next[0] = tmp; // restore from NULL
					text_width += tw;
					//g_print("letter width %d",tw);
				}
			}
		}
		//g_print("textwidth %d", text_width);
		g_free(line_str);
	}

	if( 0 <= diff_line && diff_line < screen_lines ) {
		int total_text_height = diff_line * text_height;

		*show_y = y_origin + rect_notebook.y + note_tab_height + total_text_height + text_height + 1;
		*show_x = x_origin + rect_notebook.x + margin_width + text_width - x_scroll_offset;
	}
	else { // out of screen
		*show_x = 0;
		*show_y = 0;
	}
}

enum {
	MODEL_TYPEDTEXT_INDEX = 0,
	MODEL_LABEL_INDEX = 1,
	MODEL_TYPE_INDEX = 2
};

void SuggestionWindow::show(const cc::CodeCompletionResults& results) {
	if( !results.empty() ) {
		if( this->isShowing() ) { //close and re-show
			this->close();
		}
		gtk_list_store_clear(model);
		g_print("start suggest show");
		ClangCompletePluginPref* pref = get_ClangCompletePluginPref();
		GtkTreeIter iter;
		for(size_t i = 0; i < results.size(); i++) {

			gtk_list_store_append(model, &iter);
			if( pref->row_text_max < results[i].label.length() ) {
				gtk_list_store_set(model, &iter,
					MODEL_TYPEDTEXT_INDEX, results[i].typedText.c_str(),
					MODEL_LABEL_INDEX,     results[i].label.substr(0, pref->row_text_max).c_str(),
					MODEL_TYPE_INDEX,      icon_pixbufs[results[i].type],  -1);
			} else {
				gtk_list_store_set(model, &iter,
					MODEL_TYPEDTEXT_INDEX, results[i].typedText.c_str(),
					MODEL_LABEL_INDEX,     results[i].label.c_str(),
					MODEL_TYPE_INDEX,      icon_pixbufs[results[i].type],  -1);
			}
		}
		gtk_tree_view_columns_autosize(GTK_TREE_VIEW(tree_view));


		// gtk2+
		GtkRequisition win_size;
		gtk_widget_size_request(tree_view, &win_size);
		g_print("(%d, %d)", win_size.width, win_size.height);
		if( win_size.height > pref->suggestion_window_height_max ) {
			win_size.height = pref->suggestion_window_height_max;
		}
		gtk_widget_set_size_request(window, win_size.width, win_size.height);
		/*
			in gtk3 use follow?
			gtk_widget_get_preferred_size (tree_view, &minimum_size, &natural_size);
		*/
		int show_x, show_y;
		get_suggestion_window_coord(&show_x, &show_y);
		gtk_window_move(GTK_WINDOW(window), show_x, show_y);

		showing_flag = true;
		filtered_str = "";

		gtk_widget_show(tree_view);
		gtk_widget_show(window);
		/* call after gtk_widget_show */
		gtk_tree_view_scroll_to_point(GTK_TREE_VIEW(tree_view), 0, 0);
	}
}
void SuggestionWindow::show_with_filter(
	const cc::CodeCompletionResults& results, const std::string& filter) {

	this->show(results);
	this->filter_add(filter);
}

void SuggestionWindow::move_cursor(bool down) {
	GtkTreeIter iter;
	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
	if( gtk_tree_selection_get_selected(selection, NULL, &iter) )
	{
		if( down ) { /* move down */
			if( gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter) ) {
				GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &iter);
				gtk_tree_view_scroll_to_cell(
					GTK_TREE_VIEW(tree_view), path, NULL, FALSE, 0.0, 0.0);
				gtk_tree_view_set_cursor_on_cell(
					GTK_TREE_VIEW(tree_view), path, NULL, NULL, FALSE);
				gtk_tree_path_free(path);
			}
		}
		else { /* move up */
			GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &iter);
			if( gtk_tree_path_prev(path) ) {
				gtk_tree_view_scroll_to_cell(
					GTK_TREE_VIEW(tree_view), path, NULL, FALSE, 0.0, 0.0);
				gtk_tree_view_set_cursor_on_cell(
					GTK_TREE_VIEW(tree_view), path, NULL, NULL, FALSE);
				gtk_tree_path_free(path);
			}
		}
	}
}
void SuggestionWindow::select_suggestion() {
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
	if(gtk_tree_selection_get_selected(selection, &model, &iter))
	{
		gchar *typedtext;
		gtk_tree_model_get (model, &iter, MODEL_TYPEDTEXT_INDEX, &typedtext, -1);

		int dist = strlen(typedtext) - filtered_str.length();
		if( dist == 0 ) {
			/* do nothing */
		}
		else if( dist < 0 ){
			g_printerr("assert SuggestionWindow::select_suggestion()");
		}
		else {
			g_print ("will insert %s (%s)\n", typedtext, &typedtext[filtered_str.length()]);
			GeanyDocument* doc = document_get_current();
			if(doc != NULL) {
				int added_byte  = filtered_str.length();
				char* insert_text = &typedtext[filtered_str.length()];
				int insert_byte = strlen(insert_text);
				int cur_pos = sci_get_current_position(doc->editor->sci);
				sci_insert_text(doc->editor->sci, -1, insert_text);
				sci_set_current_position(doc->editor->sci, cur_pos + insert_byte, FALSE);
			}
		}
		g_free (typedtext);
		this->close();
	}
}
void SuggestionWindow::signal_tree_selection(
	GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column,
	SuggestionWindow* self)
{
	self->select_suggestion();
}
namespace g_sig {
	gboolean close_request(GtkWidget *widget, GdkEvent *event, SuggestionWindow* self) {
		self->close();
		return FALSE;
	}
}

gboolean SuggestionWindow::signal_key_press_and_release(
		GtkWidget *widget, GdkEventKey *event, SuggestionWindow* self)
{
	if( !self->isShowing() ) { return FALSE; }

	switch(event->keyval) {
	/* take over for selecting a suggestion */
	case GDK_KEY_Down: case GDK_KEY_KP_Down:
		self->move_cursor(true);
		return TRUE;
	case GDK_KEY_Up: case GDK_KEY_KP_Up:
		self->move_cursor(false);
		return TRUE;
	/* select current suggestion */
	case GDK_KEY_Return: case GDK_KEY_KP_Enter:
		self->select_suggestion();
		return TRUE;
	case GDK_KEY_BackSpace:
		self->filter_backspace();
		return FALSE; /* editor will delete a char. */
	case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
	case GDK_KEY_Escape: case GDK_KEY_Right:
	case GDK_KEY_Left: case GDK_KEY_KP_Left:
		self->close();
		return TRUE;
	default:
		return FALSE;
	}
}

void SuggestionWindow::do_filtering() {
	if( !this->isShowing() ) { return; }

	GtkTreeIter iter;

	if( gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter) ) {
		do {
			gchar* typedtext;
			gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, MODEL_TYPEDTEXT_INDEX, &typedtext, -1);

			gboolean endFlag = FALSE;
			if( strstr(typedtext, filtered_str.c_str()) == typedtext ) {
				GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &iter);
				gtk_tree_view_scroll_to_cell(
					GTK_TREE_VIEW(tree_view), path, NULL, TRUE, 0.0, 0.0);
				gtk_tree_view_set_cursor_on_cell(
					GTK_TREE_VIEW(tree_view), path, NULL, NULL, FALSE);
				gtk_tree_path_free(path);

				endFlag = TRUE;
			}
			g_free(typedtext);
			if( endFlag ) { return; }
		} while( gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter) );

		/* not found */
		this->close();
	}
	else { /* empty suggestion */
		this->close();
	}

}
void SuggestionWindow::filter_backspace() {
	if( this->isShowing() ) {
		if( filtered_str.empty() ) {
			this->close();
		}
		else {
			filtered_str.erase( filtered_str.length() - 1 );
			this->do_filtering();
		}
	}
}
void SuggestionWindow::filter_add(int ch) {
	if( this->isShowing() ) {
		char buf[8] = {0};
		g_unichar_to_utf8((gunichar)ch, buf);
		filtered_str += buf;
		do_filtering();
	}
}
void SuggestionWindow::filter_add(const std::string& str) {
	if( this->isShowing() ) {
		filtered_str += str;
		do_filtering();
	}
}
gboolean SuggestionWindow::on_editor_notify(
	GObject *obj, GeanyEditor *editor, SCNotification *nt) {

	if( !this->isShowing() ) { return FALSE; }

	switch (nt->nmhdr.code)
	{
		case SCN_UPDATEUI:
			ui_set_statusbar(FALSE, _("%d"), 42);
			/*TODO relocation suggestion window when typings occur scroll (e.g. editting long line) */
			if(nt->updated & SC_UPDATE_SELECTION) {
				this->close();
			}
			break;
		case SCN_MODIFIED:
			break;
		case SCN_CHARADDED:
			{
				this->filter_add(nt->ch);
			}
			break;
		case SCN_USERLISTSELECTION:
			break;
		case SCN_CALLTIPCLICK:
			break;
		default:
			break;
	}

	return FALSE;
}

#include "sw_icon_resources.hpp"



SuggestionWindow::SuggestionWindow() : showing_flag(false) {
	window = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_container_set_border_width(GTK_CONTAINER(window), 1);
	gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_widget_set_size_request(window, 150, 150);

	model = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);

	tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view), FALSE);

	/* create icon pixbuf
	   follow cc::CompleteResultType order */
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_var_icon_pixbuf, FALSE, NULL));
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_method_icon_pixbuf, FALSE, NULL));
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_class_icon_pixbuf, FALSE, NULL));
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_method_icon_pixbuf, FALSE, NULL));
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_var_icon_pixbuf, FALSE, NULL));
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_struct_icon_pixbuf, FALSE, NULL));
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_namespace_icon_pixbuf, FALSE, NULL));
	icon_pixbufs.push_back(
		gdk_pixbuf_new_from_inline (-1, sw_macro_icon_pixbuf, FALSE, NULL));

	GtkCellRenderer* pixbuf_renderer = gtk_cell_renderer_pixbuf_new();
	GtkTreeViewColumn *i_column = gtk_tree_view_column_new_with_attributes(
	"icon", pixbuf_renderer, "pixbuf", 2, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), i_column);

	GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
	GtkTreeViewColumn *t_column = gtk_tree_view_column_new_with_attributes(
		"label", text_renderer, "text", MODEL_LABEL_INDEX ,  NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), t_column);

	GtkTreeSelection* select = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
	gtk_tree_selection_set_mode(select, GTK_SELECTION_BROWSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tree_view), FALSE);

	gtk_container_add(GTK_CONTAINER(window), tree_view);

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
		G_CALLBACK(signal_tree_selection), this);
	g_signal_connect(G_OBJECT(geany_data->main_widgets->window), "key-press-event",
		G_CALLBACK(signal_key_press_and_release), this);
	g_signal_connect(G_OBJECT(geany_data->main_widgets->window), "focus-out-event",
		G_CALLBACK(g_sig::close_request), this);

	gtk_widget_realize(tree_view);

	std::string str;
}


}