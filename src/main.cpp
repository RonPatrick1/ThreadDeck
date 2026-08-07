#include "app_server_client.h"
#include "context_panel.h"
#include "thread_header.h"

#include <nlohmann/json.hpp>

#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <gdk/gdkkeysyms.h>
#include <gdkmm/screen.h>
#include <glibmm/dispatcher.h>
#include <glibmm/main.h>
#include <gtkmm/aboutdialog.h>
#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/dialog.h>
#include <gtkmm/expander.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <gtkmm/stacksidebar.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class SettingsWindow final : public Gtk::Window {
public:
    explicit SettingsWindow(
        Gtk::ComboBoxText& theme_selector
    )
        : root_(Gtk::ORIENTATION_HORIZONTAL),
          appearance_page_(Gtk::ORIENTATION_VERTICAL),
          theme_selector_(theme_selector),
          appearance_title_("Appearance"),
          theme_label_("Theme"),
          theme_description_(
              "Choose whether ThreadDeck follows the current "
              "GTK system appearance or uses a controlled "
              "built-in palette.") {
        set_title("ThreadDeck Settings");
        set_default_size(620, 420);
        set_modal(false);

        root_.get_style_context()->add_class(
            "settings-root");

        category_sidebar_.get_style_context()->add_class(
            "settings-sidebar");
        category_sidebar_.set_size_request(170, -1);
        category_sidebar_.set_stack(settings_stack_);

        settings_stack_.get_style_context()->add_class(
            "settings-page");
        settings_stack_.set_hexpand(true);
        settings_stack_.set_vexpand(true);
        settings_stack_.set_transition_type(
            Gtk::STACK_TRANSITION_TYPE_CROSSFADE);
        settings_stack_.set_transition_duration(150);

        appearance_page_.get_style_context()->add_class(
            "settings-page");
        appearance_page_.set_border_width(22);
        appearance_page_.set_spacing(14);

        appearance_title_.set_xalign(0.0F);
        appearance_title_.set_markup(
            "<span size=\"x-large\" weight=\"bold\">"
            "Appearance"
            "</span>");

        theme_label_.set_xalign(0.0F);
        theme_label_.set_markup(
            "<b>Theme</b>");

        theme_description_.set_xalign(0.0F);
        theme_description_.set_line_wrap(true);
        theme_description_.set_max_width_chars(54);

        theme_selector_.set_hexpand(false);
        theme_selector_.set_halign(Gtk::ALIGN_START);

        appearance_page_.pack_start(
            appearance_title_,
            Gtk::PACK_SHRINK);
        appearance_page_.pack_start(
            theme_label_,
            Gtk::PACK_SHRINK);
        appearance_page_.pack_start(
            theme_description_,
            Gtk::PACK_SHRINK);
        appearance_page_.pack_start(
            theme_selector_,
            Gtk::PACK_SHRINK);

        settings_stack_.add(
            appearance_page_,
            "appearance",
            "Appearance");

        root_.pack_start(
            category_sidebar_,
            Gtk::PACK_SHRINK);
        root_.pack_start(
            settings_stack_,
            Gtk::PACK_EXPAND_WIDGET);

        add(root_);

        signal_delete_event().connect(
            sigc::mem_fun(
                *this,
                &SettingsWindow::handle_delete),
            false);

        show_all_children();
    }

    void present_for(Gtk::Window& parent) {
        set_transient_for(parent);
        set_position(Gtk::WIN_POS_CENTER_ON_PARENT);
        show_all();
        present();
    }

private:
    bool handle_delete(GdkEventAny*) {
        hide();
        return true;
    }

    Gtk::Box root_;
    Gtk::StackSidebar category_sidebar_;
    Gtk::Stack settings_stack_;
    Gtk::Box appearance_page_;

    Gtk::ComboBoxText& theme_selector_;

    Gtk::Label appearance_title_;
    Gtk::Label theme_label_;
    Gtk::Label theme_description_;
};

class MainWindow final : public Gtk::ApplicationWindow {
public:
    MainWindow()
        : root_(Gtk::ORIENTATION_VERTICAL),
          body_(Gtk::ORIENTATION_HORIZONTAL),
          workspace_(Gtk::ORIENTATION_HORIZONTAL),
          sidebar_(Gtk::ORIENTATION_VERTICAL),
          send_button_(),
          selected_folder_("No folder selected"),
          status_label_("Codex: starting"),
          sidebar_title_("Threads") {
        set_title("ThreadDeck");
        set_default_size(1200, 760);

        header_.set_title("ThreadDeck");
        header_.set_show_close_button(true);
        set_titlebar(header_);

        add(root_);

        folder_button_.signal_clicked().connect(
            sigc::mem_fun(*this, &MainWindow::select_folder));

        new_thread_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::create_thread_for_selected_folder));

        sidebar_toggle_button_.signal_toggled().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_sidebar_toggled));

        context_toggle_button_.set_tooltip_text(
            "Show or hide the contextual inspector");
        context_toggle_button_.get_style_context()->add_class(
            "context-toggle-button");

        folder_button_.set_tooltip_text(
            "Open project folder");
        new_thread_button_.set_tooltip_text(
            "Create a new thread");

        send_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_send_or_stop));

        turn_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_turn_finished));

        turn_event_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_turn_events));

        approval_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_approval_request));

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

        send_image_.set_from_icon_name(
            "mail-send-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        send_button_.set_image(send_image_);
        send_button_.set_always_show_image(true);
        send_button_.set_relief(Gtk::RELIEF_NONE);
        send_button_.set_size_request(42, 42);
        send_button_.set_valign(Gtk::ALIGN_END);
        send_button_.set_tooltip_text("Send message (Enter)");

        theme_selector_.append(
            "system",
            "System");
        theme_selector_.append(
            "neutral-light",
            "Neutral Light");
        theme_selector_.append(
            "neutral-dark",
            "Neutral Dark");
        theme_selector_.append(
            "winter-frost",
            "Winter Frost");
        theme_selector_.append(
            "spring-moss",
            "Spring Moss");
        theme_selector_.append(
            "summer-coast",
            "Summer Coast");
        theme_selector_.append(
            "autumn-ember",
            "Autumn Ember");
        theme_selector_.append(
            "midnight-ocean",
            "Midnight Ocean");
        theme_selector_.append(
            "forest-rain",
            "Forest Rain");
        theme_selector_.append(
            "lavender-calm",
            "Lavender Calm");
        theme_selector_.append(
            "storm-slate",
            "Storm Slate");
        theme_selector_.set_tooltip_text(
            "ThreadDeck appearance");
        theme_selector_.get_style_context()->add_class(
            "theme-selector");

        app_menu_model_ = Gio::Menu::create();
        app_menu_model_->append(
            "Settings",
            "app.settings");
        app_menu_model_->append(
            "About ThreadDeck",
            "app.about");
        app_menu_model_->append(
            "Quit",
            "app.quit");

        hamburger_image_.set_from_icon_name(
            "open-menu-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        sidebar_image_.set_from_icon_name(
            "sidebar-hide-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        folder_image_.set_from_icon_name(
            "folder-open-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        new_thread_image_.set_from_icon_name(
            "document-new-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        context_image_.set_from_icon_name(
            "view-sidebar-symbolic",
            Gtk::ICON_SIZE_BUTTON);

        hamburger_button_.set_image(
            hamburger_image_);
        sidebar_toggle_button_.set_image(
            sidebar_image_);
        folder_button_.set_image(
            folder_image_);
        new_thread_button_.set_image(
            new_thread_image_);
        context_toggle_button_.set_image(
            context_image_);

        hamburger_button_.set_always_show_image(true);
        sidebar_toggle_button_.set_always_show_image(true);
        folder_button_.set_always_show_image(true);
        new_thread_button_.set_always_show_image(true);
        context_toggle_button_.set_always_show_image(true);

        hamburger_button_.set_tooltip_text(
            "ThreadDeck menu");
        hamburger_button_.property_use_popover() =
            false;
        hamburger_button_.set_menu_model(
            app_menu_model_);

        for (
            Gtk::Button* button :
            {
                static_cast<Gtk::Button*>(
                    &hamburger_button_),
                static_cast<Gtk::Button*>(
                    &sidebar_toggle_button_),
                &folder_button_,
                &new_thread_button_,
                static_cast<Gtk::Button*>(
                    &context_toggle_button_),
            }
        ) {
            button->set_relief(
                Gtk::RELIEF_NONE);
            button->set_size_request(
                36,
                36);
            button->get_style_context()->add_class(
                "compact-header-button");
        }

        hamburger_button_.get_style_context()->add_class(
            "app-menu-button");

        header_.pack_start(hamburger_button_);
        header_.pack_start(sidebar_toggle_button_);
        header_.pack_start(folder_button_);
        header_.pack_start(new_thread_button_);
        header_.pack_end(context_toggle_button_);

        selected_folder_.set_xalign(0.0F);
        selected_folder_.set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
        root_.pack_start(selected_folder_, Gtk::PACK_SHRINK);

        status_label_.set_xalign(0.0F);
        root_.pack_start(status_label_, Gtk::PACK_SHRINK);

        header_.get_style_context()->add_class(
            "threaddeck-header");
        selected_folder_.get_style_context()->add_class(
            "context-strip");
        status_label_.get_style_context()->add_class(
            "context-strip");
        sidebar_.get_style_context()->add_class(
            "threaddeck-sidebar");
        content_.get_style_context()->add_class(
            "threaddeck-content");
        transcript_scroll_.set_name(
            "transcript-scroll");
        transcript_.set_name(
            "transcript-view");

        sidebar_title_.set_xalign(0.0F);
        sidebar_.set_border_width(12);
        sidebar_.set_spacing(8);
        sidebar_.pack_start(sidebar_title_, Gtk::PACK_SHRINK);

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
        prompt_.set_sensitive(true);
        prompt_.set_editable(true);
        prompt_.set_cursor_visible(true);
        prompt_.set_can_focus(true);
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
            thread_header_,
            Gtk::PACK_SHRINK);
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

        workspace_.pack1(content_, true, false);
        workspace_.pack2(context_panel_, false, false);
        workspace_.set_position(620);

        body_.pack1(sidebar_, false, false);
        body_.pack2(workspace_, true, false);
        body_.set_position(260);

        root_.pack_start(body_, Gtk::PACK_EXPAND_WIDGET);

        load_ui_state();

        theme_selector_.set_active_id(
            theme_id_);
        theme_selector_.signal_changed().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_theme_changed));

        sidebar_toggle_button_.set_active(
            sidebar_visible_);

        context_toggle_button_.set_active(
            context_panel_visible_);
        context_toggle_button_.signal_toggled().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_context_panel_toggled));

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

        configure_styles();
        apply_theme(false);
        update_prompt_height();
        initialize_app_server();
        show_all_children();
        apply_sidebar_visibility(false);
        apply_context_panel_visibility(false);
    }

    ~MainWindow() override {
        {
            std::lock_guard<std::mutex> lock(
                approval_mutex_);

            if (
                approval_waiting_ &&
                !approval_resolved_
            ) {
                approval_decision_ = "cancel";
                approval_resolved_ = true;
            }
        }

        approval_condition_.notify_all();

        if (turn_worker_.joinable()) {
            turn_worker_.join();
        }
    }

