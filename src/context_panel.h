#pragma once

#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/stack.h>

#include <string>

class ContextPanel final : public Gtk::Box {
public:
    ContextPanel();

    void set_details(
        const std::string& thread_label,
        const std::string& project_label,
        const std::string& folder_name,
        const std::string& working_directory,
        const std::string& thread_id);

    void clear();

private:
    void configure_key_label(
        Gtk::Label& label);

    void configure_value_label(
        Gtk::Label& label);

    Gtk::Stack pages_;
    Gtk::Box details_page_{
        Gtk::ORIENTATION_VERTICAL};
    Gtk::Label title_{"Details"};
    Gtk::Grid details_grid_;

    Gtk::Label thread_key_{"Thread"};
    Gtk::Label thread_value_;

    Gtk::Label project_key_{"Project"};
    Gtk::Label project_value_;

    Gtk::Label folder_key_{"Folder"};
    Gtk::Label folder_value_;

    Gtk::Label path_key_{"Path"};
    Gtk::Label path_value_;

    Gtk::Label id_key_{"Thread ID"};
    Gtk::Label id_value_;
};
