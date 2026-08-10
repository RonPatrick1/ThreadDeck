#pragma once

#include <gtkmm/box.h>
#include <gtkmm/label.h>

#include <string>

class ThreadHeader final : public Gtk::Box {
public:
    ThreadHeader();

    void set_thread(
        const std::string& display_label,
        const std::string& working_directory);

    void attach_status_widgets(
        Gtk::Label& label);

    void clear();

private:
    std::string folder_name(
        const std::string& working_directory) const;

    Gtk::Box row_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Box status_box_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Label title_;
    Gtk::Label folder_chip_;
};
