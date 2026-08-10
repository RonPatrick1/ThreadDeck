#include "thread_header.h"

#include <filesystem>

ThreadHeader::ThreadHeader()
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL) {
    get_style_context()->add_class(
        "thread-header");

    row_.set_spacing(10);

    title_.set_xalign(0.0F);
    title_.set_hexpand(false);
    title_.set_ellipsize(
        Pango::ELLIPSIZE_END);
    title_.get_style_context()->add_class(
        "thread-header-title");

    folder_chip_.set_xalign(0.5F);
    folder_chip_.set_selectable(true);
    folder_chip_.set_ellipsize(
        Pango::ELLIPSIZE_END);
    folder_chip_.set_max_width_chars(28);
    folder_chip_.get_style_context()->add_class(
        "thread-folder-chip");

    status_box_.set_spacing(6);
    status_box_.set_hexpand(true);
    status_box_.set_halign(Gtk::ALIGN_FILL);

    row_.pack_start(
        title_,
        Gtk::PACK_SHRINK);
    row_.pack_start(
        status_box_,
        Gtk::PACK_EXPAND_WIDGET);
    row_.pack_end(
        folder_chip_,
        Gtk::PACK_SHRINK);

    pack_start(
        row_,
        Gtk::PACK_SHRINK);

    clear();
}

void ThreadHeader::attach_status_widgets(
    Gtk::Label& label
) {
    status_box_.pack_start(
        label,
        Gtk::PACK_SHRINK);
}

std::string ThreadHeader::folder_name(
    const std::string& working_directory
) const {
    if (working_directory.empty()) {
        return "No folder";
    }

    const auto normalized =
        std::filesystem::path(
            working_directory).lexically_normal();

    const std::string name =
        normalized.filename().string();

    return name.empty()
        ? working_directory
        : name;
}

void ThreadHeader::set_thread(
    const std::string& display_label,
    const std::string& working_directory
) {
    title_.set_text(
        display_label.empty()
            ? "Untitled Thread"
            : display_label);

    folder_chip_.set_text(
        folder_name(
            working_directory));

    folder_chip_.set_tooltip_text(
        working_directory);
}

void ThreadHeader::clear() {
    title_.set_text(
        "No active thread");
    folder_chip_.set_text(
        "No folder");
    folder_chip_.set_tooltip_text(
        "");
}
