#include "context_panel.h"

ContextPanel::ContextPanel()
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL) {
    set_size_request(240, -1);
    get_style_context()->add_class(
        "context-panel");

    pages_.set_hexpand(true);
    pages_.set_vexpand(true);

    details_page_.set_border_width(16);
    details_page_.set_spacing(12);

    title_.set_xalign(0.0F);
    title_.get_style_context()->add_class(
        "context-panel-title");

    details_grid_.set_column_spacing(12);
    details_grid_.set_row_spacing(5);
    details_grid_.set_hexpand(true);

    configure_key_label(thread_key_);
    configure_key_label(project_key_);
    configure_key_label(folder_key_);
    configure_key_label(path_key_);
    configure_key_label(id_key_);

    configure_value_label(thread_value_);
    configure_value_label(project_value_);
    configure_value_label(folder_value_);
    configure_value_label(path_value_);
    configure_value_label(id_value_);

    details_grid_.attach(
        thread_key_, 0, 0, 1, 1);
    details_grid_.attach(
        thread_value_, 1, 0, 1, 1);

    details_grid_.attach(
        project_key_, 0, 1, 1, 1);
    details_grid_.attach(
        project_value_, 1, 1, 1, 1);

    details_grid_.attach(
        folder_key_, 0, 2, 1, 1);
    details_grid_.attach(
        folder_value_, 1, 2, 1, 1);

    details_grid_.attach(
        path_key_, 0, 3, 1, 1);
    details_grid_.attach(
        path_value_, 1, 3, 1, 1);

    details_grid_.attach(
        id_key_, 0, 4, 1, 1);
    details_grid_.attach(
        id_value_, 1, 4, 1, 1);

    details_page_.pack_start(
        title_,
        Gtk::PACK_SHRINK);
    details_page_.pack_start(
        details_grid_,
        Gtk::PACK_SHRINK);

    pages_.add(
        details_page_,
        "details",
        "Details");

    pack_start(
        pages_,
        Gtk::PACK_EXPAND_WIDGET);

    clear();
}

void ContextPanel::configure_key_label(
    Gtk::Label& label
) {
    label.set_xalign(0.0F);
    label.set_halign(Gtk::ALIGN_START);
    label.set_valign(Gtk::ALIGN_START);
    label.get_style_context()->add_class(
        "details-key");
}

void ContextPanel::configure_value_label(
    Gtk::Label& label
) {
    label.set_xalign(0.0F);
    label.set_halign(Gtk::ALIGN_FILL);
    label.set_hexpand(true);
    label.set_selectable(true);
    label.set_line_wrap(true);
    label.set_max_width_chars(34);
    label.get_style_context()->add_class(
        "details-value");
}

void ContextPanel::set_details(
    const std::string& thread_label,
    const std::string& project_label,
    const std::string& folder_name,
    const std::string& working_directory,
    const std::string& thread_id
) {
    thread_value_.set_text(
        thread_label);
    project_value_.set_text(
        project_label);
    folder_value_.set_text(
        folder_name);
    path_value_.set_text(
        working_directory);
    id_value_.set_text(
        thread_id);
}

void ContextPanel::clear() {
    thread_value_.set_text("—");
    project_value_.set_text("—");
    folder_value_.set_text("—");
    path_value_.set_text("—");
    id_value_.set_text("—");
}