private:
public:
    void show_settings() {
        settings_window_.present_for(*this);
    }

    void show_about() {
        Gtk::AboutDialog dialog;

        dialog.set_transient_for(*this);
        dialog.set_modal(true);
        dialog.set_program_name("ThreadDeck");
        dialog.set_version("0.1.0");
        dialog.set_comments(
            "A native desktop client for Codex on Linux.");
        dialog.set_copyright(
            "Copyright © 2026 Ron Patrick");

        dialog.run();
    }

private:
    std::string folder_name_from_path(
        const std::string& path
    ) const {
        if (path.empty()) {
            return "No folder";
        }

        const auto normalized =
            std::filesystem::path(path).lexically_normal();

        const std::string name =
            normalized.filename().string();

        return name.empty()
            ? path
            : name;
    }

    std::string project_display_name(
        const std::string& cwd
    ) const {
        const auto label =
            folder_labels_.find(cwd);

        if (
            label != folder_labels_.end() &&
            !label->second.empty()
        ) {
            return label->second;
        }

        return folder_name_from_path(cwd);
    }

    void clear_active_thread_surfaces() {
        current_thread_default_label_.clear();
        current_thread_data_ =
            nlohmann::json::object();
        current_thread_turn_failed_ = false;

        thread_header_.clear();
        context_panel_.clear();
    }

    void refresh_active_thread_surfaces_from_labels() {
        if (current_thread_id_.empty()) {
            clear_active_thread_surfaces();
            return;
        }

        std::string title =
            current_thread_default_label_;

        if (
            current_thread_data_.is_object() &&
            !current_thread_data_.empty()
        ) {
            title =
                display_thread_label(
                    current_thread_data_);
        } else {
            const auto custom_label =
                thread_labels_.find(
                    current_thread_id_);

            if (
                custom_label != thread_labels_.end() &&
                !custom_label->second.empty()
            ) {
                title = custom_label->second;
            }
        }

        if (title.empty()) {
            title = "New Thread";
        }

        const std::string cwd =
            !last_active_thread_cwd_.empty()
                ? last_active_thread_cwd_
                : selected_folder_path_;

        thread_header_.set_thread(
            title,
            cwd);

        context_panel_.set_details(
            title,
            project_display_name(cwd),
            folder_name_from_path(cwd),
            cwd,
            current_thread_id_);
    }

    void set_active_thread_surfaces(
        const std::string& default_label,
        const std::string& cwd,
        const std::string& thread_id,
        const nlohmann::json& thread_data =
            nlohmann::json::object()
    ) {
        current_thread_default_label_ =
            default_label;
        current_thread_data_ =
            thread_data;
        last_active_thread_cwd_ =
            cwd;
        current_thread_id_ =
            thread_id;

        refresh_active_thread_surfaces_from_labels();
    }

    void update_sidebar_toggle_visuals() {
        sidebar_image_.set_from_icon_name(
            sidebar_visible_
                ? "sidebar-hide-symbolic"
                : "sidebar-show-symbolic",
            Gtk::ICON_SIZE_BUTTON);

        sidebar_toggle_button_.set_tooltip_text(
            sidebar_visible_
                ? "Hide projects and threads"
                : "Show projects and threads");
    }

    void apply_sidebar_visibility(
        bool persist
    ) {
        if (sidebar_visible_) {
            sidebar_.show();

            int restored_width =
                std::clamp(
                    sidebar_width_,
                    180,
                    600);

            const int allocated_width =
                body_.get_allocated_width();

            if (allocated_width > 540) {
                restored_width =
                    std::min(
                        restored_width,
                        allocated_width - 360);
            }

            body_.set_position(
                std::max(
                    180,
                    restored_width));
        } else {
            sidebar_.hide();
            body_.set_position(0);
        }

        update_sidebar_toggle_visuals();

        if (persist) {
            save_ui_state();
        }
    }

    void handle_sidebar_toggled() {
        if (
            sidebar_visible_ &&
            !sidebar_toggle_button_.get_active()
        ) {
            const int divider_position =
                body_.get_position();

            if (divider_position > 0) {
                sidebar_width_ =
                    std::clamp(
                        divider_position,
                        180,
                        600);
            }
        }

        sidebar_visible_ =
            sidebar_toggle_button_.get_active();

        apply_sidebar_visibility(true);
    }

    void apply_context_panel_visibility(
        bool persist
    ) {
        if (context_panel_visible_) {
            context_panel_.show();

            const int allocated_width =
                workspace_.get_allocated_width();

            if (allocated_width > 560) {
                workspace_.set_position(
                    allocated_width -
                    std::clamp(
                        context_panel_width_,
                        240,
                        600));
            }
        } else {
            context_panel_.hide();
        }

        if (persist) {
            save_ui_state();
        }
    }

    void handle_context_panel_toggled() {
        if (
            context_panel_visible_ &&
            !context_toggle_button_.get_active()
        ) {
            const int allocated_width =
                workspace_.get_allocated_width();
            const int divider_position =
                workspace_.get_position();

            if (
                allocated_width > divider_position &&
                divider_position > 0
            ) {
                context_panel_width_ =
                    std::clamp(
                        allocated_width -
                            divider_position,
                        240,
                        600);
            }
        }

        context_panel_visible_ =
            context_toggle_button_.get_active();

        apply_context_panel_visibility(true);
    }

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

            theme_id_ =
                state.value(
                    "theme",
                    std::string{"system"});

            sidebar_visible_ =
                state.value(
                    "sidebarVisible",
                    true);

            sidebar_width_ =
                std::clamp(
                    state.value(
                        "sidebarWidth",
                        260),
                    180,
                    600);

            context_panel_visible_ =
                state.value(
                    "contextPanelVisible",
                    true);

            context_panel_width_ =
                std::clamp(
                    state.value(
                        "contextPanelWidth",
                        320),
                    240,
                    600);

            if (!is_known_theme_id(theme_id_)) {
                theme_id_ = "system";
            }

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

            int saved_sidebar_width =
                sidebar_width_;

            if (sidebar_visible_) {
                const int divider_position =
                    body_.get_position();

                if (divider_position > 0) {
                    saved_sidebar_width =
                        std::clamp(
                            divider_position,
                            180,
                            600);
                }
            }

            int saved_context_panel_width =
                context_panel_width_;

            if (context_panel_visible_) {
                const int allocated_width =
                    workspace_.get_allocated_width();
                const int divider_position =
                    workspace_.get_position();

                if (
                    allocated_width > divider_position &&
                    divider_position > 0
                ) {
                    saved_context_panel_width =
                        std::clamp(
                            allocated_width -
                                divider_position,
                            240,
                            600);
                }
            }

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
                    "theme",
                    theme_id_,
                },
                {
                    "sidebarVisible",
                    sidebar_visible_,
                },
                {
                    "sidebarWidth",
                    saved_sidebar_width,
                },
                {
                    "contextPanelVisible",
                    context_panel_visible_,
                },
                {
                    "contextPanelWidth",
                    saved_context_panel_width,
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

    void configure_styles() {
        structural_css_provider_ =
            Gtk::CssProvider::create();

        structural_css_provider_->load_from_data(R"CSS(
headerbar.threaddeck-header {
    border-bottom: 1px solid alpha(@theme_fg_color, 0.16);
}

.thread-header {
    background-color: @theme_bg_color;
    border-bottom: 1px solid alpha(@theme_fg_color, 0.14);
    padding: 10px 12px;
}

.thread-header-title {
    font-size: 18px;
    font-weight: bold;
}

.thread-folder-chip {
    background-color: alpha(@theme_fg_color, 0.08);
    border-radius: 8px;
    padding: 4px 9px;
}

.context-panel {
    background-color: shade(@theme_bg_color, 0.97);
    border-left: 1px solid alpha(@theme_fg_color, 0.14);
}

.context-panel-title {
    font-size: 17px;
    font-weight: bold;
}

.details-key {
    opacity: 0.70;
    font-weight: bold;
}

.details-value {
    padding-bottom: 7px;
}

.context-toggle-button:checked {
    background-color: alpha(@theme_selected_bg_color, 0.22);
}

.compact-header-button {
    min-width: 36px;
    min-height: 36px;
    padding: 0;
    border-radius: 7px;
}

.compact-header-button:hover {
    background-color: alpha(@theme_fg_color, 0.08);
}

.compact-header-button:active,
.compact-header-button:checked {
    background-color: alpha(@theme_selected_bg_color, 0.22);
}

.settings-root {
    background-color: @theme_bg_color;
}

.settings-sidebar {
    background-color: shade(@theme_bg_color, 0.96);
    border-right: 1px solid alpha(@theme_fg_color, 0.14);
}

.settings-page {
    background-color: @theme_bg_color;
}

.context-strip {
    background-color: shade(@theme_bg_color, 0.98);
    border-bottom: 1px solid alpha(@theme_fg_color, 0.10);
    padding: 4px 10px;
}

.threaddeck-sidebar {
    background-color: shade(@theme_bg_color, 0.96);
    border-right: 1px solid alpha(@theme_fg_color, 0.14);
}

.threaddeck-content {
    background-color: @theme_bg_color;
}

#transcript-scroll,
#transcript-view,
#transcript-view text {
    background-color: @theme_base_color;
    color: @theme_text_color;
    border: none;
    box-shadow: none;
}

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
    color: @theme_text_color;
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

.approval-dialog {
    background-color: @theme_bg_color;
}

.approval-shell {
    padding: 2px;
}

.approval-eyebrow {
    font-size: 11px;
    font-weight: bold;
    opacity: 0.66;
}

.approval-title {
    font-size: 22px;
    font-weight: bold;
}

.approval-summary,
.approval-footnote {
    opacity: 0.76;
}

.approval-card {
    background-color: alpha(@theme_fg_color, 0.055);
    border: 1px solid alpha(@theme_fg_color, 0.16);
    border-radius: 12px;
    padding: 14px 16px;
}

.approval-card-label,
.approval-meta-key {
    font-size: 11px;
    font-weight: bold;
    opacity: 0.66;
}

.approval-code {
    font-family: monospace;
}

.approval-meta {
    padding: 2px 2px;
}

.approval-details {
    border-top: 1px solid alpha(@theme_fg_color, 0.12);
    padding-top: 8px;
}

#approval-details-view,
#approval-details-view text {
    background-color: alpha(@theme_fg_color, 0.045);
    color: @theme_text_color;
    border: none;
    box-shadow: none;
    font-family: monospace;
}

button.approval-primary-button {
    background-image: none;
    background-color: @theme_selected_bg_color;
    color: @theme_selected_fg_color;
    border-radius: 8px;
    font-weight: bold;
    padding: 8px 16px;
}

button.approval-session-button {
    background-image: none;
    background-color: alpha(@theme_selected_bg_color, 0.18);
    border-color: alpha(@theme_selected_bg_color, 0.46);
    border-radius: 8px;
    padding: 8px 16px;
}

button.approval-neutral-button,
button.approval-danger-button {
    border-radius: 8px;
    padding: 8px 14px;
}

.theme-selector {
    min-width: 128px;
}

.folder-heading {
    font-weight: bold;
    margin-top: 10px;
    margin-bottom: 2px;
}

.thread-row {
    padding: 5px 7px;
    border-radius: 6px;
}

.thread-row:hover {
    background-color: alpha(@theme_fg_color, 0.07);
}

.thread-row.active-thread {
    background-color: alpha(@theme_selected_bg_color, 0.22);
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

        const auto screen =
            Gdk::Screen::get_default();

        if (screen) {
            Gtk::StyleContext::add_provider_for_screen(
                screen,
                structural_css_provider_,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }

        send_button_.get_style_context()->add_class(
            "send-button");
    }

    struct ThemePalette {
        const char* id;
        const char* accent_color;
        const char* accent_bg_color;
        const char* accent_fg_color;
        const char* window_bg_color;
        const char* view_bg_color;
        const char* headerbar_bg_color;
        const char* sidebar_bg_color;
        const char* card_bg_color;
        const char* popover_bg_color;
        const char* dialog_bg_color;
        const char* foreground_color;
        const char* shade_color;
        const char* scrollbar_outline_color;
    };

    const ThemePalette* find_theme_palette(
        const std::string& theme_id
    ) const {
        static const std::array<
            ThemePalette,
            10
        > palettes{{
            ThemePalette{
                "neutral-light",
                "#3f6f98",
                "#4b82b1",
                "#ffffff",
                "#f3f4f5",
                "#ffffff",
                "#e7e9eb",
                "#eceeef",
                "#ffffff",
                "#ffffff",
                "#f3f4f5",
                "#202327",
                "rgba(24, 28, 32, 0.14)",
                "rgba(24, 28, 32, 0.24)",
            },
            ThemePalette{
                "neutral-dark",
                "#83b6e4",
                "#3f78a8",
                "#ffffff",
                "#202226",
                "#181a1d",
                "#292c31",
                "#24272b",
                "#2d3036",
                "#2d3036",
                "#202226",
                "#f1f3f5",
                "rgba(0, 0, 0, 0.48)",
                "rgba(255, 255, 255, 0.18)",
            },
            ThemePalette{
                "winter-frost",
                "#547f99",
                "#5d8eaa",
                "#ffffff",
                "#edf3f7",
                "#f9fcfd",
                "#dfeaf1",
                "#e6eef3",
                "#ffffff",
                "#ffffff",
                "#edf3f7",
                "#263743",
                "rgba(47, 78, 97, 0.16)",
                "rgba(47, 78, 97, 0.26)",
            },
            ThemePalette{
                "spring-moss",
                "#607c57",
                "#6f8f63",
                "#ffffff",
                "#eef2ea",
                "#fafcf7",
                "#e1e8dc",
                "#e7ece2",
                "#fcfdfb",
                "#fcfdfb",
                "#eef2ea",
                "#293429",
                "rgba(50, 72, 48, 0.16)",
                "rgba(50, 72, 48, 0.26)",
            },
            ThemePalette{
                "summer-coast",
                "#367679",
                "#42898d",
                "#ffffff",
                "#eaf4f4",
                "#f9fdfd",
                "#dcecec",
                "#e3f0f0",
                "#ffffff",
                "#ffffff",
                "#eaf4f4",
                "#203637",
                "rgba(41, 85, 87, 0.16)",
                "rgba(41, 85, 87, 0.26)",
            },
            ThemePalette{
                "autumn-ember",
                "#e0a06f",
                "#a85f34",
                "#ffffff",
                "#2a221e",
                "#211b18",
                "#352a24",
                "#302721",
                "#3a2e27",
                "#3a2e27",
                "#2a221e",
                "#f3e9e2",
                "rgba(0, 0, 0, 0.50)",
                "rgba(243, 233, 226, 0.18)",
            },
            ThemePalette{
                "midnight-ocean",
                "#82b4e0",
                "#3b73a5",
                "#ffffff",
                "#171d29",
                "#111722",
                "#202839",
                "#1c2433",
                "#263045",
                "#263045",
                "#171d29",
                "#eef4fb",
                "rgba(0, 0, 0, 0.52)",
                "rgba(238, 244, 251, 0.18)",
            },
            ThemePalette{
                "forest-rain",
                "#88b99d",
                "#4f7d63",
                "#ffffff",
                "#18231f",
                "#111a17",
                "#213029",
                "#1d2a25",
                "#27382f",
                "#27382f",
                "#18231f",
                "#edf5f0",
                "rgba(0, 0, 0, 0.50)",
                "rgba(237, 245, 240, 0.18)",
            },
            ThemePalette{
                "lavender-calm",
                "#725f98",
                "#806da5",
                "#ffffff",
                "#f1eef6",
                "#fbf9fd",
                "#e6e0ef",
                "#ebe6f2",
                "#ffffff",
                "#ffffff",
                "#f1eef6",
                "#332d3e",
                "rgba(71, 55, 94, 0.16)",
                "rgba(71, 55, 94, 0.26)",
            },
            ThemePalette{
                "storm-slate",
                "#9bb1c8",
                "#617c98",
                "#ffffff",
                "#23272e",
                "#1a1e24",
                "#2d323b",
                "#282d35",
                "#343a44",
                "#343a44",
                "#23272e",
                "#eef1f5",
                "rgba(0, 0, 0, 0.48)",
                "rgba(238, 241, 245, 0.18)",
            }
        }};

        for (const auto& palette : palettes) {
            if (theme_id == palette.id) {
                return &palette;
            }
        }

        return nullptr;
    }

    bool is_known_theme_id(
        const std::string& theme_id
    ) const {
        return (
            theme_id == "system" ||
            find_theme_palette(theme_id) != nullptr
        );
    }

    std::string theme_css(
        const std::string& theme_id
    ) const {
        const ThemePalette* palette =
            find_theme_palette(theme_id);

        if (!palette) {
            return {};
        }

        std::ostringstream css;

        css
            << "window {\n"
            << "    background-color: "
            << palette->window_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << "headerbar.threaddeck-header {\n"
            << "    background-image: none;\n"
            << "    background-color: "
            << palette->headerbar_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-color: "
            << palette->shade_color
            << ";\n"
            << "}\n\n"

            << ".context-strip {\n"
            << "    background-color: "
            << palette->window_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-color: "
            << palette->shade_color
            << ";\n"
            << "}\n\n"

            << ".threaddeck-sidebar,\n"
            << ".settings-sidebar {\n"
            << "    background-color: "
            << palette->sidebar_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-color: "
            << palette->shade_color
            << ";\n"
            << "}\n\n"

            << ".threaddeck-content,\n"
            << ".settings-root,\n"
            << ".settings-page,\n"
            << ".thread-header {\n"
            << "    background-color: "
            << palette->window_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << "#transcript-scroll,\n"
            << "#transcript-view,\n"
            << "#transcript-view text {\n"
            << "    background-color: "
            << palette->view_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << ".composer {\n"
            << "    background-color: "
            << palette->card_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-color: "
            << palette->scrollbar_outline_color
            << ";\n"
            << "}\n\n"

            << "#prompt-scroll,\n"
            << "#prompt-input,\n"
            << "#prompt-input text {\n"
            << "    background-color: transparent;\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << ".context-panel {\n"
            << "    background-color: "
            << palette->sidebar_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-color: "
            << palette->shade_color
            << ";\n"
            << "}\n\n"

            << ".thread-folder-chip {\n"
            << "    background-color: "
            << palette->card_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << "menu,\n"
            << "popover,\n"
            << "dialog {\n"
            << "    background-color: "
            << palette->popover_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << "dialog {\n"
            << "    background-color: "
            << palette->dialog_bg_color
            << ";\n"
            << "}\n\n"

            << "button,\n"
            << "combobox button {\n"
            << "    background-image: none;\n"
            << "    background-color: "
            << palette->card_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-color: "
            << palette->scrollbar_outline_color
            << ";\n"
            << "}\n\n"

            << "button:hover,\n"
            << "combobox button:hover,\n"
            << "menuitem:hover {\n"
            << "    background-color: alpha("
            << palette->accent_bg_color
            << ", 0.20);\n"
            << "}\n\n"

            << "button:active,\n"
            << "button:checked,\n"
            << "combobox button:active {\n"
            << "    background-color: alpha("
            << palette->accent_bg_color
            << ", 0.32);\n"
            << "}\n\n"

            << ".thread-row:hover {\n"
            << "    background-color: alpha("
            << palette->accent_bg_color
            << ", 0.18);\n"
            << "}\n\n"

            << ".thread-row.active-thread {\n"
            << "    background-color: alpha("
            << palette->accent_bg_color
            << ", 0.34);\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << ".send-button {\n"
            << "    background-color: "
            << palette->accent_bg_color
            << ";\n"
            << "    color: "
            << palette->accent_fg_color
            << ";\n"
            << "}\n\n"

            << ".send-button:hover {\n"
            << "    background-color: "
            << palette->accent_color
            << ";\n"
            << "}\n\n"

            << "button.approval-primary-button {\n"
            << "    background-color: "
            << palette->accent_bg_color
            << ";\n"
            << "    color: "
            << palette->accent_fg_color
            << ";\n"
            << "    border-color: "
            << palette->accent_bg_color
            << ";\n"
            << "}\n\n"

            << "button.approval-primary-button:hover {\n"
            << "    background-color: "
            << palette->accent_color
            << ";\n"
            << "}\n\n"

            << "button.approval-session-button {\n"
            << "    background-color: alpha("
            << palette->accent_bg_color
            << ", 0.18);\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-color: alpha("
            << palette->accent_bg_color
            << ", 0.52);\n"
            << "}\n\n"

            << "button.approval-session-button:hover {\n"
            << "    background-color: alpha("
            << palette->accent_bg_color
            << ", 0.30);\n"
            << "}\n\n"

            << "#approval-details-view,\n"
            << "#approval-details-view text {\n"
            << "    background-color: "
            << palette->card_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "}\n\n"

            << "selection {\n"
            << "    background-color: "
            << palette->accent_bg_color
            << ";\n"
            << "    color: "
            << palette->accent_fg_color
            << ";\n"
            << "}\n\n"

            << "scrollbar slider {\n"
            << "    background-color: alpha("
            << palette->foreground_color
            << ", 0.28);\n"
            << "}\n\n"

            << "scrollbar trough {\n"
            << "    border-color: "
            << palette->scrollbar_outline_color
            << ";\n"
            << "}\n";

        return css.str();
    }

    void apply_theme(bool persist_selection) {
        const auto screen =
            Gdk::Screen::get_default();

        if (
            screen &&
            theme_css_provider_
        ) {
            Gtk::StyleContext::remove_provider_for_screen(
                screen,
                theme_css_provider_);

            theme_css_provider_ =
                Glib::RefPtr<Gtk::CssProvider>();
        }

        const std::string css =
            theme_css(theme_id_);

        if (
            screen &&
            !css.empty()
        ) {
            theme_css_provider_ =
                Gtk::CssProvider::create();

            theme_css_provider_->load_from_data(
                css);

            Gtk::StyleContext::add_provider_for_screen(
                screen,
                theme_css_provider_,
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
        }

        queue_draw();

        if (persist_selection) {
            save_ui_state();
        }

        std::cout
            << "PASS: applied ThreadDeck theme "
            << theme_id_
            << (
                css.empty()
                    ? " using the current GTK system palette"
                    : " using a controlled built-in palette"
            )
            << '\n';
    }

    void handle_theme_changed() {
        const std::string selected =
            theme_selector_.get_active_id().raw();

        if (
            selected.empty() ||
            selected == theme_id_
        ) {
            return;
        }

        if (!is_known_theme_id(selected)) {
            std::cerr
                << "FAIL: rejected unknown ThreadDeck theme "
                << selected
                << '\n';
            return;
        }

        theme_id_ = selected;
        apply_theme(true);
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
        if (turn_in_progress_) {
            send_button_.set_sensitive(
                !active_turn_id_.empty() &&
                !stop_requested_);
            return;
        }

        const std::string prompt_text = trim(
            prompt_.get_buffer()->get_text().raw());

        send_button_.set_sensitive(
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
        refresh_active_thread_surfaces_from_labels();

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
                const std::string pending_label =
                    current_thread_turn_failed_
                        ? "● Current thread - turn failed "
                          "(not saved)"
                        : "● Current thread "
                          "(not saved yet)";

                auto* pending_button =
                    Gtk::manage(
                        new Gtk::Button(
                            pending_label));

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

    static std::string activity_label(
        const std::string& type
    ) {
        if (type == "commandExecution") {
            return "Command execution";
        }
        if (type == "fileChange") {
            return "File change";
        }
        if (type == "mcpToolCall") {
            return "MCP tool call";
        }
        if (type == "dynamicToolCall") {
            return "Tool call";
        }
        if (type == "collabAgentToolCall") {
            return "Collaboration tool call";
        }
        if (type == "subAgentActivity") {
            return "Sub-agent activity";
        }
        if (type == "webSearch") {
            return "Web search";
        }
        if (type == "imageView") {
            return "Image view";
        }
        if (type == "sleep") {
            return "Waiting";
        }
        if (type == "imageGeneration") {
            return "Image generation";
        }
        if (type == "contextCompaction") {
            return "Context compaction";
        }

        return type.empty()
            ? "Codex activity"
            : type;
    }

    static bool is_activity_type(
        const std::string& type
    ) {
        return
            type == "commandExecution" ||
            type == "fileChange" ||
            type == "mcpToolCall" ||
            type == "dynamicToolCall" ||
            type == "collabAgentToolCall" ||
            type == "subAgentActivity" ||
            type == "webSearch" ||
            type == "imageView" ||
            type == "sleep" ||
            type == "imageGeneration" ||
            type == "contextCompaction";
    }

    static std::string activity_state(
        const nlohmann::json& item,
        const std::string& turn_status,
        const std::string& fallback
    ) {
        const std::string status =
            item.value("status", std::string{});

        if (status == "inProgress") {
            if (turn_status == "failed") {
                return "failed";
            }

            if (turn_status == "interrupted") {
                return "interrupted";
            }

            return "running";
        }

        if (!status.empty()) {
            return status;
        }

        if (
            turn_status == "failed" ||
            turn_status == "interrupted"
        ) {
            return turn_status;
        }

        if (
            item.value("type", std::string{}) ==
            "subAgentActivity"
        ) {
            const std::string kind =
                item.value("kind", std::string{});

            if (!kind.empty()) {
                return kind;
            }
        }

        return fallback;
    }

    static std::string activity_detail(
        const nlohmann::json& item
    ) {
        const std::string type =
            item.value("type", std::string{});

        if (type == "commandExecution") {
            return item.value(
                "command",
                std::string{});
        }

        if (type == "fileChange") {
            std::string paths;

            const auto changes =
                item.value(
                    "changes",
                    nlohmann::json::array());

            if (changes.is_array()) {
                for (const auto& change : changes) {
                    if (!change.is_object()) {
                        continue;
                    }

                    const std::string path =
                        change.value(
                            "path",
                            std::string{});

                    if (path.empty()) {
                        continue;
                    }

                    if (!paths.empty()) {
                        paths += ", ";
                    }

                    paths += path;
                }
            }

            return paths;
        }

        if (type == "mcpToolCall") {
            const std::string server =
                item.value(
                    "server",
                    std::string{});

            const std::string tool =
                item.value(
                    "tool",
                    std::string{});

            if (server.empty()) {
                return tool;
            }

            if (tool.empty()) {
                return server;
            }

            return server + "/" + tool;
        }

        if (type == "dynamicToolCall") {
            std::string name_space;

            if (
                item.contains("namespace") &&
                item["namespace"].is_string()
            ) {
                name_space =
                    item["namespace"]
                        .get<std::string>();
            }

            const std::string tool =
                item.value(
                    "tool",
                    std::string{});

            if (name_space.empty()) {
                return tool;
            }

            if (tool.empty()) {
                return name_space;
            }

            return name_space + "::" + tool;
        }

        if (type == "collabAgentToolCall") {
            return item.value(
                "tool",
                std::string{});
        }

        if (type == "subAgentActivity") {
            return item.value(
                "agentPath",
                std::string{});
        }

        if (type == "webSearch") {
            return item.value(
                "query",
                std::string{});
        }

        if (type == "imageView") {
            return item.value(
                "path",
                std::string{});
        }

        if (type == "imageGeneration") {
            if (
                item.contains("savedPath") &&
                item["savedPath"].is_string()
            ) {
                return item["savedPath"]
                    .get<std::string>();
            }

            return {};
        }

        return {};
    }

    static std::string render_activity(
        const nlohmann::json& item,
        const std::string& turn_status
    ) {
        std::string rendered =
            "[" +
            activity_state(
                item,
                turn_status,
                "completed") +
            "] " +
            activity_label(
                item.value(
                    "type",
                    std::string{}));

        const std::string detail =
            activity_detail(item);

        if (!detail.empty()) {
            rendered += ": ";
            rendered += detail;
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

                const std::string turn_status =
                    turn.value(
                        "status",
                        std::string{});

                bool rendered_agent_message = false;

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
                        const std::string agent_text =
                            item.value(
                                "text",
                                std::string{});

                        if (!agent_text.empty()) {
                            rendered_agent_message = true;
                        }

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
                            agent_text);

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

                        append_rendered_block(
                            rendered,
                            "Codex reasoning",
                            reasoning);

                    } else if (is_activity_type(type)) {
                        append_rendered_block(
                            rendered,
                            "Codex activity",
                            render_activity(
                                item,
                                turn_status));

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

                if (
                    turn_status == "completed" &&
                    !rendered_agent_message
                ) {
                    append_rendered_block(
                        rendered,
                        "Codex",
                        "(Turn completed without text.)");

                } else if (turn_status == "failed") {
                    std::string error_message;

                    if (
                        turn.contains("error") &&
                        turn["error"].is_object()
                    ) {
                        error_message =
                            turn["error"].value(
                                "message",
                                std::string{});
                    }

                    append_rendered_block(
                        rendered,
                        "Codex error",
                        error_message.empty()
                            ? "The turn failed without an error message."
                            : error_message);

                } else if (turn_status == "interrupted") {
                    append_rendered_block(
                        rendered,
                        "Codex",
                        "Turn interrupted.");
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

                clear_active_thread_surfaces();

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
        current_thread_turn_failed_ = false;

        set_active_thread_surfaces(
            display_thread_label(
                result.thread),
            resumed_cwd,
            result.thread_id,
            result.thread);

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

    static std::string live_activity_label(
        const nlohmann::json& item
    ) {
        return activity_label(
            item.value(
                "type",
                std::string{}));
    }

    static bool is_live_activity(
        const nlohmann::json& item
    ) {
        const std::string type =
            item.value("type", std::string{});

        return
            type != "agentMessage" &&
            type != "reasoning" &&
            type != "plan";
    }

    void render_live_turn() {
        std::string rendered =
            live_base_transcript_;

        append_rendered_block(
            rendered,
            "Codex reasoning",
            live_reasoning_summary_);

        if (!live_activities_.empty()) {
            std::string activities;

            for (
                const LiveActivity& activity :
                live_activities_
            ) {
                if (!activities.empty()) {
                    activities += '\n';
                }

                activities += "[";
                activities += activity.state;
                activities += "] ";
                activities += activity.label;
            }

            append_rendered_block(
                rendered,
                "Codex activity",
                activities);
        }

        append_rendered_block(
            rendered,
            "Codex",
            live_agent_text_);

        const auto buffer =
            transcript_.get_buffer();

        buffer->set_text(rendered);

        auto end = buffer->end();
        transcript_.scroll_to(end);
    }

    void begin_live_turn() {
        {
            std::lock_guard<std::mutex> lock(
                turn_event_mutex_);
            pending_turn_events_.clear();
        }

        active_turn_id_.clear();
        stop_requested_ = false;
        live_reasoning_summary_.clear();
        live_agent_text_.clear();
        live_activities_.clear();

        live_base_transcript_ =
            transcript_.get_buffer()
                ->get_text()
                .raw();
    }

    void finalize_live_activities(
        const std::string& state
    ) {
        bool changed = false;

        for (LiveActivity& activity : live_activities_) {
            if (activity.state == "running") {
                activity.state = state;
                changed = true;
            }
        }

        if (changed) {
            render_live_turn();
        }
    }

    std::string request_approval(
        const AppServerClient::ApprovalRequest& request
    ) {
        {
            std::lock_guard<std::mutex> lock(
                approval_mutex_);

            if (approval_waiting_) {
                return "decline";
            }

            pending_approval_ = request;
            approval_decision_.clear();
            approval_waiting_ = true;
            approval_resolved_ = false;
        }

        approval_dispatcher_.emit();

        std::unique_lock<std::mutex> lock(
            approval_mutex_);

        approval_condition_.wait(
            lock,
            [this]() {
                return approval_resolved_;
            });

        std::string decision =
            approval_decision_;

        pending_approval_ =
            AppServerClient::ApprovalRequest{};
        approval_decision_.clear();
        approval_waiting_ = false;
        approval_resolved_ = false;

        return decision.empty()
            ? "decline"
            : decision;
    }

    void handle_approval_request() {
        AppServerClient::ApprovalRequest request;

        {
            std::lock_guard<std::mutex> lock(
                approval_mutex_);

            if (
                !approval_waiting_ ||
                approval_resolved_
            ) {
                return;
            }

            request = pending_approval_;
        }

        const auto nullable_string =
            [&request](
                const std::string& key
            ) {
                if (
                    !request.params.is_object() ||
                    !request.params.contains(key) ||
                    !request.params[key].is_string()
                ) {
                    return std::string{};
                }

                return request.params[key]
                    .get<std::string>();
            };

        const auto json_string =
            [&request](
                const std::string& key
            ) {
                if (
                    !request.params.is_object() ||
                    !request.params.contains(key) ||
                    request.params[key].is_null()
                ) {
                    return std::string{};
                }

                const auto& value =
                    request.params[key];

                return value.is_string()
                    ? value.get<std::string>()
                    : value.dump(2);
            };

        const bool command_approval =
            request.method ==
            "item/commandExecution/requestApproval";

        const std::string reason =
            nullable_string("reason");
        const std::string command =
            nullable_string("command");
        const std::string cwd =
            nullable_string("cwd");
        const std::string grant_root =
            nullable_string("grantRoot");

        const bool has_network_approval_context =
            request.params.is_object() &&
            request.params.contains(
                "networkApprovalContext") &&
            !request.params[
                "networkApprovalContext"].is_null();

        const bool session_approval_available =
            !command_approval ||
            has_network_approval_context;

        std::string permission_details;

        const auto append_permission_detail =
            [&permission_details](
                const std::string& label,
                const std::string& value
            ) {
                if (value.empty()) {
                    return;
                }

                if (!permission_details.empty()) {
                    permission_details += "\n\n";
                }

                permission_details += label;
                permission_details += ":\n";
                permission_details += value;
            };

        if (command_approval) {
            append_permission_detail(
                "Environment",
                json_string("environmentId"));
            append_permission_detail(
                "Additional permissions",
                json_string("additionalPermissions"));
            append_permission_detail(
                "Network approval context",
                json_string("networkApprovalContext"));
            append_permission_detail(
                "Proposed command policy amendment",
                json_string("proposedExecpolicyAmendment"));
            append_permission_detail(
                "Proposed network policy amendments",
                json_string("proposedNetworkPolicyAmendments"));
        }

        status_label_.set_text(
            "Codex: approval required");

        Gtk::Dialog dialog;

        dialog.set_title("Codex approval");
        dialog.set_transient_for(*this);
        dialog.set_modal(true);
        dialog.set_resizable(true);
        dialog.set_default_size(
            700,
            permission_details.empty()
                ? 430
                : 560);

        dialog.get_style_context()->add_class(
            "approval-dialog");

        auto* content_area =
            dialog.get_content_area();

        Gtk::Box shell(
            Gtk::ORIENTATION_VERTICAL,
            18);

        shell.set_border_width(24);
        shell.get_style_context()->add_class(
            "approval-shell");

        content_area->pack_start(
            shell,
            Gtk::PACK_EXPAND_WIDGET);

        Gtk::Label eyebrow("CODEX APPROVAL");
        eyebrow.set_xalign(0.0F);
        eyebrow.get_style_context()->add_class(
            "approval-eyebrow");
        shell.pack_start(
            eyebrow,
            Gtk::PACK_SHRINK);

        Gtk::Label title(
            command_approval
                ? "Run this command?"
                : "Allow these file changes?");

        title.set_xalign(0.0F);
        title.get_style_context()->add_class(
            "approval-title");
        shell.pack_start(
            title,
            Gtk::PACK_SHRINK);

        Gtk::Label summary(
            command_approval
                ? "Codex needs your approval before it can "
                    "run this command."
                : "Codex needs your approval before it can "
                    "write these changes.");

        summary.set_xalign(0.0F);
        summary.set_line_wrap(true);
        summary.get_style_context()->add_class(
            "approval-summary");
        shell.pack_start(
            summary,
            Gtk::PACK_SHRINK);

        Gtk::Box action_card(
            Gtk::ORIENTATION_VERTICAL,
            6);

        action_card.get_style_context()->add_class(
            "approval-card");

        Gtk::Label action_label(
            command_approval
                ? "COMMAND"
                : "WRITE ACCESS");

        action_label.set_xalign(0.0F);
        action_label.get_style_context()->add_class(
            "approval-card-label");
        action_card.pack_start(
            action_label,
            Gtk::PACK_SHRINK);

        Gtk::Label action_value(
            command_approval
                ? (
                    command.empty()
                        ? "Command details unavailable"
                        : command
                )
                : (
                    grant_root.empty()
                        ? "Requested files were not specified"
                        : grant_root
                ));

        action_value.set_xalign(0.0F);
        action_value.set_line_wrap(true);
        action_value.set_selectable(true);

        if (command_approval) {
            action_value.get_style_context()->add_class(
                "approval-code");
        }

        action_card.pack_start(
            action_value,
            Gtk::PACK_SHRINK);

        shell.pack_start(
            action_card,
            Gtk::PACK_SHRINK);

        Gtk::Box metadata(
            Gtk::ORIENTATION_VERTICAL,
            12);

        metadata.get_style_context()->add_class(
            "approval-meta");

        Gtk::Box reason_box(
            Gtk::ORIENTATION_VERTICAL,
            3);
        Gtk::Label reason_key("REASON");
        Gtk::Label reason_value(
            reason.empty()
                ? "No additional reason was provided."
                : reason);

        reason_key.set_xalign(0.0F);
        reason_key.get_style_context()->add_class(
            "approval-meta-key");
        reason_value.set_xalign(0.0F);
        reason_value.set_line_wrap(true);
        reason_value.set_selectable(true);

        reason_box.pack_start(
            reason_key,
            Gtk::PACK_SHRINK);
        reason_box.pack_start(
            reason_value,
            Gtk::PACK_SHRINK);
        metadata.pack_start(
            reason_box,
            Gtk::PACK_SHRINK);

        Gtk::Box location_box(
            Gtk::ORIENTATION_VERTICAL,
            3);
        Gtk::Label location_key(
            command_approval
                ? "WORKING DIRECTORY"
                : "PROJECT");
        Gtk::Label location_value(
            command_approval
                ? (
                    cwd.empty()
                        ? "Not specified"
                        : cwd
                )
                : (
                    selected_folder_path_.empty()
                        ? "Current thread"
                        : selected_folder_path_
                ));

        location_key.set_xalign(0.0F);
        location_key.get_style_context()->add_class(
            "approval-meta-key");
        location_value.set_xalign(0.0F);
        location_value.set_line_wrap(true);
        location_value.set_selectable(true);

        location_box.pack_start(
            location_key,
            Gtk::PACK_SHRINK);
        location_box.pack_start(
            location_value,
            Gtk::PACK_SHRINK);
        metadata.pack_start(
            location_box,
            Gtk::PACK_SHRINK);

        shell.pack_start(
            metadata,
            Gtk::PACK_SHRINK);

        Gtk::Expander details_expander(
            "Permission details");

        Gtk::ScrolledWindow details_scroll;
        Gtk::TextView details_view;

        if (!permission_details.empty()) {
            details_expander.set_expanded(false);
            details_expander.get_style_context()->add_class(
                "approval-details");

            details_view.set_name(
                "approval-details-view");
            details_view.set_editable(false);
            details_view.set_cursor_visible(false);
            details_view.set_wrap_mode(
                Gtk::WRAP_WORD_CHAR);
            details_view.set_left_margin(12);
            details_view.set_right_margin(12);
            details_view.set_top_margin(10);
            details_view.set_bottom_margin(10);
            details_view.get_buffer()->set_text(
                permission_details);

            details_scroll.set_policy(
                Gtk::POLICY_AUTOMATIC,
                Gtk::POLICY_AUTOMATIC);
            details_scroll.set_shadow_type(
                Gtk::SHADOW_NONE);
            details_scroll.set_size_request(
                -1,
                170);
            details_scroll.add(details_view);
            details_expander.add(details_scroll);

            shell.pack_start(
                details_expander,
                Gtk::PACK_EXPAND_WIDGET);
        }

        Gtk::Label footnote(
            command_approval
                ? "Approve only if you recognize the command "
                    "and the access it requests."
                : "Approve only if this write access matches "
                    "the change you asked Codex to make.");

        footnote.set_xalign(0.0F);
        footnote.set_line_wrap(true);
        footnote.get_style_context()->add_class(
            "approval-footnote");
        shell.pack_start(
            footnote,
            Gtk::PACK_SHRINK);

        constexpr int approve_once_response = 1;
        constexpr int approve_session_response = 2;
        constexpr int decline_response = 3;
        constexpr int cancel_response = 4;

        Gtk::Button* cancel_button =
            dialog.add_button(
                "Cancel turn",
                cancel_response);
        Gtk::Button* decline_button =
            dialog.add_button(
                "Decline",
                decline_response);
        Gtk::Button* session_button = nullptr;

        if (session_approval_available) {
            session_button =
                dialog.add_button(
                    "Approve for session",
                    approve_session_response);
        }

        Gtk::Button* approve_button =
            dialog.add_button(
                "Approve once",
                approve_once_response);

        if (cancel_button != nullptr) {
            cancel_button->get_style_context()->add_class(
                "approval-danger-button");
            cancel_button->get_style_context()->add_class(
                "destructive-action");
        }

        if (decline_button != nullptr) {
            decline_button->get_style_context()->add_class(
                "approval-neutral-button");
        }

        if (session_button != nullptr) {
            session_button->get_style_context()->add_class(
                "approval-session-button");
        }

        if (approve_button != nullptr) {
            approve_button->get_style_context()->add_class(
                "approval-primary-button");
            approve_button->get_style_context()->add_class(
                "suggested-action");
        }

        auto* action_area =
            dialog.get_action_area();

        if (action_area != nullptr) {
            action_area->set_spacing(8);
            action_area->set_border_width(16);
        }

        dialog.set_default_response(
            session_approval_available
                ? approve_session_response
                : approve_once_response);

        dialog.show_all();

        const int response =
            dialog.run();

        std::string decision{"cancel"};

        if (response == approve_once_response) {
            decision = "accept";
        } else if (
            response == approve_session_response
        ) {
            decision = "acceptForSession";
        } else if (
            response == decline_response
        ) {
            decision = "decline";
        }

        {
            std::lock_guard<std::mutex> lock(
                approval_mutex_);

            if (
                approval_waiting_ &&
                !approval_resolved_
            ) {
                approval_decision_ =
                    decision;
                approval_resolved_ = true;
            }
        }

        approval_condition_.notify_one();

        if (turn_in_progress_) {
            status_label_.set_text(
                decision == "cancel"
                    ? "Codex: cancelling turn"
                    : "Codex: working");
        }
    }

    void handle_turn_events() {
        std::deque<AppServerClient::TurnEvent> events;

        {
            std::lock_guard<std::mutex> lock(
                turn_event_mutex_);
            events.swap(pending_turn_events_);
        }

        bool transcript_changed = false;

        for (
            const AppServerClient::TurnEvent& event :
            events
        ) {
            if (
                !event.thread_id.empty() &&
                event.thread_id != current_thread_id_
            ) {
                continue;
            }

            switch (event.type) {
            case AppServerClient::TurnEvent::Type::
                TurnStarted:
                active_turn_id_ = event.turn_id;
                send_button_.set_tooltip_text(
                    "Stop Codex turn");
                update_send_button_state();
                break;

            case AppServerClient::TurnEvent::Type::
                AgentMessageDelta:
                live_agent_text_ += event.delta;
                transcript_changed = true;
                break;

            case AppServerClient::TurnEvent::Type::
                ReasoningSummaryDelta:
                live_reasoning_summary_ += event.delta;
                transcript_changed = true;
                break;

            case AppServerClient::TurnEvent::Type::
                ReasoningTextDelta:
                break;

            case AppServerClient::TurnEvent::Type::
                ItemStarted:
            case AppServerClient::TurnEvent::Type::
                ItemCompleted: {
                if (!is_live_activity(event.item)) {
                    break;
                }

                const std::string label =
                    live_activity_label(event.item);

                const auto found =
                    std::find_if(
                        live_activities_.begin(),
                        live_activities_.end(),
                        [&event, &label](
                            const LiveActivity& activity
                        ) {
                            if (!event.item_id.empty()) {
                                return
                                    activity.item_id ==
                                    event.item_id;
                            }

                            return
                                activity.item_id.empty() &&
                                activity.label == label &&
                                activity.state == "running";
                        });

                const std::string state =
                    event.type ==
                        AppServerClient::TurnEvent::Type::
                            ItemStarted
                        ? "running"
                        : activity_state(
                            event.item,
                            {},
                            "completed");

                if (found == live_activities_.end()) {
                    live_activities_.push_back(
                        {
                            event.item_id,
                            label,
                            state,
                        });
                } else {
                    found->label = label;
                    found->state = state;
                }

                transcript_changed = true;
                break;
            }
            }
        }

        if (transcript_changed) {
            render_live_turn();
        }
    }

    void request_turn_stop() {
        if (
            !turn_in_progress_ ||
            active_turn_id_.empty() ||
            stop_requested_
        ) {
            return;
        }

        stop_requested_ = true;
        status_label_.set_text("Codex: stopping");
        send_button_.set_tooltip_text("Stop requested");
        update_send_button_state();

        const auto result =
            app_server_.interrupt_turn(
                current_thread_id_,
                active_turn_id_);

        if (!result.success) {
            stop_requested_ = false;
            status_label_.set_text(
                "Codex: stop request failed");
            send_button_.set_tooltip_text(
                "Stop Codex turn");
            update_send_button_state();

            std::cerr
                << "FAIL: GTK turn/interrupt: "
                << result.error
                << '\n';
            return;
        }

        std::cout
            << "PASS: GTK requested interruption for turn "
            << active_turn_id_
            << '\n';
    }

    void handle_send_or_stop() {
        if (turn_in_progress_) {
            request_turn_stop();
            return;
        }

        submit_prompt();
    }

    void set_turn_busy(bool busy) {
        turn_in_progress_ = busy;

        folder_button_.set_sensitive(!busy);

        new_thread_button_.set_sensitive(
            !busy &&
            !selected_folder_path_.empty());

        prompt_.set_editable(!busy);

        send_image_.set_from_icon_name(
            busy
                ? "media-playback-stop-symbolic"
                : "mail-send-symbolic",
            Gtk::ICON_SIZE_BUTTON);

        if (busy) {
            send_button_.set_tooltip_text(
                "Waiting for Codex turn to start");
        } else {
            active_turn_id_.clear();
            stop_requested_ = false;
            send_button_.set_tooltip_text(
                "Send message (Enter)");
        }

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

            clear_active_thread_surfaces();

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
        current_thread_turn_failed_ = false;

        set_active_thread_surfaces(
            "New Thread",
            selected_folder_path_,
            current_thread_id_);

        status_label_.set_text("Codex: connected");

        prompt_.set_sensitive(true);
        prompt_.set_editable(true);
        prompt_.set_cursor_visible(true);
        prompt_.set_can_focus(true);
        prompt_.get_buffer()->set_text("");
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
        prompt_.grab_focus();

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

        begin_live_turn();

        status_label_.set_text("Codex: working");
        current_thread_turn_failed_ = false;
        set_turn_busy(true);

        std::cout
            << "PASS: GTK submitted a turn for thread "
            << thread_id
            << '\n';

        turn_worker_ = std::thread(
            [this, thread_id, prompt_text]() {
                auto result = app_server_.start_turn(
                    thread_id,
                    prompt_text,
                    60000,
                    [this](
                        const AppServerClient::TurnEvent& event
                    ) {
                        {
                            std::lock_guard<std::mutex> lock(
                                turn_event_mutex_);
                            pending_turn_events_.push_back(
                                event);
                        }

                        turn_event_dispatcher_.emit();
                    },
                    [this](
                        const AppServerClient::ApprovalRequest& request
                    ) {
                        return request_approval(request);
                    });

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

        handle_turn_events();

        if (
            !result.streamed_text.empty() &&
            live_agent_text_ != result.streamed_text
        ) {
            live_agent_text_ = result.streamed_text;
            render_live_turn();
        }

        set_turn_busy(false);

        if (!result.success) {
            finalize_live_activities("failed");
            status_label_.set_text(
                "Codex: turn transport failed");

            append_transcript(
                "Codex transport error:\n" +
                result.error);

            current_thread_turn_failed_ = true;

            std::cerr
                << "FAIL: GTK turn transport: "
                << result.error
                << '\n';

            refresh_sidebar_threads();
            return;
        }

        if (result.status == "completed") {
            finalize_live_activities("completed");
            status_label_.set_text("Codex: connected");

            if (live_agent_text_.empty()) {
                append_transcript(
                    "Codex:\n"
                    "(Turn completed without text.)");
            }

        } else if (result.status == "failed") {
            finalize_live_activities("failed");
            current_thread_turn_failed_ = true;

            status_label_.set_text(
                "Codex: turn failed");

            append_transcript(
                "Codex error:\n" +
                (
                    result.turn_error.empty()
                        ? "The turn failed without an error message."
                        : result.turn_error));

        } else if (result.status == "interrupted") {
            finalize_live_activities("interrupted");

            status_label_.set_text(
                "Codex: turn interrupted");

            append_transcript(
                "Codex turn interrupted.");

        } else {
            finalize_live_activities("finished");

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

            clear_active_thread_surfaces();

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
    Gtk::Paned workspace_;
    Gtk::Box sidebar_;
    Gtk::Box content_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box composer_{Gtk::ORIENTATION_HORIZONTAL};

    ThreadHeader thread_header_;
    ContextPanel context_panel_;

    Gtk::Image hamburger_image_;
    Gtk::Image sidebar_image_;
    Gtk::Image folder_image_;
    Gtk::Image new_thread_image_;
    Gtk::Image context_image_;
    Gtk::Image send_image_;

    Gtk::MenuButton hamburger_button_;
    Gtk::ToggleButton sidebar_toggle_button_;
    Gtk::Button folder_button_;
    Gtk::Button new_thread_button_;
    Gtk::ToggleButton context_toggle_button_;
    Gtk::Button send_button_;
    Gtk::ComboBoxText theme_selector_;
    SettingsWindow settings_window_{theme_selector_};

    Glib::RefPtr<Gio::Menu> app_menu_model_;

    Gtk::Label selected_folder_;
    Gtk::Label status_label_;
    Gtk::Label sidebar_title_;

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
    std::string current_thread_default_label_;
    nlohmann::json current_thread_data_ =
        nlohmann::json::object();
    std::string theme_id_{"system"};

    bool sidebar_visible_{true};
    int sidebar_width_{260};
    bool context_panel_visible_{true};
    int context_panel_width_{320};
    bool turn_in_progress_{false};
    bool current_thread_turn_failed_{false};

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

    Glib::RefPtr<Gtk::CssProvider>
        structural_css_provider_;
    Glib::RefPtr<Gtk::CssProvider>
        theme_css_provider_;

    struct LiveActivity {
        std::string item_id;
        std::string label;
        std::string state;
    };

    Glib::Dispatcher turn_dispatcher_;
    Glib::Dispatcher turn_event_dispatcher_;
    Glib::Dispatcher approval_dispatcher_;
    std::thread turn_worker_;

    std::mutex turn_result_mutex_;
    AppServerClient::TurnResult pending_turn_result_;

    std::mutex turn_event_mutex_;
    std::deque<AppServerClient::TurnEvent>
        pending_turn_events_;

    std::mutex approval_mutex_;
    std::condition_variable approval_condition_;
    AppServerClient::ApprovalRequest
        pending_approval_;
    std::string approval_decision_;
    bool approval_waiting_{false};
    bool approval_resolved_{false};

    std::string active_turn_id_;
    bool stop_requested_{false};

    std::string live_base_transcript_;
    std::string live_reasoning_summary_;
    std::string live_agent_text_;
    std::vector<LiveActivity> live_activities_;
};

int main(int argc, char* argv[]) {
    const auto application = Gtk::Application::create(
        argc,
        argv,
        "com.ronpatrick.ThreadDeck");

    MainWindow window;

    const auto settings_action =
        Gio::SimpleAction::create("settings");

    settings_action->signal_activate().connect(
        [&window](const Glib::VariantBase&) {
            window.show_settings();
        });

    application->add_action(settings_action);

    const auto about_action =
        Gio::SimpleAction::create("about");

    about_action->signal_activate().connect(
        [&window](const Glib::VariantBase&) {
            window.show_about();
        });

    application->add_action(about_action);

    const auto quit_action =
        Gio::SimpleAction::create("quit");

    quit_action->signal_activate().connect(
        [application](const Glib::VariantBase&) {
            application->quit();
        });

    application->add_action(quit_action);

    return application->run(window);
}
