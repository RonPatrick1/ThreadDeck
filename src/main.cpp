#include "app_server_client.h"

#include <nlohmann/json.hpp>

#include <gdk/gdkkeysyms.h>
#include <gdkmm/screen.h>
#include <glibmm/dispatcher.h>
#include <glibmm/main.h>
#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/textview.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class MainWindow final : public Gtk::ApplicationWindow {
public:
    MainWindow()
        : root_(Gtk::ORIENTATION_VERTICAL),
          body_(Gtk::ORIENTATION_HORIZONTAL),
          sidebar_(Gtk::ORIENTATION_VERTICAL),
          folder_button_("Select Folder"),
          new_thread_button_("New Thread"),
          send_button_("↑"),
          selected_folder_("No folder selected"),
          status_label_("Codex: starting"),
          sidebar_title_("Threads"),
          current_thread_label_("No active thread") {
        set_title("ThreadDeck");
        set_default_size(1200, 760);

        header_.set_title("ThreadDeck");
        header_.set_subtitle("Native GTK 3 client");
        header_.set_show_close_button(true);
        set_titlebar(header_);

        add(root_);

        folder_button_.signal_clicked().connect(
            sigc::mem_fun(*this, &MainWindow::select_folder));

        new_thread_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::create_thread_for_selected_folder));

        send_button_.signal_clicked().connect(
            sigc::mem_fun(*this, &MainWindow::submit_prompt));

        turn_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_turn_finished));

        prompt_.signal_key_press_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::on_prompt_key_press),
            false);

        prompt_.get_buffer()->signal_changed().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_prompt_changed));

        new_thread_button_.set_sensitive(false);
        send_button_.set_sensitive(false);

        send_button_.set_relief(Gtk::RELIEF_NONE);
        send_button_.set_size_request(42, 42);
        send_button_.set_valign(Gtk::ALIGN_END);
        send_button_.set_tooltip_text("Send message (Enter)");

        header_.pack_start(folder_button_);
        header_.pack_end(new_thread_button_);

        selected_folder_.set_xalign(0.0F);
        selected_folder_.set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
        root_.pack_start(selected_folder_, Gtk::PACK_SHRINK);

        status_label_.set_xalign(0.0F);
        root_.pack_start(status_label_, Gtk::PACK_SHRINK);

        sidebar_title_.set_xalign(0.0F);
        sidebar_.set_border_width(12);
        sidebar_.set_spacing(8);
        sidebar_.pack_start(sidebar_title_, Gtk::PACK_SHRINK);

        current_thread_label_.set_xalign(0.0F);
        current_thread_label_.set_line_wrap(true);
        current_thread_label_.set_selectable(true);
        sidebar_.pack_start(
            current_thread_label_,
            Gtk::PACK_SHRINK);

        sidebar_list_.set_spacing(4);
        sidebar_scroll_.set_policy(
            Gtk::POLICY_NEVER,
            Gtk::POLICY_AUTOMATIC);
        sidebar_scroll_.set_shadow_type(Gtk::SHADOW_NONE);
        sidebar_scroll_.add(sidebar_list_);
        sidebar_.pack_start(
            sidebar_scroll_,
            Gtk::PACK_EXPAND_WIDGET);

        transcript_.get_buffer()->set_text(
            "ThreadDeck\n\n"
            "Select a folder to create a Codex thread.");

        transcript_.set_editable(false);
        transcript_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
        transcript_scroll_.add(transcript_);

        prompt_.get_buffer()->set_text("");
        prompt_.set_name("prompt-input");
        prompt_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
        prompt_.set_left_margin(4);
        prompt_.set_right_margin(4);
        prompt_.set_top_margin(8);
        prompt_.set_bottom_margin(8);
        prompt_.set_tooltip_text(
            "Enter to send; Shift+Enter for a new line");

        prompt_scroll_.set_name("prompt-scroll");
        prompt_scroll_.set_policy(
            Gtk::POLICY_NEVER,
            Gtk::POLICY_AUTOMATIC);
        prompt_scroll_.set_shadow_type(Gtk::SHADOW_NONE);
        prompt_scroll_.set_size_request(-1, 48);
        prompt_scroll_.add(prompt_);

        content_.set_orientation(Gtk::ORIENTATION_VERTICAL);
        content_.set_spacing(8);
        content_.set_border_width(8);
        content_.pack_start(
            transcript_scroll_,
            Gtk::PACK_EXPAND_WIDGET);

        composer_.set_spacing(8);
        composer_.get_style_context()->add_class("composer");
        composer_.pack_start(
            prompt_scroll_,
            Gtk::PACK_EXPAND_WIDGET);
        composer_.pack_end(
            send_button_,
            Gtk::PACK_SHRINK);

        content_.pack_start(
            composer_,
            Gtk::PACK_SHRINK);

        body_.pack1(sidebar_, false, false);
        body_.pack2(content_, true, false);
        body_.set_position(260);

        root_.pack_start(body_, Gtk::PACK_EXPAND_WIDGET);

        load_ui_state();
        apply_main_window_state();

        signal_configure_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_main_window_configure),
            false);

        signal_window_state_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_main_window_state),
            false);

        signal_delete_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_main_window_delete),
            false);

        configure_composer_style();
        update_prompt_height();
        initialize_app_server();
        show_all_children();
    }

    ~MainWindow() override {
        if (turn_worker_.joinable()) {
            turn_worker_.join();
        }
    }

private:
    std::filesystem::path ui_state_path() const {
        const char* xdg_config_home =
            std::getenv("XDG_CONFIG_HOME");

        if (
            xdg_config_home != nullptr &&
            *xdg_config_home != '\0'
        ) {
            return std::filesystem::path(
                xdg_config_home
            ) / "threaddeck" / "ui-state.json";
        }

        const char* home = std::getenv("HOME");

        if (home != nullptr && *home != '\0') {
            return std::filesystem::path(
                home
            ) / ".config" /
                "threaddeck" /
                "ui-state.json";
        }

        return std::filesystem::path(
            ".threaddeck-ui-state.json");
    }

    void load_ui_state() {
        const auto state_path = ui_state_path();

        if (!std::filesystem::is_regular_file(state_path)) {
            return;
        }

        try {
            std::ifstream input(state_path);

            if (!input) {
                std::cerr
                    << "FAIL: could not open UI state file "
                    << state_path
                    << '\n';
                return;
            }

            nlohmann::json state;
            input >> state;

            chooser_has_geometry_ =
                state.value("hasGeometry", false);

            chooser_x_ =
                state.value("x", 0);

            chooser_y_ =
                state.value("y", 0);

            chooser_width_ =
                state.value("width", 900);

            chooser_height_ =
                state.value("height", 650);

            chooser_monitor_ =
                state.value("monitor", -1);

            chooser_last_folder_ =
                state.value(
                    "lastFolder",
                    std::string{});

            selected_folder_path_ =
                state.value(
                    "selectedFolder",
                    std::string{});

            last_active_thread_id_ =
                state.value(
                    "activeThreadId",
                    std::string{});

            last_active_thread_cwd_ =
                state.value(
                    "activeThreadCwd",
                    std::string{});

            selected_project_folders_.clear();

            if (
                state.contains("projectFolders") &&
                state["projectFolders"].is_array()
            ) {
                for (
                    const auto& folder :
                    state["projectFolders"]
                ) {
                    if (
                        folder.is_string() &&
                        !folder.get<std::string>().empty()
                    ) {
                        selected_project_folders_.push_back(
                            folder.get<std::string>());
                    }
                }
            }

            if (
                !selected_folder_path_.empty() &&
                std::find(
                    selected_project_folders_.begin(),
                    selected_project_folders_.end(),
                    selected_folder_path_) ==
                    selected_project_folders_.end()
            ) {
                selected_project_folders_.push_back(
                    selected_folder_path_);
            }

            if (
                state.contains("folderLabels") &&
                state["folderLabels"].is_object()
            ) {
                folder_labels_ =
                    state["folderLabels"].get<
                        std::map<
                            std::string,
                            std::string>>();
            }

            if (
                state.contains("threadLabels") &&
                state["threadLabels"].is_object()
            ) {
                thread_labels_ =
                    state["threadLabels"].get<
                        std::map<
                            std::string,
                            std::string>>();
            }

            if (!selected_folder_path_.empty()) {
                selected_folder_.set_text(
                    selected_folder_path_);
            }

            if (
                state.contains("mainWindow") &&
                state["mainWindow"].is_object()
            ) {
                const auto& main_window =
                    state["mainWindow"];

                main_window_has_geometry_ =
                    main_window.value(
                        "hasGeometry",
                        false);

                main_window_x_ =
                    main_window.value("x", 0);

                main_window_y_ =
                    main_window.value("y", 0);

                main_window_width_ =
                    main_window.value(
                        "width",
                        1200);

                main_window_height_ =
                    main_window.value(
                        "height",
                        760);

                main_window_monitor_ =
                    main_window.value(
                        "monitor",
                        -1);

                main_window_maximized_ =
                    main_window.value(
                        "maximized",
                        false);
            }

            std::cout
                << "PASS: loaded file chooser UI state from "
                << state_path
                << '\n';

        } catch (const std::exception& error) {
            std::cerr
                << "FAIL: could not read UI state: "
                << error.what()
                << '\n';
        }
    }

    void save_ui_state() const {
        const auto state_path = ui_state_path();
        const auto temporary_path =
            std::filesystem::path(
                state_path.string() + ".tmp");

        try {
            std::filesystem::create_directories(
                state_path.parent_path());

            const nlohmann::json state = {
                {
                    "hasGeometry",
                    chooser_has_geometry_,
                },
                {"x", chooser_x_},
                {"y", chooser_y_},
                {"width", chooser_width_},
                {"height", chooser_height_},
                {"monitor", chooser_monitor_},
                {
                    "lastFolder",
                    chooser_last_folder_,
                },
                {
                    "selectedFolder",
                    selected_folder_path_,
                },
                {
                    "projectFolders",
                    selected_project_folders_,
                },
                {
                    "folderLabels",
                    folder_labels_,
                },
                {
                    "threadLabels",
                    thread_labels_,
                },
                {
                    "activeThreadId",
                    last_active_thread_id_,
                },
                {
                    "activeThreadCwd",
                    last_active_thread_cwd_,
                },
                {
                    "mainWindow",
                    {
                        {
                            "hasGeometry",
                            main_window_has_geometry_,
                        },
                        {"x", main_window_x_},
                        {"y", main_window_y_},
                        {
                            "width",
                            main_window_width_,
                        },
                        {
                            "height",
                            main_window_height_,
                        },
                        {
                            "monitor",
                            main_window_monitor_,
                        },
                        {
                            "maximized",
                            main_window_maximized_,
                        },
                    },
                },
            };

            {
                std::ofstream output(
                    temporary_path,
                    std::ios::trunc);

                if (!output) {
                    throw std::runtime_error(
                        "could not open temporary state file");
                }

                output
                    << state.dump(2)
                    << '\n';

                if (!output) {
                    throw std::runtime_error(
                        "could not write temporary state file");
                }
            }

            std::filesystem::rename(
                temporary_path,
                state_path);

            std::cout
                << "PASS: saved file chooser UI state to "
                << state_path
                << '\n';

        } catch (const std::exception& error) {
            std::error_code ignored_error;
            std::filesystem::remove(
                temporary_path,
                ignored_error);

            std::cerr
                << "FAIL: could not save UI state: "
                << error.what()
                << '\n';
        }
    }

    void apply_main_window_state() {
        if (!main_window_has_geometry_) {
            return;
        }

        const auto screen =
            Gdk::Screen::get_default();

        if (!screen) {
            set_default_size(
                main_window_width_,
                main_window_height_);

            move(
                main_window_x_,
                main_window_y_);

            if (main_window_maximized_) {
                maximize();
            }

            return;
        }

        int monitor = main_window_monitor_;

        if (
            monitor < 0 ||
            monitor >= screen->get_n_monitors()
        ) {
            monitor =
                screen->get_primary_monitor();
        }

        if (
            monitor < 0 ||
            monitor >= screen->get_n_monitors()
        ) {
            monitor = 0;
        }

        const Gdk::Rectangle workarea =
            screen->get_monitor_workarea(monitor);

        const int work_x =
            workarea.get_x();

        const int work_y =
            workarea.get_y();

        const int work_width =
            std::max(
                1,
                workarea.get_width());

        const int work_height =
            std::max(
                1,
                workarea.get_height());

        const int width =
            std::min(
                std::max(
                    main_window_width_,
                    640),
                work_width);

        const int height =
            std::min(
                std::max(
                    main_window_height_,
                    480),
                work_height);

        const int x =
            std::max(
                work_x,
                std::min(
                    main_window_x_,
                    work_x +
                        work_width -
                        width));

        const int y =
            std::max(
                work_y,
                std::min(
                    main_window_y_,
                    work_y +
                        work_height -
                        height));

        set_default_size(width, height);
        move(x, y);

        if (main_window_maximized_) {
            maximize();
        }
    }

    bool handle_main_window_configure(
        GdkEventConfigure* event
    ) {
        if (
            event == nullptr ||
            main_window_maximized_
        ) {
            return false;
        }

        main_window_x_ =
            event->x;

        main_window_y_ =
            event->y;

        main_window_width_ =
            event->width;

        main_window_height_ =
            event->height;

        main_window_has_geometry_ = true;

        const auto screen =
            Gdk::Screen::get_default();

        if (screen) {
            main_window_monitor_ =
                screen->get_monitor_at_point(
                    main_window_x_ +
                        (main_window_width_ / 2),
                    main_window_y_ +
                        (main_window_height_ / 2));
        }

        return false;
    }

    bool handle_main_window_state(
        GdkEventWindowState* event
    ) {
        if (event == nullptr) {
            return false;
        }

        main_window_maximized_ =
            (
                event->new_window_state &
                GDK_WINDOW_STATE_MAXIMIZED
            ) != 0;

        return false;
    }

    bool handle_main_window_delete(
        GdkEventAny*
    ) {
        save_ui_state();

        std::cout
            << "PASS: captured main window state "
            << main_window_width_
            << "x"
            << main_window_height_
            << " at "
            << main_window_x_
            << ","
            << main_window_y_
            << " on monitor "
            << main_window_monitor_
            << (
                main_window_maximized_
                    ? " maximized"
                    : " normal"
            )
            << '\n';

        return false;
    }

    int main_window_monitor(
        const Glib::RefPtr<Gdk::Screen>& screen
    ) const {
        if (!screen) {
            return -1;
        }

        int main_x = 0;
        int main_y = 0;
        int main_width = 0;
        int main_height = 0;

        get_position(main_x, main_y);
        get_size(main_width, main_height);

        return screen->get_monitor_at_point(
            main_x + (main_width / 2),
            main_y + (main_height / 2));
    }

    void apply_file_chooser_state(
        Gtk::FileChooserDialog& dialog
    ) {
        const std::string initial_folder =
            !chooser_last_folder_.empty()
                ? chooser_last_folder_
                : selected_folder_path_;

        if (!initial_folder.empty()) {
            std::error_code folder_error;

            if (
                std::filesystem::is_directory(
                    initial_folder,
                    folder_error)
            ) {
                dialog.set_current_folder(
                    initial_folder);
            }
        }

        const auto screen =
            Gdk::Screen::get_default();

        if (!screen) {
            dialog.set_default_size(
                chooser_width_,
                chooser_height_);
            return;
        }

        int monitor = chooser_monitor_;

        if (
            monitor < 0 ||
            monitor >= screen->get_n_monitors()
        ) {
            monitor = main_window_monitor(screen);
        }

        if (
            monitor < 0 ||
            monitor >= screen->get_n_monitors()
        ) {
            monitor = screen->get_primary_monitor();
        }

        if (
            monitor < 0 ||
            monitor >= screen->get_n_monitors()
        ) {
            monitor = 0;
        }

        const Gdk::Rectangle workarea =
            screen->get_monitor_workarea(monitor);

        const int work_x = workarea.get_x();
        const int work_y = workarea.get_y();
        const int work_width =
            std::max(1, workarea.get_width());
        const int work_height =
            std::max(1, workarea.get_height());

        int width =
            chooser_has_geometry_
                ? chooser_width_
                : 900;

        int height =
            chooser_has_geometry_
                ? chooser_height_
                : 650;

        width = std::min(
            std::max(width, 480),
            work_width);

        height = std::min(
            std::max(height, 360),
            work_height);

        int x =
            chooser_has_geometry_
                ? chooser_x_
                : work_x +
                    ((work_width - width) / 2);

        int y =
            chooser_has_geometry_
                ? chooser_y_
                : work_y +
                    ((work_height - height) / 2);

        x = std::max(
            work_x,
            std::min(
                x,
                work_x + work_width - width));

        y = std::max(
            work_y,
            std::min(
                y,
                work_y + work_height - height));

        dialog.set_default_size(width, height);
        dialog.move(x, y);
    }

    void capture_file_chooser_state(
        Gtk::FileChooserDialog& dialog,
        int response
    ) {
        dialog.get_position(
            chooser_x_,
            chooser_y_);

        dialog.get_size(
            chooser_width_,
            chooser_height_);

        chooser_has_geometry_ = true;

        const auto screen =
            Gdk::Screen::get_default();

        if (screen) {
            chooser_monitor_ =
                screen->get_monitor_at_point(
                    chooser_x_ +
                        (chooser_width_ / 2),
                    chooser_y_ +
                        (chooser_height_ / 2));
        }

        if (response == Gtk::RESPONSE_OK) {
            chooser_last_folder_ =
                dialog.get_filename();
        } else {
            const std::string current_folder =
                dialog.get_current_folder();

            if (!current_folder.empty()) {
                chooser_last_folder_ =
                    current_folder;
            }
        }

        save_ui_state();
    }

    void configure_composer_style() {
        css_provider_ = Gtk::CssProvider::create();

        css_provider_->load_from_data(R"CSS(
.composer {
    background-color: @theme_base_color;
    border: 1px solid alpha(@theme_fg_color, 0.22);
    border-radius: 24px;
    padding: 5px 6px 5px 12px;
}

#prompt-scroll,
#prompt-input,
#prompt-input text {
    background-color: transparent;
    border: none;
    box-shadow: none;
}

.send-button {
    background-image: none;
    background-color: @theme_selected_bg_color;
    color: @theme_selected_fg_color;
    border: none;
    border-radius: 999px;
    min-width: 42px;
    min-height: 42px;
    padding: 0;
    font-size: 24px;
    font-weight: bold;
}

.send-button:hover {
    background-color: shade(@theme_selected_bg_color, 1.08);
}

.send-button:disabled {
    opacity: 0.42;
}

.folder-heading {
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 2px;
}

.thread-row {
    padding: 5px 7px;
}

.thread-row.active-thread {
    font-weight: bold;
}

.sidebar-label-editor {
    margin: 2px 0;
}

.sidebar-more-button {
    min-width: 28px;
    min-height: 28px;
    padding: 0 4px;
    font-weight: bold;
}
)CSS");

        const auto screen = Gdk::Screen::get_default();

        if (screen) {
            Gtk::StyleContext::add_provider_for_screen(
                screen,
                css_provider_,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        send_button_.get_style_context()->add_class(
            "send-button");
    }

    bool on_prompt_key_press(GdkEventKey* event) {
        if (event == nullptr) {
            return false;
        }

        const bool is_enter =
            event->keyval == GDK_KEY_Return ||
            event->keyval == GDK_KEY_KP_Enter;

        if (!is_enter) {
            return false;
        }

        const bool shift_pressed =
            (event->state & GDK_SHIFT_MASK) != 0;

        if (shift_pressed) {
            return false;
        }

        submit_prompt();
        return true;
    }

    void handle_prompt_changed() {
        update_prompt_height();
        update_send_button_state();
    }

    void update_prompt_height() {
        const std::string text =
            prompt_.get_buffer()->get_text().raw();

        const int available_width = std::max(
            240,
            prompt_scroll_.get_allocated_width());

        const std::size_t characters_per_line =
            static_cast<std::size_t>(
                std::max(
                    20,
                    (available_width - 32) / 9));

        int visual_lines = 0;
        std::size_t line_start = 0;

        while (true) {
            const std::size_t line_end =
                text.find('\n', line_start);

            const std::size_t character_count =
                (
                    line_end == std::string::npos
                        ? text.size()
                        : line_end
                ) - line_start;

            const int wrapped_lines = std::max(
                1,
                static_cast<int>(
                    (
                        character_count +
                        characters_per_line -
                        1
                    ) /
                    characters_per_line));

            visual_lines += wrapped_lines;

            if (line_end == std::string::npos) {
                break;
            }

            line_start = line_end + 1;
        }

        const int visible_lines =
            std::clamp(visual_lines, 1, 5);

        const int requested_height =
            24 + (visible_lines * 24);

        prompt_scroll_.set_size_request(
            -1,
            requested_height);
    }

    void update_send_button_state() {
        const std::string prompt_text = trim(
            prompt_.get_buffer()->get_text().raw());

        send_button_.set_sensitive(
            !turn_in_progress_ &&
            !current_thread_id_.empty() &&
            !prompt_text.empty());
    }

    static std::string trim(const std::string& value) {
        const auto first = value.find_first_not_of(
            " \t\r\n");

        if (first == std::string::npos) {
            return {};
        }

        const auto last = value.find_last_not_of(
            " \t\r\n");

        return value.substr(first, last - first + 1);
    }


    static std::string single_line_preview(
        std::string value,
        std::size_t maximum_length = 58
    ) {
        std::replace(
            value.begin(),
            value.end(),
            '\n',
            ' ');

        std::replace(
            value.begin(),
            value.end(),
            '\r',
            ' ');

        value = trim(value);

        if (value.size() > maximum_length) {
            value.resize(maximum_length);
            value += "...";
        }

        return value;
    }

    std::string display_folder_label(
        const std::string& cwd
    ) const {
        const auto custom_label =
            folder_labels_.find(cwd);

        if (
            custom_label != folder_labels_.end() &&
            !custom_label->second.empty()
        ) {
            return custom_label->second;
        }

        std::string label =
            std::filesystem::path(cwd)
                .filename()
                .string();

        if (label.empty()) {
            label = cwd;
        }

        return label;
    }

    std::string display_thread_label(
        const nlohmann::json& thread
    ) const {
        const std::string thread_id =
            thread.value(
                "id",
                std::string{});

        const auto custom_label =
            thread_labels_.find(thread_id);

        if (
            custom_label != thread_labels_.end() &&
            !custom_label->second.empty()
        ) {
            return custom_label->second;
        }

        if (
            thread.contains("name") &&
            thread["name"].is_string()
        ) {
            const std::string name =
                single_line_preview(
                    thread["name"].get<std::string>());

            if (!name.empty()) {
                return name;
            }
        }

        const std::string preview =
            single_line_preview(
                thread.value(
                    "preview",
                    std::string{}));

        if (!preview.empty()) {
            return preview;
        }

        const std::size_t id_length =
            std::min<std::size_t>(
                8,
                thread_id.size());

        return (
            "Thread " +
            thread_id.substr(0, id_length)
        );
    }


    void schedule_sidebar_refresh() {
        Glib::signal_idle().connect(
            [this]() {
                refresh_sidebar_threads();
                return false;
            });
    }

    void begin_folder_label_edit(
        const std::string& cwd
    ) {
        editing_folder_cwd_ = cwd;
        editing_thread_id_.clear();
        schedule_sidebar_refresh();
    }

    void begin_thread_label_edit(
        const std::string& thread_id
    ) {
        editing_thread_id_ = thread_id;
        editing_folder_cwd_.clear();
        schedule_sidebar_refresh();
    }

    bool handle_label_edit_key(
        GdkEventKey* event,
        bool is_folder,
        const std::string& metadata_key,
        Gtk::Entry* entry
    ) {
        if (
            event == nullptr ||
            entry == nullptr
        ) {
            return false;
        }

        if (event->keyval == GDK_KEY_Escape) {
            editing_folder_cwd_.clear();
            editing_thread_id_.clear();
            schedule_sidebar_refresh();
            return true;
        }

        const bool is_enter =
            event->keyval == GDK_KEY_Return ||
            event->keyval == GDK_KEY_KP_Enter;

        if (!is_enter) {
            return false;
        }

        const std::string label =
            trim(entry->get_text().raw());

        auto& labels =
            is_folder
                ? folder_labels_
                : thread_labels_;

        if (label.empty()) {
            labels.erase(metadata_key);
        } else {
            labels[metadata_key] = label;
        }

        editing_folder_cwd_.clear();
        editing_thread_id_.clear();

        save_ui_state();
        schedule_sidebar_refresh();

        std::cout
            << "PASS: saved "
            << (
                is_folder
                    ? "folder"
                    : "thread"
            )
            << " display label for "
            << metadata_key
            << " without changing the underlying resource\n";

        return true;
    }

    void clear_sidebar_list() {
        const auto children =
            sidebar_list_.get_children();

        for (auto* child : children) {
            if (child != nullptr) {
                sidebar_list_.remove(*child);
            }
        }
    }

    void add_project_folder(
        const std::string& cwd
    ) {
        if (cwd.empty()) {
            return;
        }

        if (
            std::find(
                selected_project_folders_.begin(),
                selected_project_folders_.end(),
                cwd) ==
            selected_project_folders_.end()
        ) {
            selected_project_folders_.push_back(cwd);
        }

        selected_folder_path_ = cwd;
        selected_folder_.set_text(cwd);

        new_thread_button_.set_sensitive(
            app_server_.is_running() &&
            !turn_in_progress_);

        save_ui_state();
    }

    void refresh_sidebar_threads() {
        clear_sidebar_list();

        Gtk::Entry* editor_to_focus = nullptr;

        if (selected_project_folders_.empty()) {
            auto* empty_label =
                Gtk::manage(
                    new Gtk::Label(
                        "Select a project folder."));

            empty_label->set_xalign(0.0F);
            empty_label->set_line_wrap(true);

            sidebar_list_.pack_start(
                *empty_label,
                Gtk::PACK_SHRINK);

            sidebar_list_.show_all_children();
            return;
        }

        for (
            const std::string& cwd :
            selected_project_folders_
        ) {
            auto* folder_row =
                Gtk::manage(
                    new Gtk::Box(
                        Gtk::ORIENTATION_HORIZONTAL));

            folder_row->set_spacing(4);

            if (editing_folder_cwd_ == cwd) {
                auto* folder_entry =
                    Gtk::manage(
                        new Gtk::Entry());

                const auto custom_label =
                    folder_labels_.find(cwd);

                folder_entry->set_text(
                    custom_label != folder_labels_.end()
                        ? custom_label->second
                        : display_folder_label(cwd));

                folder_entry->set_hexpand(true);
                folder_entry->set_tooltip_text(
                    "Enter saves; Escape cancels");
                folder_entry->get_style_context()
                    ->add_class(
                        "sidebar-label-editor");

                folder_entry
                    ->signal_key_press_event()
                    .connect(
                        [
                            this,
                            cwd,
                            folder_entry
                        ](GdkEventKey* event) {
                            return handle_label_edit_key(
                                event,
                                true,
                                cwd,
                                folder_entry);
                        },
                        false);

                folder_row->pack_start(
                    *folder_entry,
                    Gtk::PACK_EXPAND_WIDGET);

                editor_to_focus = folder_entry;

            } else {
                auto* folder_heading =
                    Gtk::manage(
                        new Gtk::Label(
                            display_folder_label(cwd)));

                folder_heading->set_xalign(0.0F);
                folder_heading->set_hexpand(true);
                folder_heading->set_ellipsize(
                    Pango::ELLIPSIZE_MIDDLE);
                folder_heading->set_tooltip_text(cwd);
                folder_heading->get_style_context()
                    ->add_class("folder-heading");

                folder_row->pack_start(
                    *folder_heading,
                    Gtk::PACK_EXPAND_WIDGET);
            }

            auto* folder_more_button =
                Gtk::manage(
                    new Gtk::Button("⋮"));

            folder_more_button->set_relief(
                Gtk::RELIEF_NONE);
            folder_more_button->set_tooltip_text(
                "Edit folder display label");
            folder_more_button->get_style_context()
                ->add_class(
                    "sidebar-more-button");

            folder_more_button
                ->signal_clicked()
                .connect(
                    [this, cwd]() {
                        begin_folder_label_edit(cwd);
                    });

            folder_row->pack_end(
                *folder_more_button,
                Gtk::PACK_SHRINK);

            sidebar_list_.pack_start(
                *folder_row,
                Gtk::PACK_SHRINK);

            if (!app_server_.is_running()) {
                auto* unavailable_label =
                    Gtk::manage(
                        new Gtk::Label(
                            "Codex is not connected."));

                unavailable_label->set_xalign(0.0F);

                sidebar_list_.pack_start(
                    *unavailable_label,
                    Gtk::PACK_SHRINK);

                continue;
            }

            const auto result =
                app_server_.list_threads(
                    cwd,
                    100);

            if (!result.success) {
                auto* error_label =
                    Gtk::manage(
                        new Gtk::Label(
                            "Unable to load threads."));

                error_label->set_xalign(0.0F);
                error_label->set_line_wrap(true);

                sidebar_list_.pack_start(
                    *error_label,
                    Gtk::PACK_SHRINK);

                std::cerr
                    << "FAIL: thread/list for "
                    << cwd
                    << ": "
                    << result.error
                    << '\n';

                continue;
            }

            bool displayed_thread = false;
            bool current_thread_was_listed = false;

            for (
                const auto& thread :
                result.threads
            ) {
                if (
                    !thread.is_object() ||
                    !thread.contains("id") ||
                    !thread["id"].is_string()
                ) {
                    continue;
                }

                const std::string thread_id =
                    thread["id"].get<std::string>();

                const bool is_current =
                    thread_id == current_thread_id_;

                auto* thread_row =
                    Gtk::manage(
                        new Gtk::Box(
                            Gtk::ORIENTATION_HORIZONTAL));

                thread_row->set_spacing(4);

                if (
                    editing_thread_id_ ==
                    thread_id
                ) {
                    auto* thread_entry =
                        Gtk::manage(
                            new Gtk::Entry());

                    const auto custom_label =
                        thread_labels_.find(
                            thread_id);

                    thread_entry->set_text(
                        custom_label !=
                                thread_labels_.end()
                            ? custom_label->second
                            : display_thread_label(
                                thread));

                    thread_entry->set_hexpand(true);
                    thread_entry->set_tooltip_text(
                        "Enter saves; Escape cancels");
                    thread_entry->get_style_context()
                        ->add_class(
                            "sidebar-label-editor");

                    thread_entry
                        ->signal_key_press_event()
                        .connect(
                            [
                                this,
                                thread_id,
                                thread_entry
                            ](GdkEventKey* event) {
                                return handle_label_edit_key(
                                    event,
                                    false,
                                    thread_id,
                                    thread_entry);
                            },
                            false);

                    thread_row->pack_start(
                        *thread_entry,
                        Gtk::PACK_EXPAND_WIDGET);

                    editor_to_focus = thread_entry;

                } else {
                    auto* thread_button =
                        Gtk::manage(
                            new Gtk::Button(
                                (
                                    is_current
                                        ? "● "
                                        : ""
                                ) +
                                display_thread_label(
                                    thread)));

                    thread_button->set_relief(
                        Gtk::RELIEF_NONE);
                    thread_button->set_alignment(
                        0.0F,
                        0.5F);
                    thread_button->set_hexpand(true);
                    thread_button->set_tooltip_text(
                        thread_id);
                    thread_button->get_style_context()
                        ->add_class("thread-row");

                    if (is_current) {
                        thread_button
                            ->get_style_context()
                            ->add_class(
                                "active-thread");

                        current_thread_was_listed = true;
                    }

                    thread_button
                        ->signal_clicked()
                        .connect(
                            [
                                this,
                                cwd,
                                thread_id
                            ]() {
                                activate_thread(
                                    cwd,
                                    thread_id);
                            });

                    thread_row->pack_start(
                        *thread_button,
                        Gtk::PACK_EXPAND_WIDGET);
                }

                auto* thread_more_button =
                    Gtk::manage(
                        new Gtk::Button("⋮"));

                thread_more_button->set_relief(
                    Gtk::RELIEF_NONE);
                thread_more_button->set_tooltip_text(
                    "Edit thread display label");
                thread_more_button
                    ->get_style_context()
                    ->add_class(
                        "sidebar-more-button");

                thread_more_button
                    ->signal_clicked()
                    .connect(
                        [this, thread_id]() {
                            begin_thread_label_edit(
                                thread_id);
                        });

                thread_row->pack_end(
                    *thread_more_button,
                    Gtk::PACK_SHRINK);

                sidebar_list_.pack_start(
                    *thread_row,
                    Gtk::PACK_SHRINK);

                displayed_thread = true;
            }

            if (
                cwd == selected_folder_path_ &&
                !current_thread_id_.empty() &&
                !current_thread_was_listed
            ) {
                auto* pending_button =
                    Gtk::manage(
                        new Gtk::Button(
                            "● New thread "
                            "(not saved yet)"));

                pending_button->set_relief(
                    Gtk::RELIEF_NONE);
                pending_button->set_alignment(
                    0.0F,
                    0.5F);
                pending_button->set_sensitive(false);
                pending_button->set_tooltip_text(
                    current_thread_id_);
                pending_button->get_style_context()
                    ->add_class("thread-row");
                pending_button->get_style_context()
                    ->add_class("active-thread");

                sidebar_list_.pack_start(
                    *pending_button,
                    Gtk::PACK_SHRINK);

                displayed_thread = true;
            }

            if (!displayed_thread) {
                auto* empty_label =
                    Gtk::manage(
                        new Gtk::Label(
                            "No saved threads yet."));

                empty_label->set_xalign(0.0F);

                sidebar_list_.pack_start(
                    *empty_label,
                    Gtk::PACK_SHRINK);
            }
        }

        sidebar_list_.show_all_children();

        if (editor_to_focus != nullptr) {
            editor_to_focus->grab_focus();
            editor_to_focus->select_region(0, -1);
        }
    }

    static void append_rendered_block(
        std::string& transcript,
        const std::string& heading,
        const std::string& body
    ) {
        if (body.empty()) {
            return;
        }

        if (!transcript.empty()) {
            transcript += "\n\n";
        }

        transcript += heading;
        transcript += ":\n";
        transcript += body;
    }

    static std::string render_user_content(
        const nlohmann::json& content
    ) {
        if (!content.is_array()) {
            return "[Invalid stored user content]";
        }

        std::string rendered;

        const auto append_line =
            [&rendered](
                const std::string& line
            ) {
                if (!rendered.empty()) {
                    rendered += '\n';
                }

                rendered += line;
            };

        for (const auto& input : content) {
            if (!input.is_object()) {
                append_line(
                    "[Unsupported stored input]");
                continue;
            }

            const std::string type =
                input.value(
                    "type",
                    std::string{});

            if (type == "text") {
                append_line(
                    input.value(
                        "text",
                        std::string{}));

            } else if (type == "image") {
                append_line("[Image: URL]");

            } else if (type == "localImage") {
                append_line(
                    "[Local image: " +
                    input.value(
                        "path",
                        std::string{"unknown"}) +
                    "]");

            } else if (type == "audio") {
                append_line("[Audio: URL]");

            } else if (type == "localAudio") {
                append_line(
                    "[Local audio: " +
                    input.value(
                        "path",
                        std::string{"unknown"}) +
                    "]");

            } else if (type == "skill") {
                append_line(
                    "[Skill: " +
                    input.value(
                        "name",
                        std::string{"unknown"}) +
                    "]");

            } else if (type == "mention") {
                append_line(
                    "[Mention: " +
                    input.value(
                        "name",
                        std::string{"unknown"}) +
                    "]");

            } else {
                append_line(
                    "[Unsupported stored input: " +
                    (
                        type.empty()
                            ? std::string{"unknown"}
                            : type
                    ) +
                    "]");
            }
        }

        return rendered;
    }

    void render_thread_transcript(
        const nlohmann::json& thread
    ) {
        std::string rendered;

        if (
            thread.contains("turns") &&
            thread["turns"].is_array()
        ) {
            for (
                const auto& turn :
                thread["turns"]
            ) {
                if (
                    !turn.is_object() ||
                    !turn.contains("items") ||
                    !turn["items"].is_array()
                ) {
                    continue;
                }

                for (
                    const auto& item :
                    turn["items"]
                ) {
                    if (!item.is_object()) {
                        append_rendered_block(
                            rendered,
                            "Activity",
                            "[Invalid stored thread item]");
                        continue;
                    }

                    const std::string type =
                        item.value(
                            "type",
                            std::string{});

                    if (type == "userMessage") {
                        append_rendered_block(
                            rendered,
                            "You",
                            render_user_content(
                                item.value(
                                    "content",
                                    nlohmann::json::array())));

                    } else if (type == "agentMessage") {
                        const std::string phase =
                            item.value(
                                "phase",
                                std::string{});

                        append_rendered_block(
                            rendered,
                            (
                                phase == "commentary"
                                    ? "Codex commentary"
                                    : "Codex"
                            ),
                            item.value(
                                "text",
                                std::string{}));

                    } else if (type == "plan") {
                        append_rendered_block(
                            rendered,
                            "Codex plan",
                            item.value(
                                "text",
                                std::string{}));

                    } else if (type == "reasoning") {
                        std::string reasoning;

                        const auto append_reasoning =
                            [&reasoning](
                                const nlohmann::json& values
                            ) {
                                if (!values.is_array()) {
                                    return;
                                }

                                for (
                                    const auto& value :
                                    values
                                ) {
                                    if (
                                        !value.is_string() ||
                                        value.get<std::string>()
                                            .empty()
                                    ) {
                                        continue;
                                    }

                                    if (!reasoning.empty()) {
                                        reasoning += '\n';
                                    }

                                    reasoning +=
                                        value.get<std::string>();
                                }
                            };

                        append_reasoning(
                            item.value(
                                "summary",
                                nlohmann::json::array()));

                        append_reasoning(
                            item.value(
                                "content",
                                nlohmann::json::array()));

                        append_rendered_block(
                            rendered,
                            "Codex reasoning",
                            reasoning);

                    } else if (type == "hookPrompt") {
                        std::string fragments;

                        if (
                            item.contains("fragments") &&
                            item["fragments"].is_array()
                        ) {
                            for (
                                const auto& fragment :
                                item["fragments"]
                            ) {
                                if (
                                    !fragment.is_object() ||
                                    !fragment.contains("text") ||
                                    !fragment["text"].is_string()
                                ) {
                                    continue;
                                }

                                if (!fragments.empty()) {
                                    fragments += '\n';
                                }

                                fragments +=
                                    fragment["text"]
                                        .get<std::string>();
                            }
                        }

                        append_rendered_block(
                            rendered,
                            "Hook",
                            fragments);

                    } else {
                        append_rendered_block(
                            rendered,
                            "Activity",
                            "[" +
                            (
                                type.empty()
                                    ? std::string{
                                        "unknown thread item"}
                                    : type
                            ) +
                            "]");
                    }
                }
            }
        }

        if (rendered.empty()) {
            rendered =
                "This thread has no saved turns.";
        }

        const auto buffer =
            transcript_.get_buffer();

        buffer->set_text(rendered);

        auto beginning = buffer->begin();
        transcript_.scroll_to(beginning);
    }

    bool activate_thread(
        const std::string& expected_cwd,
        const std::string& thread_id,
        bool clear_saved_state_on_failure = false
    ) {
        if (
            turn_in_progress_ ||
            thread_id.empty()
        ) {
            return false;
        }

        status_label_.set_text(
            "Codex: resuming thread");

        new_thread_button_.set_sensitive(false);
        send_button_.set_sensitive(false);

        const auto result =
            app_server_.resume_thread(
                thread_id);

        if (!result.success) {
            status_label_.set_text(
                "Codex: thread resume failed");

            std::cerr
                << "FAIL: thread/resume "
                << thread_id
                << ": "
                << result.error
                << '\n';

            if (clear_saved_state_on_failure) {
                current_thread_id_.clear();
                last_active_thread_id_.clear();
                last_active_thread_cwd_.clear();

                current_thread_label_.set_text(
                    "No active thread");

                transcript_.get_buffer()->set_text(
                    "The previously active Codex thread "
                    "could not be resumed.\n\n"
                    "Choose another thread from the sidebar "
                    "or create a new thread.");

                save_ui_state();
                refresh_sidebar_threads();

                std::cout
                    << "PASS: cleared stale saved active-thread "
                    << "state without changing any Codex thread\n";
            }

            new_thread_button_.set_sensitive(
                !selected_folder_path_.empty());

            update_send_button_state();
            return false;
        }

        const std::string resumed_cwd =
            result.cwd.empty()
                ? expected_cwd
                : result.cwd;

        add_project_folder(resumed_cwd);

        current_thread_id_ =
            result.thread_id;

        last_active_thread_id_ =
            result.thread_id;

        last_active_thread_cwd_ =
            resumed_cwd;

        current_thread_label_.set_text(
            "Active thread:\n" +
            display_thread_label(
                result.thread));

        render_thread_transcript(
            result.thread);

        status_label_.set_text(
            "Codex: connected");

        prompt_.grab_focus();
        update_send_button_state();
        save_ui_state();
        refresh_sidebar_threads();

        std::cout
            << "PASS: GTK resumed Codex thread "
            << current_thread_id_
            << " with "
            << result.thread["turns"].size()
            << " turn(s)\n";

        return true;
    }

    void restore_last_active_thread() {
        if (last_active_thread_id_.empty()) {
            return;
        }

        const std::string expected_cwd =
            !last_active_thread_cwd_.empty()
                ? last_active_thread_cwd_
                : selected_folder_path_;

        std::cout
            << "PASS: restoring saved active Codex thread "
            << last_active_thread_id_
            << '\n';

        activate_thread(
            expected_cwd,
            last_active_thread_id_,
            true);
    }

    void initialize_app_server() {
        std::string start_error;

        if (!app_server_.start(start_error)) {
            status_label_.set_text("Codex: failed to start");
            std::cerr << "FAIL: " << start_error << '\n';
            return;
        }

        const auto result = app_server_.initialize(
            "threaddeck",
            "ThreadDeck",
            "0.1.0");

        if (!result.success) {
            status_label_.set_text(
                "Codex: initialization failed");

            std::cerr
                << "FAIL: "
                << result.error
                << '\n';

            app_server_.shutdown();
            return;
        }

        status_label_.set_text("Codex: connected");

        std::cout
            << "PASS: GTK application connected "
            << "to Codex App Server\n";

        new_thread_button_.set_sensitive(
            !selected_folder_path_.empty());

        refresh_sidebar_threads();
        restore_last_active_thread();
    }

    void append_transcript(const std::string& message) {
        const auto buffer = transcript_.get_buffer();
        Glib::ustring current_text = buffer->get_text();

        if (!current_text.empty()) {
            current_text += "\n\n";
        }

        current_text += Glib::ustring(message);
        buffer->set_text(current_text);

        auto end = buffer->end();
        transcript_.scroll_to(end);
    }

    void set_turn_busy(bool busy) {
        turn_in_progress_ = busy;

        folder_button_.set_sensitive(!busy);

        new_thread_button_.set_sensitive(
            !busy &&
            !selected_folder_path_.empty());

        prompt_.set_editable(!busy);
        update_send_button_state();
    }

    void create_thread_for_selected_folder() {
        if (turn_in_progress_) {
            return;
        }

        if (selected_folder_path_.empty()) {
            status_label_.set_text(
                "Codex: select a folder");
            return;
        }

        if (!app_server_.is_running()) {
            status_label_.set_text(
                "Codex: not connected");
            return;
        }

        status_label_.set_text(
            "Codex: creating thread");

        new_thread_button_.set_sensitive(false);
        send_button_.set_sensitive(false);

        const auto result = app_server_.start_thread(
            selected_folder_path_,
            false);

        new_thread_button_.set_sensitive(true);

        if (!result.success) {
            current_thread_id_.clear();

            current_thread_label_.set_text(
                "No active thread");

            status_label_.set_text(
                "Codex: thread creation failed");

            send_button_.set_sensitive(false);

            std::cerr
                << "FAIL: thread/start: "
                << result.error
                << '\n';

            return;
        }

        current_thread_id_ = result.thread_id;
        last_active_thread_id_ = result.thread_id;
        last_active_thread_cwd_ =
            selected_folder_path_;

        current_thread_label_.set_text(
            "Active thread:\n" +
            current_thread_id_);

        status_label_.set_text("Codex: connected");
        prompt_.grab_focus();
        update_send_button_state();

        transcript_.get_buffer()->set_text(
            "Created a persistent Codex thread for:\n" +
            selected_folder_path_ +
            "\n\nThis thread will appear as a saved "
            "thread after Codex materializes its first turn."
            "\n\nThread ID:\n" +
            current_thread_id_);

        save_ui_state();
        refresh_sidebar_threads();

        std::cout
            << "PASS: GTK created Codex thread "
            << current_thread_id_
            << " for "
            << selected_folder_path_
            << '\n';
    }

    void submit_prompt() {
        if (turn_in_progress_) {
            return;
        }

        if (current_thread_id_.empty()) {
            status_label_.set_text(
                "Codex: no active thread");
            return;
        }

        const std::string prompt_text = trim(
            prompt_.get_buffer()->get_text().raw());

        if (prompt_text.empty()) {
            status_label_.set_text(
                "Codex: enter a message");
            return;
        }

        if (turn_worker_.joinable()) {
            turn_worker_.join();
        }

        const std::string thread_id =
            current_thread_id_;

        prompt_.get_buffer()->set_text("");

        append_transcript(
            "You:\n" +
            prompt_text);

        status_label_.set_text("Codex: working");
        set_turn_busy(true);

        std::cout
            << "PASS: GTK submitted a turn for thread "
            << thread_id
            << '\n';

        turn_worker_ = std::thread(
            [this, thread_id, prompt_text]() {
                auto result = app_server_.start_turn(
                    thread_id,
                    prompt_text);

                {
                    std::lock_guard<std::mutex> lock(
                        turn_result_mutex_);

                    pending_turn_result_ =
                        std::move(result);
                }

                turn_dispatcher_.emit();
            });
    }

    void handle_turn_finished() {
        if (turn_worker_.joinable()) {
            turn_worker_.join();
        }

        AppServerClient::TurnResult result;

        {
            std::lock_guard<std::mutex> lock(
                turn_result_mutex_);

            result = std::move(
                pending_turn_result_);
        }

        set_turn_busy(false);

        if (!result.success) {
            status_label_.set_text(
                "Codex: turn transport failed");

            append_transcript(
                "Codex transport error:\n" +
                result.error);

            std::cerr
                << "FAIL: GTK turn transport: "
                << result.error
                << '\n';

            refresh_sidebar_threads();
            return;
        }

        if (result.status == "completed") {
            status_label_.set_text("Codex: connected");

            append_transcript(
                "Codex:\n" +
                (
                    result.streamed_text.empty()
                        ? "(Turn completed without text.)"
                        : result.streamed_text));

        } else if (result.status == "failed") {
            status_label_.set_text(
                "Codex: turn failed");

            append_transcript(
                "Codex error:\n" +
                (
                    result.turn_error.empty()
                        ? "The turn failed without an error message."
                        : result.turn_error));

        } else if (result.status == "interrupted") {
            status_label_.set_text(
                "Codex: turn interrupted");

            append_transcript(
                "Codex turn interrupted.");

        } else {
            status_label_.set_text(
                "Codex: unexpected turn status");

            append_transcript(
                "Codex returned unexpected turn status:\n" +
                result.status);
        }

        save_ui_state();
        refresh_sidebar_threads();

        std::cout
            << "PASS: GTK handled Codex turn "
            << result.turn_id
            << " with status "
            << result.status
            << '\n';
    }

    void select_folder() {
        if (turn_in_progress_) {
            return;
        }

        Gtk::FileChooserDialog dialog(
            "Select project folder",
            Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);

        dialog.set_modal(false);
        dialog.set_skip_taskbar_hint(false);

        dialog.add_button(
            "_Cancel",
            Gtk::RESPONSE_CANCEL);

        dialog.add_button(
            "_Select",
            Gtk::RESPONSE_OK);

        apply_file_chooser_state(dialog);

        const int response = dialog.run();

        capture_file_chooser_state(
            dialog,
            response);

        if (response == Gtk::RESPONSE_OK) {
            add_project_folder(
                dialog.get_filename());

            current_thread_id_.clear();
            last_active_thread_id_.clear();
            last_active_thread_cwd_.clear();

            current_thread_label_.set_text(
                "No active thread");

            transcript_.get_buffer()->set_text(
                "Project folder selected.\n\n"
                "Choose an existing thread from the "
                "sidebar or click New Thread.");

            status_label_.set_text(
                "Codex: connected");

            save_ui_state();
            update_send_button_state();
            refresh_sidebar_threads();

            std::cout
                << "PASS: GTK added project folder "
                << selected_folder_path_
                << " without changing it on disk\n";
        }
    }

    Gtk::HeaderBar header_;
    Gtk::Box root_;
    Gtk::Paned body_;
    Gtk::Box sidebar_;
    Gtk::Box content_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box composer_{Gtk::ORIENTATION_HORIZONTAL};

    Gtk::Button folder_button_;
    Gtk::Button new_thread_button_;
    Gtk::Button send_button_;

    Gtk::Label selected_folder_;
    Gtk::Label status_label_;
    Gtk::Label sidebar_title_;
    Gtk::Label current_thread_label_;

    Gtk::ScrolledWindow sidebar_scroll_;
    Gtk::Box sidebar_list_{
        Gtk::ORIENTATION_VERTICAL};

    Gtk::ScrolledWindow transcript_scroll_;
    Gtk::ScrolledWindow prompt_scroll_;
    Gtk::TextView transcript_;
    Gtk::TextView prompt_;

    std::string selected_folder_path_;
    std::string current_thread_id_;
    std::string last_active_thread_id_;
    std::string last_active_thread_cwd_;
    bool turn_in_progress_{false};

    std::vector<std::string>
        selected_project_folders_;

    std::map<std::string, std::string>
        folder_labels_;

    std::map<std::string, std::string>
        thread_labels_;

    std::string editing_folder_cwd_;
    std::string editing_thread_id_;

    bool chooser_has_geometry_{false};
    int chooser_x_{0};
    int chooser_y_{0};
    int chooser_width_{900};
    int chooser_height_{650};
    int chooser_monitor_{-1};
    std::string chooser_last_folder_;

    bool main_window_has_geometry_{false};
    int main_window_x_{0};
    int main_window_y_{0};
    int main_window_width_{1200};
    int main_window_height_{760};
    int main_window_monitor_{-1};
    bool main_window_maximized_{false};

    AppServerClient app_server_;

    Glib::RefPtr<Gtk::CssProvider> css_provider_;
    Glib::Dispatcher turn_dispatcher_;
    std::thread turn_worker_;
    std::mutex turn_result_mutex_;
    AppServerClient::TurnResult pending_turn_result_;
};

int main(int argc, char* argv[]) {
    const auto application = Gtk::Application::create(
        argc,
        argv,
        "com.ronpatrick.ThreadDeck");

    MainWindow window;
    return application->run(window);
}
