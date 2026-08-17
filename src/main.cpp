#include "app_server_client.h"
#include "context_panel.h"
#include "secret_store.h"
#include "thread_header.h"

#include <nlohmann/json.hpp>

#include <glib.h>
#include <giomm/appinfo.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf-io.h>
#include <gdkmm/cursor.h>
#include <gdkmm/pixbuf.h>
#include <gdkmm/screen.h>
#include <glibmm/dispatcher.h>
#include <glibmm/convert.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <gtkmm/aboutdialog.h>
#include <gtkmm/application.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/clipboard.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/dialog.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/expander.h>
#include <gtkmm/menu.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/overlay.h>
#include <gtkmm/paned.h>
#include <gtkmm/popover.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/spinner.h>
#include <gtkmm/stack.h>
#include <gtkmm/stacksidebar.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

class DoubleShieldIcon final : public Gtk::DrawingArea {
public:
    DoubleShieldIcon() {
        set_size_request(25, 22);
        set_halign(Gtk::ALIGN_CENTER);
        set_valign(Gtk::ALIGN_CENTER);
    }

protected:
    bool on_draw(
        const Cairo::RefPtr<Cairo::Context>& context
    ) override {
        const Gtk::Widget* parent = get_parent();
        const auto style =
            parent != nullptr
                ? parent->get_style_context()
                : get_style_context();
        const auto state =
            parent != nullptr
                ? parent->get_state_flags()
                : get_state_flags();
        const Gdk::RGBA color =
            style->get_color(state);
        const Gtk::Allocation allocation =
            get_allocation();
        const double origin_x =
            std::max(
                0.0,
                (allocation.get_width() - 25.0) / 2.0);
        const double origin_y =
            std::max(
                0.0,
                (allocation.get_height() - 22.0) / 2.0);

        context->set_line_width(1.8);
        context->set_line_join(
            Cairo::LINE_JOIN_ROUND);
        context->set_line_cap(
            Cairo::LINE_CAP_ROUND);

        draw_shield(
            context,
            origin_x + 1.5,
            origin_y + 1.0,
            15.0,
            18.0,
            color,
            0.58);
        draw_shield(
            context,
            origin_x + 8.0,
            origin_y + 4.0,
            15.0,
            17.0,
            color,
            1.0);

        return true;
    }

private:
    static void draw_shield(
        const Cairo::RefPtr<Cairo::Context>& context,
        double x,
        double y,
        double width,
        double height,
        const Gdk::RGBA& color,
        double opacity
    ) {
        context->set_source_rgba(
            color.get_red(),
            color.get_green(),
            color.get_blue(),
            color.get_alpha() * opacity);
        shield_path(
            context,
            x + 3.1,
            y + 4.0,
            width - 6.2,
            height - 7.0);
        context->fill();
        shield_path(
            context,
            x,
            y,
            width,
            height);
        context->stroke();
    }

    static void shield_path(
        const Cairo::RefPtr<Cairo::Context>& context,
        double x,
        double y,
        double width,
        double height
    ) {
        const double center = x + (width / 2.0);

        context->begin_new_path();
        context->move_to(center, y);
        context->curve_to(
            center + (width * 0.15),
            y + (height * 0.10),
            x + width - (width * 0.13),
            y + (height * 0.17),
            x + width,
            y + (height * 0.19));
        context->line_to(
            x + width - (width * 0.05),
            y + (height * 0.54));
        context->curve_to(
            x + width - (width * 0.07),
            y + (height * 0.74),
            center + (width * 0.17),
            y + height - (height * 0.06),
            center,
            y + height);
        context->curve_to(
            center - (width * 0.17),
            y + height - (height * 0.06),
            x + (width * 0.07),
            y + (height * 0.74),
            x + (width * 0.05),
            y + (height * 0.54));
        context->line_to(
            x,
            y + (height * 0.19));
        context->curve_to(
            x + (width * 0.13),
            y + (height * 0.17),
            center - (width * 0.15),
            y + (height * 0.10),
            center,
            y);
        context->close_path();
    }
};

class SettingsWindow final : public Gtk::Window {
public:
    using SplunkSaveHandler =
        std::function<bool(
            const std::string&,
            const std::string&,
            bool,
            std::string&)>;

    explicit SettingsWindow(
        Gtk::ComboBoxText& theme_selector
    )
        : root_(Gtk::ORIENTATION_HORIZONTAL),
          appearance_page_(Gtk::ORIENTATION_VERTICAL),
          splunk_page_(Gtk::ORIENTATION_VERTICAL),
          splunk_actions_(Gtk::ORIENTATION_HORIZONTAL),
          theme_selector_(theme_selector),
          appearance_title_("Appearance"),
          theme_label_("Theme"),
          theme_description_(
              "Choose whether ThreadDeck follows the current "
              "GTK system appearance or uses a controlled "
              "built-in palette."),
          splunk_title_("Splunk"),
          splunk_description_(
              "These values are inherited by every Codex thread. "
              "The token is stored in Ubuntu's secure keyring, "
              "not in ThreadDeck's JSON settings."),
          splunk_host_label_("Splunk host"),
          splunk_token_label_("Splunk token"),
          splunk_status_("No Splunk token is saved."),
          splunk_save_button_("Save"),
          splunk_remove_button_("Remove") {
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

        splunk_page_.get_style_context()->add_class(
            "settings-page");
        splunk_page_.set_border_width(22);
        splunk_page_.set_spacing(12);

        splunk_title_.set_xalign(0.0F);
        splunk_title_.set_markup(
            "<span size=\"x-large\" weight=\"bold\">"
            "Splunk"
            "</span>");

        splunk_description_.set_xalign(0.0F);
        splunk_description_.set_line_wrap(true);
        splunk_description_.set_max_width_chars(54);

        splunk_host_label_.set_xalign(0.0F);
        splunk_host_label_.set_markup(
            "<b>Host</b>");
        splunk_host_entry_.set_placeholder_text(
            "https://example.splunkcloud.com:8089");

        splunk_token_label_.set_xalign(0.0F);
        splunk_token_label_.set_markup(
            "<b>Token</b>");
        splunk_token_entry_.set_visibility(false);
        splunk_token_entry_.set_placeholder_text(
            "Leave blank to keep the saved token");

        splunk_status_.set_xalign(0.0F);
        splunk_status_.set_line_wrap(true);

        splunk_actions_.set_spacing(8);
        splunk_actions_.pack_start(
            splunk_save_button_,
            Gtk::PACK_SHRINK);
        splunk_actions_.pack_start(
            splunk_remove_button_,
            Gtk::PACK_SHRINK);

        splunk_save_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &SettingsWindow::save_splunk));
        splunk_remove_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &SettingsWindow::remove_splunk));

        splunk_page_.pack_start(
            splunk_title_,
            Gtk::PACK_SHRINK);
        splunk_page_.pack_start(
            splunk_description_,
            Gtk::PACK_SHRINK);
        splunk_page_.pack_start(
            splunk_host_label_,
            Gtk::PACK_SHRINK);
        splunk_page_.pack_start(
            splunk_host_entry_,
            Gtk::PACK_SHRINK);
        splunk_page_.pack_start(
            splunk_token_label_,
            Gtk::PACK_SHRINK);
        splunk_page_.pack_start(
            splunk_token_entry_,
            Gtk::PACK_SHRINK);
        splunk_page_.pack_start(
            splunk_status_,
            Gtk::PACK_SHRINK);
        splunk_page_.pack_start(
            splunk_actions_,
            Gtk::PACK_SHRINK);

        settings_stack_.add(
            splunk_page_,
            "splunk",
            "Splunk");

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

    void set_splunk_save_handler(
        SplunkSaveHandler handler
    ) {
        splunk_save_handler_ = std::move(handler);
    }

    void present_for(
        Gtk::Window& parent,
        const std::string& splunk_host,
        bool token_saved
    ) {
        splunk_host_entry_.set_text(splunk_host);
        splunk_token_entry_.set_text("");
        splunk_status_.set_text(
            token_saved
                ? "A Splunk token is saved in Ubuntu's keyring."
                : "No Splunk token is saved.");

        set_transient_for(parent);
        set_position(Gtk::WIN_POS_CENTER_ON_PARENT);
        show_all();
        present();
    }

private:
    void show_splunk_error(
        const std::string& error
    ) {
        Gtk::MessageDialog dialog(
            *this,
            "Splunk settings were not saved.",
            false,
            Gtk::MESSAGE_ERROR,
            Gtk::BUTTONS_OK,
            true);

        dialog.set_secondary_text(error);
        dialog.run();
    }

    void save_splunk() {
        if (!splunk_save_handler_) {
            return;
        }

        std::string error;

        if (!splunk_save_handler_(
                splunk_host_entry_.get_text().raw(),
                splunk_token_entry_.get_text().raw(),
                false,
                error)) {
            show_splunk_error(error);
            return;
        }

        splunk_token_entry_.set_text("");
        splunk_status_.set_text(
            "Saved. New Codex sessions will use these values.");
    }

    void remove_splunk() {
        if (!splunk_save_handler_) {
            return;
        }

        Gtk::MessageDialog confirmation(
            *this,
            "Remove the saved Splunk settings?",
            false,
            Gtk::MESSAGE_WARNING,
            Gtk::BUTTONS_NONE,
            true);

        confirmation.set_secondary_text(
            "Future Codex sessions will no longer receive "
            "SPLUNK_HOST or SPLUNK_TOKEN from ThreadDeck.");
        confirmation.add_button(
            "_Cancel",
            Gtk::RESPONSE_CANCEL);
        confirmation.add_button(
            "_Remove",
            Gtk::RESPONSE_OK);

        if (confirmation.run() != Gtk::RESPONSE_OK) {
            return;
        }

        std::string error;

        if (!splunk_save_handler_(
                {},
                {},
                true,
                error)) {
            show_splunk_error(error);
            return;
        }

        splunk_host_entry_.set_text("");
        splunk_token_entry_.set_text("");
        splunk_status_.set_text(
            "Splunk settings removed.");
    }

    bool handle_delete(GdkEventAny*) {
        hide();
        return true;
    }

    Gtk::Box root_;
    Gtk::StackSidebar category_sidebar_;
    Gtk::Stack settings_stack_;
    Gtk::Box appearance_page_;
    Gtk::Box splunk_page_;
    Gtk::Box splunk_actions_;

    Gtk::ComboBoxText& theme_selector_;

    Gtk::Label appearance_title_;
    Gtk::Label theme_label_;
    Gtk::Label theme_description_;
    Gtk::Label splunk_title_;
    Gtk::Label splunk_description_;
    Gtk::Label splunk_host_label_;
    Gtk::Entry splunk_host_entry_;
    Gtk::Label splunk_token_label_;
    Gtk::Entry splunk_token_entry_;
    Gtk::Label splunk_status_;
    Gtk::Button splunk_save_button_;
    Gtk::Button splunk_remove_button_;
    SplunkSaveHandler splunk_save_handler_;
};

class AgentsEditorWindow final : public Gtk::Window {
public:
    AgentsEditorWindow()
        : root_(Gtk::ORIENTATION_VERTICAL),
          body_(Gtk::ORIENTATION_HORIZONTAL),
          files_panel_(Gtk::ORIENTATION_VERTICAL),
          editor_panel_(Gtk::ORIENTATION_VERTICAL),
          action_row_(Gtk::ORIENTATION_HORIZONTAL),
          title_("Project Instructions"),
          project_label_("No project selected"),
          file_label_("No AGENTS.md selected"),
          dirty_label_(""),
          create_button_("New AGENTS.md"),
          save_button_("Save"),
          cancel_button_("Cancel") {
        set_title("ThreadDeck Project Instructions");
        set_default_size(920, 680);
        set_modal(false);

        root_.set_border_width(12);
        root_.set_spacing(10);

        title_.set_xalign(0.0F);
        title_.set_markup(
            "<span size=\"x-large\" weight=\"bold\">"
            "Project Instructions"
            "</span>");

        project_label_.set_xalign(0.0F);
        project_label_.set_ellipsize(
            Pango::ELLIPSIZE_MIDDLE);
        project_label_.set_selectable(true);

        file_label_.set_xalign(0.0F);
        file_label_.set_ellipsize(
            Pango::ELLIPSIZE_MIDDLE);
        file_label_.set_selectable(true);

        dirty_label_.set_xalign(0.0F);
        dirty_label_.get_style_context()->add_class(
            "agents-dirty-label");

        files_panel_.set_spacing(6);
        files_panel_.set_border_width(6);
        files_panel_.set_size_request(280, -1);

        files_scroll_.set_policy(
            Gtk::POLICY_NEVER,
            Gtk::POLICY_AUTOMATIC);
        files_scroll_.set_shadow_type(
            Gtk::SHADOW_IN);
        files_scroll_.add(files_panel_);

        editor_.set_wrap_mode(
            Gtk::WRAP_WORD_CHAR);
        editor_.set_left_margin(10);
        editor_.set_right_margin(10);
        editor_.set_top_margin(10);
        editor_.set_bottom_margin(10);
        editor_.set_sensitive(false);

        editor_scroll_.set_policy(
            Gtk::POLICY_AUTOMATIC,
            Gtk::POLICY_AUTOMATIC);
        editor_scroll_.set_shadow_type(
            Gtk::SHADOW_IN);
        editor_scroll_.add(editor_);

        editor_panel_.set_spacing(6);
        editor_panel_.pack_start(
            file_label_,
            Gtk::PACK_SHRINK);
        editor_panel_.pack_start(
            dirty_label_,
            Gtk::PACK_SHRINK);
        editor_panel_.pack_start(
            editor_scroll_,
            Gtk::PACK_EXPAND_WIDGET);

        body_.pack_start(
            files_scroll_,
            Gtk::PACK_SHRINK);
        body_.pack_start(
            editor_panel_,
            Gtk::PACK_EXPAND_WIDGET);

        create_button_.set_tooltip_text(
            "Create AGENTS.md in an existing directory "
            "inside the selected project");

        save_button_.set_sensitive(false);
        cancel_button_.set_sensitive(false);

        create_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &AgentsEditorWindow::create_agents_file));

        save_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &AgentsEditorWindow::save_current));

        cancel_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &AgentsEditorWindow::cancel_current));

        editor_.get_buffer()
            ->signal_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &AgentsEditorWindow::handle_editor_changed));

        action_row_.set_spacing(8);
        action_row_.pack_start(
            create_button_,
            Gtk::PACK_SHRINK);
        action_row_.pack_end(
            save_button_,
            Gtk::PACK_SHRINK);
        action_row_.pack_end(
            cancel_button_,
            Gtk::PACK_SHRINK);

        root_.pack_start(
            title_,
            Gtk::PACK_SHRINK);
        root_.pack_start(
            project_label_,
            Gtk::PACK_SHRINK);
        root_.pack_start(
            body_,
            Gtk::PACK_EXPAND_WIDGET);
        root_.pack_start(
            action_row_,
            Gtk::PACK_SHRINK);

        add(root_);

        signal_delete_event().connect(
            sigc::mem_fun(
                *this,
                &AgentsEditorWindow::handle_delete),
            false);

        show_all_children();
    }

    void present_for(
        Gtk::Window& parent,
        const std::string& project_path
    ) {
        if (project_path.empty()) {
            Gtk::MessageDialog dialog(
                parent,
                "No project is selected.",
                false,
                Gtk::MESSAGE_INFO,
                Gtk::BUTTONS_OK,
                true);

            dialog.set_secondary_text(
                "Select a project folder before opening "
                "Project Instructions.");
            dialog.run();
            return;
        }

        if (
            dirty_ &&
            project_path_ != project_path
        ) {
            Gtk::MessageDialog dialog(
                parent,
                "Unsaved AGENTS.md changes exist.",
                false,
                Gtk::MESSAGE_WARNING,
                Gtk::BUTTONS_OK,
                true);

            dialog.set_secondary_text(
                "Save or Cancel the current edits before "
                "switching projects.");
            dialog.run();
            return;
        }

        set_transient_for(parent);
        set_position(
            Gtk::WIN_POS_CENTER_ON_PARENT);

        if (project_path_ != project_path) {
            project_path_ = project_path;
            clear_current();
        }

        project_label_.set_text(
            "Project: " + project_path_);

        refresh_files();

        show_all();
        present();
    }

private:
    static std::string read_text_file(
        const std::filesystem::path& file
    ) {
        std::ifstream input(
            file,
            std::ios::binary);

        if (!input) {
            throw std::runtime_error(
                "Could not open " +
                file.string());
        }

        std::ostringstream contents;
        contents << input.rdbuf();

        return contents.str();
    }

    static void write_text_file(
        const std::filesystem::path& file,
        const std::string& contents
    ) {
        std::ofstream output(
            file,
            std::ios::binary |
            std::ios::trunc);

        if (!output) {
            throw std::runtime_error(
                "Could not open " +
                file.string() +
                " for writing");
        }

        output.write(
            contents.data(),
            static_cast<std::streamsize>(
                contents.size()));

        if (!output) {
            throw std::runtime_error(
                "Could not write " +
                file.string());
        }
    }

    bool path_is_inside_project(
        const std::filesystem::path& candidate
    ) const {
        if (project_path_.empty()) {
            return false;
        }

        std::error_code error;

        const auto root =
            std::filesystem::weakly_canonical(
                std::filesystem::path(
                    project_path_),
                error);

        if (error) {
            return false;
        }

        const auto child =
            std::filesystem::weakly_canonical(
                candidate,
                error);

        if (error) {
            return false;
        }

        auto root_it = root.begin();
        auto child_it = child.begin();

        for (
            ;
            root_it != root.end();
            ++root_it, ++child_it
        ) {
            if (
                child_it == child.end() ||
                *root_it != *child_it
            ) {
                return false;
            }
        }

        return true;
    }

    void show_error(
        const std::string& primary,
        const std::string& secondary
    ) {
        Gtk::MessageDialog dialog(
            *this,
            primary,
            false,
            Gtk::MESSAGE_ERROR,
            Gtk::BUTTONS_OK,
            true);

        dialog.set_secondary_text(
            secondary);
        dialog.run();
    }

    void show_warning(
        const std::string& primary,
        const std::string& secondary
    ) {
        Gtk::MessageDialog dialog(
            *this,
            primary,
            false,
            Gtk::MESSAGE_WARNING,
            Gtk::BUTTONS_OK,
            true);

        dialog.set_secondary_text(
            secondary);
        dialog.run();
    }

    void clear_file_buttons() {
        const auto children =
            files_panel_.get_children();

        for (auto* child : children) {
            if (child != nullptr) {
                files_panel_.remove(*child);
            }
        }
    }

    std::vector<std::filesystem::path>
    discover_agents_files() const {
        std::vector<std::filesystem::path>
            files;

        if (project_path_.empty()) {
            return files;
        }

        const std::filesystem::path root(
            project_path_);

        std::error_code error;

        if (
            !std::filesystem::exists(
                root,
                error) ||
            error
        ) {
            return files;
        }

        const auto root_agents =
            root / "AGENTS.md";

        if (
            std::filesystem::is_regular_file(
                root_agents,
                error) &&
            !error
        ) {
            files.push_back(
                root_agents);
        }

        error.clear();

        std::filesystem::recursive_directory_iterator
            iterator(
                root,
                std::filesystem::
                    directory_options::
                        skip_permission_denied,
                error);

        const std::filesystem::
            recursive_directory_iterator end;

        while (iterator != end) {
            if (error) {
                error.clear();
            }

            const auto entry =
                *iterator;

            if (
                entry.path() != root_agents &&
                entry.path().filename() ==
                    "AGENTS.md"
            ) {
                std::error_code type_error;

                if (
                    entry.is_regular_file(
                        type_error) &&
                    !type_error
                ) {
                    files.push_back(
                        entry.path());
                }
            }

            iterator.increment(error);
        }

        std::sort(
            files.begin(),
            files.end());

        return files;
    }

    void refresh_files() {
        clear_file_buttons();

        const auto files =
            discover_agents_files();

        if (files.empty()) {
            auto* empty =
                Gtk::manage(
                    new Gtk::Label(
                        "No AGENTS.md files found."));

            empty->set_xalign(0.0F);
            empty->set_line_wrap(true);

            files_panel_.pack_start(
                *empty,
                Gtk::PACK_SHRINK);
        }

        const std::filesystem::path root(
            project_path_);

        for (
            const auto& file :
            files
        ) {
            std::error_code error;

            const auto relative =
                std::filesystem::relative(
                    file,
                    root,
                    error);

            const std::string relative_text =
                error
                    ? file.string()
                    : relative.string();

            int depth = 0;

            if (!error) {
                for (
                    const auto& component :
                    relative.parent_path()
                ) {
                    static_cast<void>(component);
                    ++depth;
                }
            }

            std::string label;

            if (
                !error &&
                relative ==
                    std::filesystem::path(
                        "AGENTS.md")
            ) {
                label =
                    "AGENTS.md  ·  PROJECT ROOT";
            } else {
                label = relative_text;
            }

            auto* button =
                Gtk::manage(
                    new Gtk::Button(
                        label));

            button->set_relief(
                Gtk::RELIEF_NONE);
            button->set_alignment(
                0.0F,
                0.5F);
            button->set_margin_start(
                10 + (depth * 18));
            button->set_tooltip_text(
                file.string());
            button->get_style_context()
                ->add_class(
                    "agents-file-row");

            if (
                !current_file_.empty() &&
                current_file_ == file
            ) {
                button->get_style_context()
                    ->add_class(
                        "agents-file-row-active");
            }

            button->signal_clicked().connect(
                [
                    this,
                    file
                ]() {
                    select_file(file);
                });

            files_panel_.pack_start(
                *button,
                Gtk::PACK_SHRINK);
        }

        files_panel_.show_all_children();
    }

    void select_file(
        const std::filesystem::path& file
    ) {
        if (
            dirty_ &&
            file != current_file_
        ) {
            show_warning(
                "Unsaved changes exist.",
                "Save or Cancel the current AGENTS.md "
                "before opening another file.");
            return;
        }

        try {
            const std::string contents =
                read_text_file(file);

            loading_editor_ = true;

            current_file_ = file;
            original_contents_ =
                contents;
            pending_new_file_ = false;
            dirty_ = false;

            editor_.get_buffer()->set_text(
                contents);

            loading_editor_ = false;

            editor_.set_sensitive(true);
            file_label_.set_text(
                file.string());

            update_dirty_state();
            refresh_files();

        } catch (
            const std::exception& error
        ) {
            loading_editor_ = false;

            show_error(
                "Could not open AGENTS.md.",
                error.what());
        }
    }

    void create_agents_file() {
        if (project_path_.empty()) {
            return;
        }

        if (dirty_) {
            show_warning(
                "Unsaved changes exist.",
                "Save or Cancel the current AGENTS.md "
                "before creating another file.");
            return;
        }

        Gtk::FileChooserDialog dialog(
            *this,
            "Choose an existing project directory",
            Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);

        dialog.add_button(
            "_Cancel",
            Gtk::RESPONSE_CANCEL);
        dialog.add_button(
            "_Choose",
            Gtk::RESPONSE_OK);

        dialog.set_current_folder(
            project_path_);

        const int response =
            dialog.run();

        if (response != Gtk::RESPONSE_OK) {
            return;
        }

        const std::filesystem::path directory(
            dialog.get_filename());

        if (
            !path_is_inside_project(
                directory)
        ) {
            show_error(
                "Directory is outside the selected project.",
                "AGENTS.md can only be created inside:\n" +
                project_path_);
            return;
        }

        std::error_code error;

        if (
            !std::filesystem::is_directory(
                directory,
                error) ||
            error
        ) {
            show_error(
                "The selected path is not an existing directory.",
                directory.string());
            return;
        }

        const auto file =
            directory /
            "AGENTS.md";

        if (
            std::filesystem::exists(
                file,
                error) &&
            !error
        ) {
            select_file(file);
            return;
        }

        loading_editor_ = true;

        current_file_ = file;
        original_contents_.clear();
        pending_new_file_ = true;
        dirty_ = true;

        editor_.get_buffer()->set_text("");

        loading_editor_ = false;

        editor_.set_sensitive(true);
        file_label_.set_text(
            file.string() +
            "  ·  NEW");

        update_dirty_state();
        editor_.grab_focus();
    }

    void update_dirty_state() {
        dirty_label_.set_text(
            dirty_
                ? "Unsaved changes"
                : "");

        save_button_.set_sensitive(
            dirty_ &&
            !current_file_.empty());

        cancel_button_.set_sensitive(
            dirty_);

        set_title(
            dirty_
                ? "ThreadDeck Project Instructions *"
                : "ThreadDeck Project Instructions");
    }

    void handle_editor_changed() {
        if (
            loading_editor_ ||
            current_file_.empty()
        ) {
            return;
        }

        const std::string contents =
            editor_.get_buffer()
                ->get_text()
                .raw();

        dirty_ =
            pending_new_file_ ||
            contents !=
                original_contents_;

        update_dirty_state();
    }

    void save_current() {
        if (
            current_file_.empty() ||
            !dirty_
        ) {
            return;
        }

        try {
            std::error_code error;

            const bool exists_now =
                std::filesystem::exists(
                    current_file_,
                    error);

            if (error) {
                throw std::runtime_error(
                    "Could not inspect " +
                    current_file_.string());
            }

            if (pending_new_file_) {
                if (exists_now) {
                    show_error(
                        "AGENTS.md appeared on disk before Save.",
                        "ThreadDeck will not overwrite it. "
                        "Cancel this pending file, then open the "
                        "new on-disk file.");
                    return;
                }
            } else {
                if (!exists_now) {
                    show_error(
                        "AGENTS.md was removed outside ThreadDeck.",
                        "ThreadDeck will not recreate or overwrite "
                        "it automatically.");
                    return;
                }

                const std::string disk_contents =
                    read_text_file(
                        current_file_);

                if (
                    disk_contents !=
                    original_contents_
                ) {
                    show_error(
                        "AGENTS.md changed outside ThreadDeck.",
                        "Save was refused so the external changes "
                        "are not overwritten. Cancel the current "
                        "edits and reopen the file.");
                    return;
                }
            }

            const std::string contents =
                editor_.get_buffer()
                    ->get_text()
                    .raw();

            write_text_file(
                current_file_,
                contents);

            original_contents_ =
                contents;
            pending_new_file_ = false;
            dirty_ = false;

            file_label_.set_text(
                current_file_.string());

            update_dirty_state();
            refresh_files();

            std::cout
                << "PASS: saved real AGENTS.md "
                << current_file_
                << " without changing project paths or labels\n";

        } catch (
            const std::exception& error
        ) {
            show_error(
                "Could not save AGENTS.md.",
                error.what());
        }
    }

    void cancel_current() {
        if (!dirty_) {
            return;
        }

        loading_editor_ = true;

        if (pending_new_file_) {
            current_file_.clear();
            original_contents_.clear();
            pending_new_file_ = false;
            dirty_ = false;

            editor_.get_buffer()->set_text("");
            editor_.set_sensitive(false);
            file_label_.set_text(
                "No AGENTS.md selected");
        } else {
            editor_.get_buffer()->set_text(
                original_contents_);
            dirty_ = false;
        }

        loading_editor_ = false;

        update_dirty_state();
        refresh_files();

        std::cout
            << "PASS: cancelled unsaved AGENTS.md edits "
            << "without modifying disk\n";
    }

    void clear_current() {
        loading_editor_ = true;

        current_file_.clear();
        original_contents_.clear();
        pending_new_file_ = false;
        dirty_ = false;

        editor_.get_buffer()->set_text("");
        editor_.set_sensitive(false);
        file_label_.set_text(
            "No AGENTS.md selected");

        loading_editor_ = false;

        update_dirty_state();
    }

    bool handle_delete(
        GdkEventAny*
    ) {
        if (dirty_) {
            show_warning(
                "Unsaved AGENTS.md changes exist.",
                "Use Save or Cancel before closing "
                "Project Instructions.");
            return true;
        }

        hide();
        return true;
    }

    Gtk::Box root_;
    Gtk::Box body_;
    Gtk::Box files_panel_;
    Gtk::Box editor_panel_;
    Gtk::Box action_row_;

    Gtk::Label title_;
    Gtk::Label project_label_;
    Gtk::Label file_label_;
    Gtk::Label dirty_label_;

    Gtk::ScrolledWindow files_scroll_;
    Gtk::ScrolledWindow editor_scroll_;
    Gtk::TextView editor_;

    Gtk::Button create_button_;
    Gtk::Button save_button_;
    Gtk::Button cancel_button_;

    std::string project_path_;
    std::filesystem::path current_file_;
    std::string original_contents_;

    bool pending_new_file_{false};
    bool dirty_{false};
    bool loading_editor_{false};
};


class MainWindow final : public Gtk::ApplicationWindow {
public:
    enum class ShieldOperation {
        Status,
        Enable,
        Disable,
    };

    struct CompletedShieldOperation {
        ShieldOperation operation{
            ShieldOperation::Status};
        bool success{false};
        bool enabled{false};
        int exit_code{-1};
    };

    struct PromptEditSnapshot {
        Glib::ustring text;
        int insert_offset{0};
        int selection_bound_offset{0};
        std::vector<std::pair<int, int>>
            pasted_ranges;
    };

    struct ComposerDraft {
        PromptEditSnapshot current;
        std::vector<PromptEditSnapshot> undo_history;
        std::vector<PromptEditSnapshot> redo_history;
        std::vector<std::string> image_paths;
        std::vector<std::string> audio_paths;
        std::vector<std::string>
            temporary_attachment_paths;
    };

    struct PromptCommandHistoryNavigation {
        bool active{false};
        std::size_t index{0};
        Glib::ustring draft;
    };

    struct ActivityExpansionPayload {
        std::string activity_identity;
        std::string preview;
        std::string full_text;
    };

    struct ThreadSearchRequest {
        std::size_t generation{0};
        std::string search_term;
        AppServerClient::ProcessEnvironment environment;
    };

    struct CompletedThreadSearch {
        std::size_t generation{0};
        std::string search_term;
        std::vector<nlohmann::json> threads;
        std::string error;
    };

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

        settings_window_.set_splunk_save_handler(
            [this](
                const std::string& host,
                const std::string& token,
                bool remove,
                std::string& error
            ) {
                return save_splunk_settings(
                    host,
                    token,
                    remove,
                    error);
            });

        folder_button_.signal_clicked().connect(
            sigc::mem_fun(*this, &MainWindow::select_folder));

        new_thread_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::create_thread_for_selected_folder));

        shield_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_shield_clicked));

        auto_copy_button_.signal_toggled().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_auto_copy_toggled));

        pause_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_pause_clicked));

        remote_shield_button_.signal_toggled().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_remote_shield_panel_toggled));

        remote_hosts_add_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::add_remote_host));

        remote_hosts_close_button_.signal_clicked().connect(
            [this]() {
                remote_shield_button_.set_active(false);
            });

        sidebar_toggle_button_.signal_toggled().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_sidebar_toggled));

        context_toggle_button_.set_tooltip_text(
            "Show or hide the contextual inspector");
        context_toggle_button_.get_style_context()->add_class(
            "context-toggle-button");

        folder_button_.set_tooltip_text(
            "Create new project");
        new_thread_button_.set_tooltip_text(
            "Create a new Codex thread in the selected project");

        send_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_send_or_stop));

        continue_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::continue_current_thread));

        attachment_button_.signal_clicked().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::choose_local_images));

        audio_attachment_button_
            .signal_clicked()
            .connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::choose_local_audio));

        clear_attachments_button_
            .signal_clicked()
            .connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::clear_composer_attachments));

        turn_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_turn_finished));

        turn_event_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_turn_events));

        thread_activation_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_thread_activation_finished));

        thread_move_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_thread_move_finished));

        skill_load_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_skill_load_finished));

        shield_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_shield_operation_finished));

        shell_command_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_shell_command_finished));

        approval_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_approval_request));

        thread_search_dispatcher_.connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_thread_search_finished));

        approval_blink_connection_ =
            Glib::signal_timeout().connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::update_approval_blink),
                650);

        prompt_.signal_key_press_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::on_prompt_key_press),
            false);

        signal_key_press_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::on_window_key_press),
            false);

        prompt_.get_buffer()->signal_changed().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_prompt_changed));

        prompt_.get_buffer()->signal_insert().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_prompt_text_inserted),
            true);

        const std::vector<Gtk::TargetEntry>
            composer_drop_targets = {
                Gtk::TargetEntry("text/uri-list"),
            };

        for (
            Gtk::Widget* drop_target :
            {
                static_cast<Gtk::Widget*>(&prompt_),
                static_cast<Gtk::Widget*>(&prompt_scroll_),
                static_cast<Gtk::Widget*>(&composer_),
                static_cast<Gtk::Widget*>(&composer_area_),
            }
        ) {
            drop_target->drag_dest_set(
                composer_drop_targets,
                Gtk::DEST_DEFAULT_ALL,
                Gdk::ACTION_COPY);
            drop_target->signal_drag_data_received().connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::handle_composer_file_drop));
        }

        sidebar_search_.signal_search_changed().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_thread_search_changed));

        skill_popover_.set_relative_to(prompt_);
        skill_suggestions_.set_spacing(2);
        skill_suggestions_.set_border_width(6);
        skill_popover_.add(skill_suggestions_);

        new_thread_button_.set_sensitive(false);
        send_button_.set_sensitive(false);
        continue_button_.set_sensitive(false);

        send_image_.set_from_icon_name(
            "mail-send-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        send_button_.set_image(send_image_);
        send_button_.set_always_show_image(true);
        send_button_.set_relief(Gtk::RELIEF_NONE);
        send_button_.set_size_request(42, 42);
        send_button_.set_valign(Gtk::ALIGN_END);
        send_button_.set_tooltip_text("Send message (Enter)");

        continue_button_.set_label("Continue");
        continue_button_.set_relief(Gtk::RELIEF_NORMAL);
        continue_button_.set_size_request(-1, 42);
        continue_button_.set_valign(Gtk::ALIGN_END);
        continue_button_.set_tooltip_text(
            "Continue this thread (F11 or Fn+F11)");

        model_label_.set_text("Model");
        effort_label_.set_text("Reasoning");
        mode_label_.set_text("Mode");
        access_label_.set_text("Access");

        for (
            Gtk::Label* label :
            {
                &model_label_,
                &effort_label_,
                &mode_label_,
                &access_label_,
            }
        ) {
            label->set_xalign(0.0F);
            label->get_style_context()->add_class(
                "session-control-label");
        }

        model_selector_.set_tooltip_text(
            "Model used for the next turn and subsequent turns");
        effort_selector_.set_tooltip_text(
            "Reasoning effort used for the next turn and subsequent turns");

        mode_selector_.append(
            "default",
            "Default");
        mode_selector_.append(
            "plan",
            "Plan");
        mode_selector_.set_active_id("default");
        mode_selector_.set_sensitive(false);
        mode_selector_.set_tooltip_text(
            "Codex agent behavior for this thread. Default performs normal "
            "work; Plan uses Codex's native planning workflow. Current: "
            "Default. Plan selection is not enabled yet.");

        access_selector_.append(
            "configured",
            "Thread policy");
        access_selector_.append(
            "yolo",
            "YOLO · Full access");
        access_selector_.set_active_id("configured");
        access_selector_.set_tooltip_text(
            "Saved per thread. Thread policy allows workspace changes and "
            "asks when additional permission is needed; YOLO disables both "
            "restrictions for the normal Unix user (not root).");

        model_selector_.signal_changed().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_model_changed));

        effort_selector_.signal_changed().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_effort_changed));

        access_selector_.signal_changed().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_access_changed));

        usage_label_.set_xalign(1.0F);
        usage_label_.set_hexpand(true);
        usage_label_.set_halign(Gtk::ALIGN_END);
        usage_label_.set_text("Usage —");
        usage_label_.get_style_context()->add_class(
            "usage-strip");

        session_controls_.set_spacing(8);
        session_controls_.set_valign(Gtk::ALIGN_CENTER);
        session_controls_.get_style_context()->add_class(
            "session-controls");

        session_controls_.pack_start(
            model_label_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            model_selector_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            effort_label_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            effort_selector_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            mode_label_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            mode_selector_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            access_label_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            access_selector_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            shield_button_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            auto_copy_button_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            pause_button_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_start(
            remote_shield_button_,
            Gtk::PACK_SHRINK);
        session_controls_.pack_end(
            usage_label_,
            Gtk::PACK_EXPAND_WIDGET);

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
            "Project Instructions",
            "app.project-instructions");
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

        shield_button_.set_label("Shield");
        shield_image_.set_from_icon_name(
            "security-high-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        shield_button_.set_image(shield_image_);
        shield_button_.set_always_show_image(true);
        shield_button_.set_label("");
        shield_button_.set_relief(Gtk::RELIEF_NONE);
        shield_button_.set_size_request(42, 36);
        shield_button_.get_style_context()->add_class(
            "compact-header-button");
        shield_button_.get_style_context()->add_class(
            "shield-button");
        shield_button_.set_tooltip_text(
            "Checking privileged authorization…");
        shield_button_.set_sensitive(false);

        auto_copy_image_.set_from_icon_name(
            "edit-copy-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        auto_copy_button_.set_image(
            auto_copy_image_);
        auto_copy_button_.set_always_show_image(true);
        auto_copy_button_.set_relief(Gtk::RELIEF_NONE);
        auto_copy_button_.set_size_request(42, 36);
        auto_copy_button_.get_style_context()->add_class(
            "compact-header-button");
        auto_copy_button_.get_style_context()->add_class(
            "auto-copy-button");
        auto_copy_button_.set_tooltip_text(
            "Automatically copy shell command blocks from this thread");
        auto_copy_button_.set_sensitive(false);

        pause_image_.set_from_icon_name(
            "media-playback-pause-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        pause_button_.set_image(
            pause_image_);
        pause_button_.set_always_show_image(true);
        pause_button_.set_relief(Gtk::RELIEF_NONE);
        pause_button_.set_size_request(42, 36);
        pause_button_.get_style_context()->add_class(
            "compact-header-button");
        pause_button_.get_style_context()->add_class(
            "thread-pause-button");
        pause_button_.set_tooltip_text(
            "Pause this thread at a safe checkpoint");
        pause_button_.set_sensitive(false);

        remote_shield_button_.add(
            remote_shield_icon_);
        remote_shield_icon_.show();
        remote_shield_button_.set_relief(Gtk::RELIEF_NONE);
        remote_shield_button_.set_size_request(42, 36);
        remote_shield_button_.get_style_context()->add_class(
            "compact-header-button");
        remote_shield_button_.get_style_context()->add_class(
            "remote-shield-button");
        remote_shield_button_.set_tooltip_text(
            "Show Remote Shield hosts for this thread");

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
        status_label_.set_hexpand(false);

        thread_header_.attach_status_widgets(
            status_label_);

        header_.get_style_context()->add_class(
            "threaddeck-header");
        selected_folder_.get_style_context()->add_class(
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

        sidebar_search_.set_placeholder_text(
            "Search threads");
        sidebar_search_.set_tooltip_text(
            "Search titles and message text across all projects");
        sidebar_search_.set_hexpand(true);
        sidebar_search_.set_name(
            "sidebar-thread-search");

        sidebar_sort_image_.set_from_icon_name(
            "view-sort-descending-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        sidebar_sort_button_.set_image(
            sidebar_sort_image_);
        sidebar_sort_button_.set_always_show_image(true);
        sidebar_sort_button_.set_label("Projects");
        sidebar_sort_button_.set_image_position(Gtk::POS_LEFT);
        sidebar_sort_button_.set_relief(
            Gtk::RELIEF_NONE);
        sidebar_sort_button_.set_tooltip_text(
            "Sort projects");
        sidebar_sort_button_.get_style_context()
            ->add_class("sidebar-more-button");

        auto* sort_menu =
            Gtk::manage(new Gtk::Menu());

        const std::array<
            std::pair<const char*, const char*>,
            4
        > sort_choices = {{
            {"updated-desc", "Modified · newest first"},
            {"updated-asc", "Modified · oldest first"},
            {"name-asc", "Name · A to Z"},
            {"name-desc", "Name · Z to A"},
        }};

        for (const auto& choice : sort_choices) {
            auto* item = Gtk::manage(
                new Gtk::MenuItem(choice.second));
            const std::string sort_id = choice.first;
            const std::string sort_label = choice.second;

            item->signal_activate().connect(
                [this, sort_id, sort_label]() {
                    sidebar_project_sort_ = sort_id;
                    sidebar_sort_button_.set_tooltip_text(
                        "Sort projects: " + sort_label);
                    sidebar_sort_image_.set_from_icon_name(
                        sort_id == "updated-desc" ||
                                sort_id == "name-desc"
                            ? "view-sort-descending-symbolic"
                            : "view-sort-ascending-symbolic",
                        Gtk::ICON_SIZE_BUTTON);
                    save_ui_state();
                    schedule_sidebar_refresh();
                });
            sort_menu->append(*item);
        }

        sort_menu->attach_to_widget(
            sidebar_sort_button_);
        sort_menu->show_all();

        sidebar_sort_button_.signal_clicked().connect(
            [this, sort_menu]() {
                sort_menu->popup_at_widget(
                    &sidebar_sort_button_,
                    Gdk::GRAVITY_SOUTH_EAST,
                    Gdk::GRAVITY_NORTH_EAST,
                    nullptr);
            });

        sidebar_search_row_.set_spacing(4);
        sidebar_search_row_.pack_start(
            sidebar_search_,
            Gtk::PACK_EXPAND_WIDGET);
        sidebar_search_row_.pack_start(
            sidebar_sort_button_,
            Gtk::PACK_SHRINK);
        sidebar_.pack_start(
            sidebar_search_row_,
            Gtk::PACK_SHRINK);

        sidebar_list_.set_spacing(4);
        sidebar_scroll_.set_policy(
            Gtk::POLICY_NEVER,
            Gtk::POLICY_AUTOMATIC);
        sidebar_scroll_.set_name(
            "sidebar-thread-scroll");
        sidebar_scroll_.set_overlay_scrolling(false);
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
        transcript_.add_events(
            Gdk::POINTER_MOTION_MASK |
            Gdk::LEAVE_NOTIFY_MASK);
        transcript_.signal_realize().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_transcript_realized));
        transcript_.signal_motion_notify_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_transcript_pointer_motion),
            false);
        transcript_.signal_leave_notify_event().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::handle_transcript_pointer_leave),
            false);
        transcript_scroll_.add(transcript_);

        transcript_bottom_image_.set_from_icon_name(
            "go-bottom-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        transcript_bottom_button_.set_image(
            transcript_bottom_image_);
        transcript_bottom_button_.set_always_show_image(
            true);
        transcript_bottom_button_.set_relief(
            Gtk::RELIEF_NORMAL);
        transcript_bottom_button_.set_halign(
            Gtk::ALIGN_END);
        transcript_bottom_button_.set_valign(
            Gtk::ALIGN_END);
        transcript_bottom_button_.set_margin_end(16);
        transcript_bottom_button_.set_margin_bottom(16);
        transcript_bottom_button_.set_tooltip_text(
            "Jump to the latest message and follow new output");
        transcript_bottom_button_.set_no_show_all(true);
        transcript_bottom_button_.hide();
        transcript_bottom_button_.signal_clicked().connect(
            [this]() {
                scroll_transcript_to_end(true);
            });

        transcript_overlay_.add(transcript_scroll_);
        transcript_overlay_.add_overlay(
            transcript_bottom_button_);

        const auto transcript_adjustment =
            transcript_scroll_.get_vadjustment();

        if (transcript_adjustment) {
            transcript_adjustment
                ->signal_value_changed()
                .connect(
                    sigc::mem_fun(
                        *this,
                        &MainWindow::handle_transcript_scroll_changed));
        }

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
        prompt_scroll_.set_name("prompt-scroll");
        prompt_scroll_.set_policy(
            Gtk::POLICY_NEVER,
            Gtk::POLICY_AUTOMATIC);
        prompt_scroll_.set_shadow_type(Gtk::SHADOW_NONE);
        prompt_scroll_.set_size_request(-1, 48);
        prompt_scroll_.add(prompt_);

        attachment_image_.set_from_icon_name(
            "image-x-generic-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        attachment_button_.set_image(
            attachment_image_);
        attachment_button_.set_always_show_image(true);
        attachment_button_.set_relief(Gtk::RELIEF_NONE);
        attachment_button_.set_size_request(42, 42);
        attachment_button_.set_valign(Gtk::ALIGN_END);
        attachment_button_.set_tooltip_text(
            "Attach one or more local images to the next Codex turn");

        audio_attachment_image_.set_from_icon_name(
            "audio-x-generic-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        audio_attachment_button_.set_image(
            audio_attachment_image_);
        audio_attachment_button_.set_always_show_image(true);
        audio_attachment_button_.set_relief(Gtk::RELIEF_NONE);
        audio_attachment_button_.set_size_request(42, 42);
        audio_attachment_button_.set_valign(Gtk::ALIGN_END);
        audio_attachment_button_.set_tooltip_text(
            "Attach one or more local audio files to the next Codex turn");

        attachment_previews_.set_spacing(8);
        attachment_previews_.set_border_width(4);

        attachment_preview_scroll_.set_policy(
            Gtk::POLICY_AUTOMATIC,
            Gtk::POLICY_NEVER);
        attachment_preview_scroll_.set_shadow_type(
            Gtk::SHADOW_NONE);
        attachment_preview_scroll_.set_size_request(
            -1,
            92);
        attachment_preview_scroll_.add(
            attachment_previews_);

        clear_attachments_button_.set_label("Clear");
        clear_attachments_button_.set_relief(Gtk::RELIEF_NONE);
        clear_attachments_button_.set_tooltip_text(
            "Remove all queued attachments");

        attachment_row_.set_spacing(8);
        attachment_row_.pack_start(
            attachment_preview_scroll_,
            Gtk::PACK_EXPAND_WIDGET);
        attachment_row_.pack_end(
            clear_attachments_button_,
            Gtk::PACK_SHRINK);

        content_.set_orientation(Gtk::ORIENTATION_VERTICAL);
        content_.set_spacing(8);
        content_.set_border_width(8);
        content_.pack_start(
            thread_header_,
            Gtk::PACK_SHRINK);
        content_.pack_start(
            session_controls_,
            Gtk::PACK_SHRINK);
        content_.pack_start(
            transcript_overlay_,
            Gtk::PACK_EXPAND_WIDGET);

        composer_.set_spacing(8);
        composer_.get_style_context()->add_class("composer");
        composer_.pack_start(
            attachment_button_,
            Gtk::PACK_SHRINK);
        composer_.pack_start(
            audio_attachment_button_,
            Gtk::PACK_SHRINK);
        composer_.pack_start(
            prompt_scroll_,
            Gtk::PACK_EXPAND_WIDGET);
        composer_.pack_end(
            send_button_,
            Gtk::PACK_SHRINK);
        composer_.pack_end(
            continue_button_,
            Gtk::PACK_SHRINK);

        composer_area_.set_spacing(4);
        composer_area_.pack_start(
            attachment_row_,
            Gtk::PACK_SHRINK);
        composer_area_.pack_start(
            composer_,
            Gtk::PACK_SHRINK);

        content_.pack_start(
            composer_area_,
            Gtk::PACK_SHRINK);

        workspace_.pack1(content_, true, false);
        workspace_.pack2(context_panel_, false, true);
        workspace_.set_position(620);

        remote_hosts_title_.set_markup(
            "<b>Remote Shield</b>");
        remote_hosts_title_.set_xalign(0.0F);
        remote_hosts_title_.set_hexpand(true);
        remote_hosts_add_button_.set_label("+");
        remote_hosts_add_button_.set_tooltip_text(
            "Add a remote computer and sudo credential");
        remote_hosts_close_button_.set_label("×");
        remote_hosts_close_button_.set_tooltip_text(
            "Close Remote Shield hosts");
        remote_hosts_header_.set_spacing(4);
        remote_hosts_header_.pack_start(
            remote_hosts_title_,
            Gtk::PACK_EXPAND_WIDGET);
        remote_hosts_header_.pack_start(
            remote_hosts_add_button_,
            Gtk::PACK_SHRINK);
        remote_hosts_header_.pack_start(
            remote_hosts_close_button_,
            Gtk::PACK_SHRINK);
        remote_hosts_list_.set_spacing(6);
        remote_hosts_scroll_.set_policy(
            Gtk::POLICY_NEVER,
            Gtk::POLICY_AUTOMATIC);
        remote_hosts_scroll_.add(remote_hosts_list_);
        remote_hosts_panel_.set_spacing(8);
        remote_hosts_panel_.set_border_width(10);
        remote_hosts_panel_.set_size_request(290, -1);
        remote_hosts_panel_.get_style_context()->add_class(
            "remote-hosts-panel");
        remote_hosts_panel_.pack_start(
            remote_hosts_header_,
            Gtk::PACK_SHRINK);
        remote_hosts_panel_.pack_start(
            remote_hosts_scroll_,
            Gtk::PACK_EXPAND_WIDGET);
        remote_hosts_panel_.set_no_show_all(true);

        main_workspace_.pack1(
            workspace_,
            true,
            true);
        main_workspace_.pack2(
            remote_hosts_panel_,
            false,
            true);

        body_.pack1(sidebar_, false, true);
        body_.pack2(main_workspace_, true, false);
        body_.set_position(260);

        root_.pack_start(body_, Gtk::PACK_EXPAND_WIDGET);

        load_ui_state();
        load_known_ssh_hosts();
        sidebar_sort_image_.set_from_icon_name(
            sidebar_project_sort_ == "updated-desc" ||
                    sidebar_project_sort_ == "name-desc"
                ? "view-sort-descending-symbolic"
                : "view-sort-ascending-symbolic",
            Gtk::ICON_SIZE_BUTTON);
        load_splunk_token();
        initialize_text_tags();
        reset_prompt_edit_history();

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
        apply_remote_hosts_panel_visibility();
        refresh_attachment_row();
        apply_sidebar_visibility(false);
        apply_context_panel_visibility(false);
        start_shield_operation(
            ShieldOperation::Status);

        body_.property_position()
            .signal_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::handle_sidebar_position_changed));

        workspace_.property_position()
            .signal_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::handle_context_panel_position_changed));

        Glib::signal_idle().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::apply_restored_pane_positions));

        thread_search_worker_ = std::thread(
            [this]() {
                run_thread_search_worker();
            });
    }

    ~MainWindow() override {
        std::vector<
            std::shared_ptr<PendingApprovalState>
        > approvals_to_cancel;

        {
            std::lock_guard<std::mutex> lock(
                approval_mutex_);

            shutting_down_ = true;

            for (const auto& pending : pending_approvals_) {
                if (
                    pending != nullptr &&
                    !pending->resolved
                ) {
                    pending->decision = "cancel";
                    pending->resolved = true;
                    approvals_to_cancel.push_back(pending);
                }
            }
        }

        for (const auto& pending : approvals_to_cancel) {
            pending->condition.notify_all();
        }

        {
            std::lock_guard<std::mutex> lock(
                thread_search_mutex_);
            thread_search_stop_ = true;
            thread_search_has_request_ = false;
        }

        thread_search_condition_.notify_all();

        if (thread_search_worker_.joinable()) {
            thread_search_worker_.join();
        }

        for (auto& entry : turn_sessions_) {
            ThreadTurnSession& session =
                *entry.second;

            if (
                session.client &&
                session.worker.joinable()
            ) {
                session.client->cancel_pending_operation();
            }
        }

        for (auto& entry : turn_sessions_) {
            if (entry.second->worker.joinable()) {
                entry.second->worker.join();
            }

            if (entry.second->client) {
                entry.second->client->shutdown();
            }

            if (entry.second->loader.joinable()) {
                entry.second->loader.join();
            }
        }

        for (auto& entry : skill_loaders_) {
            if (entry.second.joinable()) {
                entry.second.join();
            }
        }

        if (shield_worker_.joinable()) {
            shield_worker_.join();
        }

        if (thread_move_worker_.joinable()) {
            thread_move_worker_.join();
        }

        save_current_composer_draft();

        for (auto& draft : composer_drafts_) {
            remove_temporary_attachment_files(
                draft.second
                    .temporary_attachment_paths);
        }
    }

private:
public:
    void show_settings() {
        settings_window_.present_for(
            *this,
            splunk_host_,
            !splunk_token_.empty());
    }

    void show_project_instructions() {
        agents_editor_window_.present_for(
            *this,
            selected_folder_path_);
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

    std::string project_cwd(
        const std::string& project_id
    ) const {
        const auto saved = project_paths_.find(project_id);
        return saved != project_paths_.end()
            ? saved->second
            : project_id;
    }

    std::string primary_project_id_for_cwd(
        const std::string& cwd
    ) const {
        for (const auto& project_id :
             selected_project_folders_) {
            if (project_cwd(project_id) == cwd) {
                return project_id;
            }
        }

        return {};
    }

    std::string project_id_for_thread(
        const std::string& thread_id,
        const std::string& cwd
    ) const {
        const auto assigned =
            thread_project_assignments_.find(thread_id);

        if (assigned != thread_project_assignments_.end()) {
            return assigned->second;
        }

        if (
            !selected_project_id_.empty() &&
            project_cwd(selected_project_id_) == cwd
        ) {
            return selected_project_id_;
        }

        return primary_project_id_for_cwd(cwd);
    }

    static std::string new_project_id() {
        gchar* generated = g_uuid_string_random();
        const std::string id =
            generated != nullptr
                ? "project-" + std::string(generated)
                : "project-" + std::to_string(
                    std::time(nullptr));
        g_free(generated);
        return id;
    }

    std::string project_display_name(
        const std::string& cwd
    ) const {
        const std::string project_id =
            project_id_for_thread(
                current_thread_id_,
                cwd);

        return project_id.empty()
            ? folder_name_from_path(cwd)
            : display_folder_label(project_id);
    }

    void clear_active_thread_surfaces() {
        switch_composer_to_thread({});
        current_thread_default_label_.clear();
        current_thread_data_ =
            nlohmann::json::object();
        current_thread_turn_failed_ = false;

        effective_model_.clear();
        effective_reasoning_effort_.clear();
        effective_mode_ = "default";
        effective_approval_policy_ =
            nlohmann::json();
        effective_sandbox_policy_ =
            nlohmann::json();
        configured_approval_policy_ =
            nlohmann::json();
        configured_sandbox_policy_ =
            nlohmann::json();
        thread_token_usage_ =
            nlohmann::json::object();

        skill_catalog_.clear();
        skill_catalog_cwd_.clear();
        skill_popover_.popdown();

        thread_header_.clear();
        context_panel_.clear();

        refresh_session_controls();
        refresh_usage_label();
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
        switch_composer_to_thread(thread_id);
        current_thread_default_label_ =
            default_label;
        current_thread_data_ =
            thread_data;
        last_active_thread_cwd_ =
            cwd;
        current_thread_id_ =
            thread_id;

        refresh_active_thread_surfaces_from_labels();
        seed_prompt_history_from_thread(
            thread_id,
            thread_data);
        load_skills_for_cwd(cwd);
        update_pause_button();
    }

    static double json_number(
        const nlohmann::json& value,
        double fallback = 0.0
    ) {
        if (value.is_number_float()) {
            return value.get<double>();
        }

        if (value.is_number_integer()) {
            return static_cast<double>(
                value.get<long long>());
        }

        if (value.is_number_unsigned()) {
            return static_cast<double>(
                value.get<unsigned long long>());
        }

        return fallback;
    }

    static std::string compact_token_count(
        double value
    ) {
        std::ostringstream output;

        if (value >= 1000000.0) {
            output
                << std::fixed
                << std::setprecision(1)
                << (value / 1000000.0)
                << "M";
        } else if (value >= 1000.0) {
            output
                << std::fixed
                << std::setprecision(1)
                << (value / 1000.0)
                << "k";
        } else {
            output
                << static_cast<long long>(value);
        }

        return output.str();
    }

    static std::string format_reset_time(
        const nlohmann::json& value
    ) {
        if (
            !value.is_number_integer() &&
            !value.is_number_unsigned()
        ) {
            return {};
        }

        const std::time_t timestamp =
            static_cast<std::time_t>(
                value.is_number_unsigned()
                    ? value.get<unsigned long long>()
                    : value.get<long long>());

        const std::tm* local =
            std::localtime(&timestamp);

        if (local == nullptr) {
            return {};
        }

        std::ostringstream output;
        output << std::put_time(
            local,
            "%b %e, %l:%M %p");

        return trim(output.str());
    }

    static std::string rate_window_name(
        const nlohmann::json& window
    ) {
        if (
            !window.is_object() ||
            !window.contains("windowDurationMins") ||
            window["windowDurationMins"].is_null()
        ) {
            return "Limit";
        }

        const int minutes =
            static_cast<int>(
                json_number(
                    window["windowDurationMins"]));

        if (minutes == 300) {
            return "5h";
        }

        if (minutes == 10080) {
            return "7d";
        }

        if (
            minutes > 0 &&
            minutes % 60 == 0
        ) {
            return
                std::to_string(minutes / 60) +
                "h";
        }

        if (minutes > 0) {
            return
                std::to_string(minutes) +
                "m";
        }

        return "Limit";
    }

    static void merge_sparse_json_object(
        nlohmann::json& target,
        const nlohmann::json& update
    ) {
        if (!update.is_object()) {
            return;
        }

        if (!target.is_object()) {
            target = nlohmann::json::object();
        }

        for (
            auto field = update.begin();
            field != update.end();
            ++field
        ) {
            if (field.value().is_null()) {
                continue;
            }

            if (field.value().is_object()) {
                merge_sparse_json_object(
                    target[field.key()],
                    field.value());
            } else {
                target[field.key()] =
                    field.value();
            }
        }
    }

    static bool is_yolo_policy(
        const nlohmann::json& approval_policy,
        const nlohmann::json& sandbox_policy
    ) {
        const bool never_approve =
            approval_policy.is_string() &&
            approval_policy.get<std::string>() ==
                "never";

        const bool danger_full_access =
            sandbox_policy.is_object() &&
            sandbox_policy.value(
                "type",
                std::string{}) ==
                "dangerFullAccess";

        return
            never_approve &&
            danger_full_access;
    }

    const nlohmann::json* find_model(
        const std::string& model_id
    ) const {
        if (!model_catalog_.is_array()) {
            return nullptr;
        }

        for (
            const auto& model :
            model_catalog_
        ) {
            if (!model.is_object()) {
                continue;
            }

            const std::string wire_model =
                model.value(
                    "model",
                    std::string{});

            const std::string id =
                model.value(
                    "id",
                    std::string{});

            if (
                model_id == wire_model ||
                model_id == id
            ) {
                return &model;
            }
        }

        return nullptr;
    }

    void populate_model_selector(
        const std::string& preferred_model = {}
    ) {
        model_selector_.remove_all();

        const std::string selected_model =
            preferred_model.empty()
                ? effective_model_
                : preferred_model;

        bool effective_found = false;
        std::string default_model;
        std::string first_model;

        if (model_catalog_.is_array()) {
            for (
                const auto& model :
                model_catalog_
            ) {
                if (!model.is_object()) {
                    continue;
                }

                std::string id =
                    model.value(
                        "model",
                        std::string{});

                if (id.empty()) {
                    id =
                        model.value(
                            "id",
                            std::string{});
                }

                if (id.empty()) {
                    continue;
                }

                if (first_model.empty()) {
                    first_model = id;
                }

                if (
                    default_model.empty() &&
                    model.value("isDefault", false)
                ) {
                    default_model = id;
                }

                std::string display =
                    model.value(
                        "displayName",
                        std::string{});

                if (display.empty()) {
                    display = id;
                }

                model_selector_.append(
                    id,
                    display);

                if (id == selected_model) {
                    effective_found = true;
                }
            }
        }

        if (
            !selected_model.empty() &&
            !effective_found
        ) {
            model_selector_.append(
                selected_model,
                selected_model);
        }

        if (!selected_model.empty()) {
            model_selector_.set_active_id(
                selected_model);
        } else if (!default_model.empty()) {
            model_selector_.set_active_id(
                default_model);
        } else if (!first_model.empty()) {
            model_selector_.set_active_id(
                first_model);
        }
    }

    void populate_reasoning_selector(
        const std::string& model_id,
        const std::string& preferred_effort,
        bool choose_model_default
    ) {
        effort_selector_.remove_all();

        const nlohmann::json* model =
            find_model(model_id);

        std::string selected_effort =
            preferred_effort;

        if (
            model != nullptr &&
            model->is_object()
        ) {
            if (
                choose_model_default ||
                selected_effort.empty()
            ) {
                selected_effort =
                    model->value(
                        "defaultReasoningEffort",
                        std::string{});
            }

            if (
                model->contains(
                    "supportedReasoningEfforts") &&
                (*model)["supportedReasoningEfforts"]
                    .is_array()
            ) {
                for (
                    const auto& option :
                    (*model)["supportedReasoningEfforts"]
                ) {
                    if (!option.is_object()) {
                        continue;
                    }

                    const std::string effort =
                        option.value(
                            "reasoningEffort",
                            std::string{});

                    if (effort.empty()) {
                        continue;
                    }

                    effort_selector_.append(
                        effort,
                        effort);
                }
            }
        }

        if (!selected_effort.empty()) {
            effort_selector_.set_active_id(
                selected_effort);
        }

        if (
            effort_selector_.get_active_row_number() < 0 &&
            !selected_effort.empty()
        ) {
            effort_selector_.append(
                selected_effort,
                selected_effort);
            effort_selector_.set_active_id(
                selected_effort);
        }
    }

    void refresh_session_control_sensitivity() {
        const bool selectable =
            app_server_.is_running() &&
            (
                !current_thread_id_.empty() ||
                !selected_folder_path_.empty()
            );

        model_selector_.set_sensitive(
            selectable &&
            model_selector_.get_active_row_number() >= 0);

        effort_selector_.set_sensitive(
            selectable &&
            effort_selector_.get_active_row_number() >= 0);

        access_selector_.set_sensitive(
            !current_thread_id_.empty());

        mode_selector_.set_sensitive(false);
    }

    void refresh_session_controls() {
        session_controls_updating_ = true;

        std::string selected_model =
            effective_model_;
        const auto saved_model =
            thread_model_selections_.find(
                current_thread_id_);

        if (
            saved_model !=
                thread_model_selections_.end() &&
            !saved_model->second.empty()
        ) {
            selected_model = saved_model->second;
        }

        populate_model_selector(selected_model);

        selected_model =
            model_selector_.get_active_id().raw();

        std::string selected_effort =
            effective_reasoning_effort_;
        const auto saved_effort =
            thread_reasoning_selections_.find(
                current_thread_id_);

        if (
            saved_effort !=
                thread_reasoning_selections_.end() &&
            !saved_effort->second.empty()
        ) {
            selected_effort = saved_effort->second;
        }

        populate_reasoning_selector(
            selected_model,
            selected_effort,
            selected_effort.empty());

        model_selector_.set_tooltip_text(
            "Saved for this thread and applied to the next turn. "
            "A running turn keeps the model it started with.");
        effort_selector_.set_tooltip_text(
            "Saved for this thread and applied to the next turn. "
            "A running turn keeps the reasoning effort it started with.");

        mode_selector_.set_active_id(
            effective_mode_.empty()
                ? "default"
                : effective_mode_);

        const std::string displayed_mode =
            effective_mode_ == "plan"
                ? "Plan"
                : "Default";

        mode_selector_.set_tooltip_text(
            "Codex agent behavior for this thread. Default performs normal "
            "work; Plan uses Codex's native planning workflow.\nCurrent: " +
            displayed_mode +
            ". Plan selection is not enabled yet.");

        std::string selected_access =
            is_yolo_policy(
                effective_approval_policy_,
                effective_sandbox_policy_)
                ? "yolo"
                : "configured";

        const auto saved_access =
            thread_access_selections_.find(
                current_thread_id_);

        if (
            saved_access !=
                thread_access_selections_.end() &&
            (
                saved_access->second == "configured" ||
                saved_access->second == "yolo"
            )
        ) {
            selected_access = saved_access->second;
        }

        access_selector_.set_active_id(
            selected_access);

        std::string access_tooltip =
            "Saved for this thread and applied to the next turn: " +
            (
                selected_access == "yolo"
                    ? std::string(
                        "YOLO full access (normal Unix user; not root)")
                    : std::string("Thread policy")
            ) +
            "\nCurrent effective approval policy: " +
            (
                effective_approval_policy_.is_null()
                    ? std::string("unknown")
                    : effective_approval_policy_.dump()
            ) +
            "\nCurrent effective sandbox: " +
            (
                effective_sandbox_policy_.is_null()
                    ? std::string("unknown")
                    : effective_sandbox_policy_.dump()
            );

        access_selector_.set_tooltip_text(
            access_tooltip);

        update_shield_button();
        update_auto_copy_button();
        update_pause_button();
        refresh_remote_hosts_panel();

        session_controls_updating_ = false;

        refresh_session_control_sensitivity();
    }

    void apply_effective_thread_settings(
        const std::string& model,
        const std::string& reasoning_effort,
        const nlohmann::json& approval_policy,
        const nlohmann::json& sandbox_policy,
        bool capture_configured_access
    ) {
        effective_model_ = model;
        effective_reasoning_effort_ =
            reasoning_effort;
        effective_approval_policy_ =
            approval_policy;
        effective_sandbox_policy_ =
            sandbox_policy;

        if (capture_configured_access) {
            const auto saved_approval =
                thread_configured_approval_policies_.find(
                    current_thread_id_);
            const auto saved_sandbox =
                thread_configured_sandbox_policies_.find(
                    current_thread_id_);

            if (
                saved_approval !=
                    thread_configured_approval_policies_.end() &&
                saved_sandbox !=
                    thread_configured_sandbox_policies_.end()
            ) {
                configured_approval_policy_ =
                    saved_approval->second;
                configured_sandbox_policy_ =
                    saved_sandbox->second;
            } else {
                configured_approval_policy_ =
                    approval_policy;
                configured_sandbox_policy_ =
                    sandbox_policy;

                if (!current_thread_id_.empty()) {
                    thread_configured_approval_policies_[
                        current_thread_id_] =
                        approval_policy;
                    thread_configured_sandbox_policies_[
                        current_thread_id_] =
                        sandbox_policy;
                }
            }
        } else {
            const auto saved_approval =
                thread_configured_approval_policies_.find(
                    current_thread_id_);
            const auto saved_sandbox =
                thread_configured_sandbox_policies_.find(
                    current_thread_id_);

            if (
                saved_approval !=
                    thread_configured_approval_policies_.end() &&
                saved_sandbox !=
                    thread_configured_sandbox_policies_.end()
            ) {
                configured_approval_policy_ =
                    saved_approval->second;
                configured_sandbox_policy_ =
                    saved_sandbox->second;
            }
        }

        refresh_session_controls();
    }

    AppServerClient::SessionOptions
    current_session_options() const {
        AppServerClient::SessionOptions options;

        const auto assigned_project =
            thread_project_assignments_.find(
                current_thread_id_);
        options.cwd =
            assigned_project !=
                    thread_project_assignments_.end()
                ? project_cwd(
                    assigned_project->second)
                : selected_folder_path_;

        options.model =
            model_selector_.get_active_id().raw();

        options.reasoning_effort =
            effort_selector_.get_active_id().raw();

        options.shield_enabled =
            shield_enabled_ &&
            thread_shield_selections_.find(
                current_thread_id_) !=
                thread_shield_selections_.end();
        options.remote_shield_hosts =
            remote_shield_hosts_for_thread(
                current_thread_id_);

        const std::string access =
            access_selector_.get_active_id().raw();

        if (access == "yolo") {
            options.approval_policy =
                "never";
            options.sandbox_mode =
                "danger-full-access";
            options.sandbox_policy = {
                {"type", "dangerFullAccess"},
            };
        } else {
            options.approval_policy =
                "on-request";
            options.sandbox_mode =
                "workspace-write";
            options.sandbox_policy = {
                {"type", "workspaceWrite"},
            };
        }

        return options;
    }

    void handle_model_changed() {
        if (session_controls_updating_) {
            return;
        }

        const std::string model =
            model_selector_.get_active_id().raw();

        if (model.empty()) {
            return;
        }

        session_controls_updating_ = true;

        populate_reasoning_selector(
            model,
            {},
            true);

        session_controls_updating_ = false;

        if (!current_thread_id_.empty()) {
            thread_model_selections_[
                current_thread_id_] = model;

            const std::string effort =
                effort_selector_.get_active_id().raw();
            if (!effort.empty()) {
                thread_reasoning_selections_[
                    current_thread_id_] = effort;
            }

            save_ui_state();
        }

        status_label_.set_text(
            current_thread_id_.empty()
                ? "Codex: model will apply to the new thread"
                : "Codex: model change applies to next message");
    }

    void handle_effort_changed() {
        if (session_controls_updating_) {
            return;
        }

        if (
            effort_selector_.get_active_row_number() >= 0
        ) {
            const std::string effort =
                effort_selector_.get_active_id().raw();

            if (!current_thread_id_.empty()) {
                thread_reasoning_selections_[
                    current_thread_id_] = effort;

                if (auto* session =
                    find_turn_session(current_thread_id_)) {
                    session->options.reasoning_effort = effort;
                }

                save_ui_state();
            }

            status_label_.set_text(
                current_thread_id_.empty()
                    ? "Codex: reasoning will apply to the new thread"
                    : "Codex: reasoning change applies to next message");
        }
    }

    void handle_access_changed() {
        if (session_controls_updating_) {
            return;
        }

        const std::string access =
            access_selector_.get_active_id().raw();

        if (access == "yolo") {
            status_label_.set_text(
                "Codex: YOLO full access applies to next message");
        } else {
            status_label_.set_text(
                "Codex: thread access policy will be restored "
                "on next message");
        }

        if (!current_thread_id_.empty()) {
            thread_access_selections_[
                current_thread_id_] = access;
            save_ui_state();
        }

        refresh_session_controls();
    }

    void refresh_usage_label() {
        std::vector<std::string> parts;
        std::ostringstream tooltip;

        if (
            thread_token_usage_.is_object() &&
            thread_token_usage_.contains("last") &&
            thread_token_usage_["last"].is_object()
        ) {
            const auto& last =
                thread_token_usage_["last"];

            const double last_turn_tokens =
                last.contains("totalTokens")
                    ? json_number(last["totalTokens"])
                    : 0.0;

            if (
                thread_token_usage_.contains(
                    "modelContextWindow") &&
                !thread_token_usage_[
                    "modelContextWindow"].is_null()
            ) {
                const double context_window =
                    json_number(
                        thread_token_usage_[
                            "modelContextWindow"]);

                if (context_window > 0.0) {
                    const int percent_left =
                        std::clamp(
                            static_cast<int>(
                                (
                                    1.0 -
                                    last_turn_tokens /
                                        context_window
                                ) *
                                    100.0 +
                                0.5),
                            0,
                            100);

                    parts.push_back(
                        std::to_string(percent_left) +
                        "% context left");

                    tooltip
                        << "Latest turn: "
                        << compact_token_count(
                            last_turn_tokens)
                        << " / "
                        << compact_token_count(
                            context_window)
                        << " context tokens · "
                        << percent_left
                        << "% left\n";
                }
            }
        }

        if (
            account_rate_limits_.is_object() &&
            account_rate_limits_.contains(
                "rateLimits") &&
            account_rate_limits_["rateLimits"]
                .is_object()
        ) {
            const nlohmann::json* snapshot =
                &account_rate_limits_["rateLimits"];

            if (
                account_rate_limits_.contains(
                    "rateLimitsByLimitId") &&
                account_rate_limits_[
                    "rateLimitsByLimitId"].is_object() &&
                account_rate_limits_[
                    "rateLimitsByLimitId"].contains(
                        "codex") &&
                account_rate_limits_[
                    "rateLimitsByLimitId"]["codex"]
                        .is_object()
            ) {
                snapshot =
                    &account_rate_limits_[
                        "rateLimitsByLimitId"]["codex"];
            }

            for (
                const char* key :
                {"primary", "secondary"}
            ) {
                if (
                    !snapshot->contains(key) ||
                    !(*snapshot)[key].is_object()
                ) {
                    continue;
                }

                const auto& window =
                    (*snapshot)[key];

                const std::string name =
                    rate_window_name(window);

                const int used =
                    static_cast<int>(
                        json_number(
                            window.value(
                                "usedPercent",
                                nlohmann::json(0.0))) +
                        0.5);

                std::string reset;

                if (
                    window.contains("resetsAt") &&
                    !window["resetsAt"].is_null()
                ) {
                    reset = format_reset_time(
                        window["resetsAt"]);
                }

                std::string label =
                    name +
                    " " +
                    std::to_string(used) +
                    "%";

                if (!reset.empty()) {
                    label += " · resets " + reset;
                }

                parts.push_back(label);

                tooltip
                    << name
                    << " usage: "
                    << used
                    << "%";

                if (!reset.empty()) {
                    tooltip
                        << " · resets "
                        << reset;
                }

                tooltip << '\n';
            }
        }

        if (
            account_usage_.is_object() &&
            account_usage_.contains("summary") &&
            account_usage_["summary"].is_object()
        ) {
            const auto& summary =
                account_usage_["summary"];

            if (
                summary.contains("lifetimeTokens") &&
                !summary["lifetimeTokens"].is_null()
            ) {
                tooltip
                    << "Lifetime account tokens: "
                    << summary["lifetimeTokens"].dump()
                    << '\n';
            }

            if (
                summary.contains("currentStreakDays") &&
                !summary["currentStreakDays"].is_null()
            ) {
                tooltip
                    << "Current usage streak: "
                    << summary["currentStreakDays"].dump()
                    << " day(s)\n";
            }
        }

        if (parts.empty()) {
            usage_label_.set_text("Usage —");
        } else {
            std::ostringstream label;

            for (
                std::size_t index = 0;
                index < parts.size();
                ++index
            ) {
                if (index > 0) {
                    label << " · ";
                }

                label << parts[index];
            }

            usage_label_.set_text(
                label.str());
        }

        usage_label_.set_tooltip_text(
            trim(tooltip.str()));
    }

    void load_model_catalog_and_usage() {
        const auto models =
            app_server_.list_models();

        if (
            models.success &&
            models.result.contains("data") &&
            models.result["data"].is_array()
        ) {
            model_catalog_ =
                models.result["data"];

            std::cout
                << "PASS: loaded "
                << model_catalog_.size()
                << " Codex model(s)\n";
        } else {
            std::cerr
                << "FAIL: model/list: "
                << models.error
                << '\n';
        }

        const auto rate_limits =
            app_server_.read_account_rate_limits();

        if (rate_limits.success) {
            account_rate_limits_ =
                rate_limits.result;

            std::cout
                << "PASS: loaded Codex account rate limits\n";
        } else {
            std::cerr
                << "FAIL: account/rateLimits/read: "
                << rate_limits.error
                << '\n';
        }

        const auto usage =
            app_server_.read_account_usage();

        if (usage.success) {
            account_usage_ =
                usage.result;

            std::cout
                << "PASS: loaded Codex account usage\n";
        } else {
            std::cerr
                << "FAIL: account/usage/read: "
                << usage.error
                << '\n';
        }

        refresh_session_controls();
        refresh_usage_label();
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

    bool apply_restored_pane_positions() {
        apply_sidebar_visibility(false);
        apply_context_panel_visibility(false);
        apply_remote_hosts_panel_visibility();
        pane_position_tracking_ready_ = true;
        return false;
    }

    void apply_sidebar_visibility(
        bool persist
    ) {
        pane_position_updating_ = true;

        if (sidebar_visible_) {
            sidebar_.show();

            int restored_width =
                std::max(
                    sidebar_width_,
                    kMinimumPaneWidth);

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
                    kMinimumPaneWidth,
                    restored_width));
        } else {
            sidebar_.hide();
            body_.set_position(0);
        }

        pane_position_updating_ = false;

        update_sidebar_toggle_visuals();

        if (persist) {
            save_ui_state();
        }
    }

    void handle_sidebar_toggled() {
        if (pane_position_updating_) {
            return;
        }

        if (
            sidebar_visible_ &&
            !sidebar_toggle_button_.get_active()
        ) {
            const int divider_position =
                body_.get_position();

            if (divider_position > 0) {
                sidebar_width_ =
                    std::max(
                        divider_position,
                        kMinimumPaneWidth);
            }
        }

        sidebar_visible_ =
            sidebar_toggle_button_.get_active();

        apply_sidebar_visibility(true);
    }

    void handle_sidebar_position_changed() {
        if (
            pane_position_updating_ ||
            !pane_position_tracking_ready_ ||
            !sidebar_visible_
        ) {
            return;
        }

        const int divider_position =
            body_.get_position();

        if (divider_position < kMinimumPaneWidth) {
            pane_position_updating_ = true;
            sidebar_visible_ = false;
            sidebar_toggle_button_.set_active(false);
            sidebar_.hide();
            body_.set_position(0);
            pane_position_updating_ = false;
            update_sidebar_toggle_visuals();
            save_ui_state();
            return;
        }

        if (divider_position >= kMinimumPaneWidth) {
            sidebar_width_ = divider_position;
            save_ui_state();
        }
    }

    void apply_context_panel_visibility(
        bool persist
    ) {
        pane_position_updating_ = true;

        if (context_panel_visible_) {
            context_panel_.show();

            const int allocated_width =
                workspace_.get_allocated_width();

            if (allocated_width > 560) {
                workspace_.set_position(
                    allocated_width -
                    std::clamp(
                        context_panel_width_,
                        kMinimumPaneWidth,
                        600));
            }
        } else {
            context_panel_.hide();
        }

        pane_position_updating_ = false;

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
                        kMinimumPaneWidth,
                        600);
            }
        }

        context_panel_visible_ =
            context_toggle_button_.get_active();

        apply_context_panel_visibility(true);
    }

    void handle_context_panel_position_changed() {
        if (
            pane_position_updating_ ||
            !pane_position_tracking_ready_ ||
            !context_panel_visible_
        ) {
            return;
        }

        const int allocated_width =
            workspace_.get_allocated_width();
        const int panel_width =
            allocated_width -
            workspace_.get_position();

        if (
            allocated_width > 100 &&
            panel_width <= 1
        ) {
            pane_position_updating_ = true;
            context_panel_visible_ = false;
            context_toggle_button_.set_active(false);
            pane_position_updating_ = false;
            return;
        }

        if (panel_width >= kMinimumPaneWidth) {
            context_panel_width_ =
                std::min(
                    panel_width,
                    600);
        }
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

    AppServerClient::ProcessEnvironment
    current_codex_process_environment() const {
        AppServerClient::ProcessEnvironment environment;

        environment.manage_splunk =
            splunk_environment_managed_;
        environment.splunk_host = splunk_host_;
        environment.splunk_token = splunk_token_;
        environment.shield_enabled = false;
        environment.shield_sudo_directory =
            shield_sudo_directory().string();
        environment.shield_executor_path =
            shield_executor_path().string();
        environment.remote_shield_ssh_directory =
            remote_shield_ssh_directory().string();

        return environment;
    }

    static std::filesystem::path
    shield_control_path() {
        std::error_code error;
        const auto executable =
            std::filesystem::read_symlink(
                "/proc/self/exe",
                error);

        if (error || executable.empty()) {
            return {};
        }

        return
            executable.parent_path() /
            "threaddeck-shield-control";
    }

    static std::filesystem::path
    shield_executor_path() {
        const auto control =
            shield_control_path();

        return control.empty()
            ? std::filesystem::path{}
            : control.parent_path() /
                "threaddeck-shield-exec";
    }

    static std::filesystem::path
    shield_sudo_directory() {
        const auto control =
            shield_control_path();

        return control.empty()
            ? std::filesystem::path{}
            : control.parent_path() /
                "shield-bin";
    }

    static std::filesystem::path
    remote_shield_ssh_directory() {
        const auto control =
            shield_control_path();

        return control.empty()
            ? std::filesystem::path{}
            : control.parent_path() /
                "remote-shield-bin";
    }

    static bool valid_remote_destination(
        const std::string& destination
    ) {
        if (
            destination.empty() ||
            destination.front() == '-'
        ) {
            return false;
        }

        return std::all_of(
            destination.begin(),
            destination.end(),
            [](unsigned char character) {
                return
                    std::isalnum(character) ||
                    character == '.' ||
                    character == '_' ||
                    character == '-' ||
                    character == '@' ||
                    character == ':' ||
                    character == '[' ||
                    character == ']' ||
                    character == '%';
            });
    }

    void load_known_ssh_hosts() {
        const char* home = std::getenv("HOME");

        if (home == nullptr || *home == '\0') {
            return;
        }

        std::ifstream input(
            std::filesystem::path(home) /
            ".ssh" /
            "config");

        if (!input) {
            return;
        }

        std::string line;

        while (std::getline(input, line)) {
            const std::size_t comment = line.find('#');

            if (comment != std::string::npos) {
                line.erase(comment);
            }

            std::istringstream parser(line);
            std::string keyword;
            parser >> keyword;

            std::transform(
                keyword.begin(),
                keyword.end(),
                keyword.begin(),
                [](unsigned char character) {
                    return static_cast<char>(
                        std::tolower(character));
                });

            if (keyword != "host") {
                continue;
            }

            std::string alias;

            while (parser >> alias) {
                if (
                    alias.find_first_of("*?!") !=
                        std::string::npos ||
                    !valid_remote_destination(alias)
                ) {
                    continue;
                }

                remote_host_labels_.try_emplace(
                    alias,
                    alias);
            }
        }
    }

    static bool observed_ssh_option_requires_argument(
        const std::string& option
    ) {
        return
            option.size() == 2 &&
            option.front() == '-' &&
            std::string("BbcDEeFIiJLlmOoPpQRSWw")
                .find(option[1]) !=
                std::string::npos;
    }

    void observe_remote_host_from_command(
        const std::string& thread_id,
        const std::string& command
    ) {
        std::size_t search = 0;

        while (search < command.size()) {
            const std::size_t ssh =
                command.find("ssh", search);

            if (ssh == std::string::npos) {
                return;
            }

            const bool left_boundary =
                ssh == 0 ||
                std::isspace(
                    static_cast<unsigned char>(
                        command[ssh - 1])) ||
                std::string(";&|('\"")
                    .find(command[ssh - 1]) !=
                    std::string::npos;
            const std::size_t after = ssh + 3;
            const bool right_boundary =
                after < command.size() &&
                std::isspace(
                    static_cast<unsigned char>(
                        command[after]));

            if (!left_boundary || !right_boundary) {
                search = after;
                continue;
            }

            std::istringstream parser(
                command.substr(after));
            std::vector<std::string> arguments;
            std::string argument;

            while (parser >> argument) {
                arguments.push_back(argument);
            }

            for (
                std::size_t index = 0;
                index < arguments.size();
                ++index
            ) {
                argument = arguments[index];

                while (
                    !argument.empty() &&
                    std::string("'\"").find(
                        argument.front()) !=
                        std::string::npos
                ) {
                    argument.erase(argument.begin());
                }

                while (
                    !argument.empty() &&
                    std::string("'\";,|&)").find(
                        argument.back()) !=
                        std::string::npos
                ) {
                    argument.pop_back();
                }

                if (argument == "--") {
                    continue;
                }

                if (
                    argument.size() > 1 &&
                    argument.front() == '-'
                ) {
                    if (
                        observed_ssh_option_requires_argument(
                            argument) &&
                        index + 1 < arguments.size()
                    ) {
                        ++index;
                    }
                    continue;
                }

                if (!valid_remote_destination(argument)) {
                    break;
                }

                const bool new_host =
                    remote_host_labels_.try_emplace(
                        argument,
                        argument).second;
                const bool new_association =
                    thread_observed_remote_hosts_[
                        thread_id].insert(argument).second;

                if (new_host || new_association) {
                    save_ui_state();

                    if (
                        thread_id == current_thread_id_ &&
                        remote_hosts_panel_visible_
                    ) {
                        refresh_remote_hosts_panel();
                    }
                }

                return;
            }

            search = after;
        }
    }

    std::vector<std::string>
    remote_shield_hosts_for_thread(
        const std::string& thread_id
    ) const {
        std::vector<std::string> hosts;
        const auto selected =
            thread_remote_shield_hosts_.find(thread_id);

        if (selected == thread_remote_shield_hosts_.end()) {
            return hosts;
        }

        for (const auto& host : selected->second) {
            if (
                remote_host_labels_.find(host) !=
                    remote_host_labels_.end() &&
                remote_host_credential_saved_.find(host) !=
                    remote_host_credential_saved_.end()
            ) {
                hosts.push_back(host);
            }
        }

        return hosts;
    }

    nlohmann::json remote_shield_host_map_for_thread(
        const std::string& thread_id
    ) const {
        nlohmann::json hosts =
            nlohmann::json::object();

        for (const auto& host :
             remote_shield_hosts_for_thread(thread_id)) {
            hosts[host] = host;
        }

        return hosts;
    }

    bool save_remote_host_credential(
        const std::string& host,
        const std::string& password
    ) {
        if (
            password.empty() ||
            password.find('\n') != std::string::npos ||
            password.find('\r') != std::string::npos
        ) {
            Gtk::MessageDialog dialog(
                *this,
                "Enter a valid remote sudo password.",
                false,
                Gtk::MESSAGE_ERROR,
                Gtk::BUTTONS_OK,
                true);
            dialog.set_secondary_text(
                "The password cannot be empty or contain a newline.");
            dialog.run();
            return false;
        }

        std::string error;

        if (!SecretStore::save_remote_sudo_password(
                host,
                password,
                error)) {
            Gtk::MessageDialog dialog(
                *this,
                "The remote sudo credential was not saved.",
                false,
                Gtk::MESSAGE_ERROR,
                Gtk::BUTTONS_OK,
                true);
            dialog.set_secondary_text(error);
            dialog.run();
            return false;
        }

        remote_host_credential_saved_.insert(host);
        ++codex_environment_generation_;
        save_ui_state();
        return true;
    }

    bool prompt_for_remote_host_credential(
        const std::string& host
    ) {
        Gtk::Dialog dialog(
            "Remote Shield credential",
            *this,
            true);
        dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
        dialog.add_button("Save password", Gtk::RESPONSE_OK);
        dialog.set_default_response(Gtk::RESPONSE_OK);

        Gtk::Label explanation(
            "Enter the sudo password for " + host +
            ". It will be stored in Ubuntu's encrypted keyring.\n"
            "SSH login itself must use your normal SSH key.");
        explanation.set_xalign(0.0F);
        explanation.set_line_wrap(true);
        Gtk::Entry password_entry;
        password_entry.set_visibility(false);
        password_entry.set_activates_default(true);
        password_entry.set_placeholder_text(
            "Remote sudo password");

        auto* area = dialog.get_content_area();
        area->set_spacing(8);
        area->set_border_width(12);
        area->pack_start(
            explanation,
            Gtk::PACK_SHRINK);
        area->pack_start(
            password_entry,
            Gtk::PACK_SHRINK);
        dialog.show_all();
        password_entry.grab_focus();

        if (dialog.run() != Gtk::RESPONSE_OK) {
            password_entry.set_text("");
            return false;
        }

        std::string password =
            password_entry.get_text().raw();
        password_entry.set_text("");
        const bool saved =
            save_remote_host_credential(
                host,
                password);
        std::fill(password.begin(), password.end(), '\0');
        return saved;
    }

    void manage_remote_host_credential(
        const std::string& host
    ) {
        const bool saved =
            remote_host_credential_saved_.find(host) !=
            remote_host_credential_saved_.end();
        Gtk::Dialog dialog(
            "Remote Shield credential",
            *this,
            true);
        dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);

        constexpr int kRemoveCredentialResponse = 1;

        if (saved) {
            dialog.add_button(
                "Remove saved password",
                kRemoveCredentialResponse);
        }

        dialog.add_button(
            saved ? "Replace password" : "Save password",
            Gtk::RESPONSE_OK);
        dialog.set_default_response(Gtk::RESPONSE_OK);

        Gtk::Label explanation(
            saved
                ? "A sudo password for " + host +
                    " is saved in Ubuntu's encrypted keyring."
                : "No sudo password is saved for " + host + ".");
        explanation.set_xalign(0.0F);
        explanation.set_line_wrap(true);
        Gtk::Entry password_entry;
        password_entry.set_visibility(false);
        password_entry.set_activates_default(true);
        password_entry.set_placeholder_text(
            saved
                ? "New remote sudo password"
                : "Remote sudo password");
        auto* area = dialog.get_content_area();
        area->set_spacing(8);
        area->set_border_width(12);
        area->pack_start(
            explanation,
            Gtk::PACK_SHRINK);
        area->pack_start(
            password_entry,
            Gtk::PACK_SHRINK);
        dialog.show_all();
        password_entry.grab_focus();

        const int response = dialog.run();

        if (response == kRemoveCredentialResponse) {
            std::string error;

            if (!SecretStore::clear_remote_sudo_password(
                    host,
                    error)) {
                Gtk::MessageDialog failure(
                    *this,
                    "The saved credential was not removed.",
                    false,
                    Gtk::MESSAGE_ERROR,
                    Gtk::BUTTONS_OK,
                    true);
                failure.set_secondary_text(error);
                failure.run();
                return;
            }

            remote_host_credential_saved_.erase(host);

            for (auto& selected :
                 thread_remote_shield_hosts_) {
                selected.second.erase(host);
            }

            ++codex_environment_generation_;
            save_ui_state();
            refresh_remote_hosts_panel();
            return;
        }

        if (response == Gtk::RESPONSE_OK) {
            std::string password =
                password_entry.get_text().raw();
            password_entry.set_text("");
            save_remote_host_credential(
                host,
                password);
            std::fill(password.begin(), password.end(), '\0');
            refresh_remote_hosts_panel();
        }
    }

    void add_remote_host() {
        Gtk::Dialog dialog(
            "Add Remote Shield computer",
            *this,
            true);
        dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
        dialog.add_button("Save", Gtk::RESPONSE_OK);
        dialog.set_default_response(Gtk::RESPONSE_OK);

        Gtk::Label host_label(
            "SSH destination or alias");
        host_label.set_xalign(0.0F);
        Gtk::Entry host_entry;
        host_entry.set_placeholder_text(
            "example: alien or ronpatrick@192.168.0.130");
        Gtk::Label password_label(
            "Remote sudo password");
        password_label.set_xalign(0.0F);
        Gtk::Entry password_entry;
        password_entry.set_visibility(false);
        password_entry.set_activates_default(true);

        auto* area = dialog.get_content_area();
        area->set_spacing(6);
        area->set_border_width(12);
        area->pack_start(host_label, Gtk::PACK_SHRINK);
        area->pack_start(host_entry, Gtk::PACK_SHRINK);
        area->pack_start(password_label, Gtk::PACK_SHRINK);
        area->pack_start(password_entry, Gtk::PACK_SHRINK);
        dialog.show_all();
        host_entry.grab_focus();

        if (dialog.run() != Gtk::RESPONSE_OK) {
            password_entry.set_text("");
            return;
        }

        const std::string host =
            trim(host_entry.get_text().raw());
        std::string password =
            password_entry.get_text().raw();
        password_entry.set_text("");

        if (!valid_remote_destination(host)) {
            std::fill(password.begin(), password.end(), '\0');
            Gtk::MessageDialog failure(
                *this,
                "Enter a valid SSH destination.",
                false,
                Gtk::MESSAGE_ERROR,
                Gtk::BUTTONS_OK,
                true);
            failure.set_secondary_text(
                "Use an SSH alias, hostname, address, or user@host without command-line options.");
            failure.run();
            return;
        }

        if (!save_remote_host_credential(host, password)) {
            std::fill(password.begin(), password.end(), '\0');
            return;
        }

        std::fill(password.begin(), password.end(), '\0');
        remote_host_labels_[host] = host;

        if (!current_thread_id_.empty()) {
            thread_remote_shield_hosts_[
                current_thread_id_].insert(host);
        }

        ++codex_environment_generation_;
        save_ui_state();
        refresh_remote_hosts_panel();
    }

    void handle_remote_host_toggled(
        const std::string& host,
        bool active
    ) {
        if (current_thread_id_.empty()) {
            refresh_remote_hosts_panel();
            return;
        }

        if (
            active &&
            remote_host_credential_saved_.find(host) ==
                remote_host_credential_saved_.end() &&
            !prompt_for_remote_host_credential(host)
        ) {
            refresh_remote_hosts_panel();
            return;
        }

        auto& selected =
            thread_remote_shield_hosts_[
                current_thread_id_];

        if (active) {
            selected.insert(host);
            status_label_.set_text(
                "Remote Shield: " + host +
                " enabled for this thread");
        } else {
            selected.erase(host);
            status_label_.set_text(
                "Remote Shield: " + host +
                " disabled for this thread");
        }

        ++codex_environment_generation_;
        save_ui_state();
        refresh_remote_hosts_panel();
    }

    void refresh_remote_hosts_panel() {
        const auto children =
            remote_hosts_list_.get_children();

        for (Gtk::Widget* child : children) {
            remote_hosts_list_.remove(*child);
        }

        std::vector<std::string> hosts;

        for (const auto& host : remote_host_labels_) {
            hosts.push_back(host.first);
        }

        const auto observed =
            thread_observed_remote_hosts_.find(
                current_thread_id_);

        std::stable_sort(
            hosts.begin(),
            hosts.end(),
            [this, &observed](
                const std::string& left,
                const std::string& right
            ) {
                const bool left_observed =
                    observed !=
                        thread_observed_remote_hosts_.end() &&
                    observed->second.find(left) !=
                        observed->second.end();
                const bool right_observed =
                    observed !=
                        thread_observed_remote_hosts_.end() &&
                    observed->second.find(right) !=
                        observed->second.end();

                if (left_observed != right_observed) {
                    return left_observed;
                }

                return
                    Glib::ustring(
                        remote_host_labels_.at(left))
                        .casefold() <
                    Glib::ustring(
                        remote_host_labels_.at(right))
                        .casefold();
            });

        if (hosts.empty()) {
            auto* empty = Gtk::manage(
                new Gtk::Label(
                    "No SSH computers are configured.\n\nUse + to add one. Hosts from ~/.ssh/config also appear here."));
            empty->set_xalign(0.0F);
            empty->set_line_wrap(true);
            remote_hosts_list_.pack_start(
                *empty,
                Gtk::PACK_SHRINK);
        }

        for (const auto& host : hosts) {
            auto* row = Gtk::manage(
                new Gtk::Box(
                    Gtk::ORIENTATION_HORIZONTAL));
            row->set_spacing(4);
            auto* enabled = Gtk::manage(
                new Gtk::CheckButton(
                    remote_host_labels_.at(host)));
            enabled->set_hexpand(true);
            enabled->set_halign(Gtk::ALIGN_FILL);
            const auto selected =
                thread_remote_shield_hosts_.find(
                    current_thread_id_);
            enabled->set_active(
                selected !=
                    thread_remote_shield_hosts_.end() &&
                selected->second.find(host) !=
                    selected->second.end());
            enabled->set_sensitive(
                !current_thread_id_.empty());
            enabled->set_tooltip_text(
                remote_host_credential_saved_.find(host) !=
                        remote_host_credential_saved_.end()
                    ? "Enable this saved remote sudo credential for the current thread"
                    : "No sudo password is saved; enabling will ask for it");
            enabled->signal_toggled().connect(
                [this, host, enabled]() {
                    handle_remote_host_toggled(
                        host,
                        enabled->get_active());
                });

            auto* credential = Gtk::manage(
                new Gtk::Button());
            auto* credential_image = Gtk::manage(
                new Gtk::Image());
            credential_image->set_from_icon_name(
                "dialog-password-symbolic",
                Gtk::ICON_SIZE_BUTTON);
            credential->set_image(*credential_image);
            credential->set_always_show_image(true);
            credential->set_relief(Gtk::RELIEF_NONE);
            credential->set_tooltip_text(
                remote_host_credential_saved_.find(host) !=
                        remote_host_credential_saved_.end()
                    ? "Replace or remove the saved sudo password"
                    : "Save the remote sudo password");
            credential->signal_clicked().connect(
                [this, host]() {
                    manage_remote_host_credential(host);
                });

            row->pack_start(
                *enabled,
                Gtk::PACK_EXPAND_WIDGET);
            row->pack_end(
                *credential,
                Gtk::PACK_SHRINK);
            remote_hosts_list_.pack_start(
                *row,
                Gtk::PACK_SHRINK);
        }

        remote_hosts_list_.show_all_children();

        const bool any_enabled =
            !remote_shield_hosts_for_thread(
                current_thread_id_).empty();
        auto style =
            remote_shield_button_.get_style_context();

        if (any_enabled) {
            style->add_class("remote-shield-active");
        } else {
            style->remove_class("remote-shield-active");
        }
    }

    void apply_remote_hosts_panel_visibility() {
        remote_shield_button_updating_ = true;
        remote_shield_button_.set_active(
            remote_hosts_panel_visible_);
        remote_shield_button_updating_ = false;

        if (remote_hosts_panel_visible_) {
            refresh_remote_hosts_panel();
            remote_hosts_panel_.show_all_children();
            remote_hosts_panel_.show();

            const int allocated_width =
                main_workspace_.get_allocated_width();

            if (allocated_width > 420) {
                main_workspace_.set_position(
                    std::max(
                        180,
                        allocated_width - 310));
            }
        } else {
            remote_hosts_panel_.hide();
        }
    }

    void handle_remote_shield_panel_toggled() {
        if (remote_shield_button_updating_) {
            return;
        }

        remote_hosts_panel_visible_ =
            remote_shield_button_.get_active();
        apply_remote_hosts_panel_visibility();
        save_ui_state();
    }

    static int run_program(
        const std::vector<std::string>& arguments,
        bool quiet
    ) {
        if (arguments.empty()) {
            return -1;
        }

        const pid_t child = ::fork();

        if (child < 0) {
            return -1;
        }

        if (child == 0) {
            if (quiet) {
                const int null_descriptor =
                    ::open("/dev/null", O_WRONLY);

                if (null_descriptor >= 0) {
                    ::dup2(
                        null_descriptor,
                        STDERR_FILENO);
                    ::close(null_descriptor);
                }
            }

            std::vector<char*> values;
            values.reserve(arguments.size() + 1);

            for (const auto& argument : arguments) {
                values.push_back(
                    const_cast<char*>(
                        argument.c_str()));
            }

            values.push_back(nullptr);
            ::execv(values.front(), values.data());
            _exit(127);
        }

        int status = 0;

        while (::waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) {
                return -1;
            }
        }

        return WIFEXITED(status)
            ? WEXITSTATUS(status)
            : -1;
    }

    void update_shield_button() {
        const bool thread_selected =
            !current_thread_id_.empty() &&
            thread_shield_selections_.find(
                current_thread_id_) !=
                thread_shield_selections_.end();
        const bool thread_enabled =
            thread_selected && shield_enabled_;

        shield_button_updating_ = true;
        shield_button_.set_active(thread_enabled);
        shield_button_updating_ = false;

        auto shield_style =
            shield_button_.get_style_context();
        if (thread_enabled) {
            shield_style->add_class("shield-active");
        } else {
            shield_style->remove_class("shield-active");
        }

        shield_button_.set_sensitive(
            !current_thread_id_.empty() &&
            !shield_operation_running_);
        shield_button_.set_tooltip_text(
            thread_enabled
                ? "Shield is enabled for this thread; click to disable sudo for this thread"
                : (
                    shield_enabled_
                        ? "Enable sudo commands for this thread"
                        : "Authenticate once and enable sudo commands for this thread"
                ));
    }

    void update_auto_copy_button() {
        const bool enabled =
            !current_thread_id_.empty() &&
            thread_auto_copy_selections_.find(
                current_thread_id_) !=
                thread_auto_copy_selections_.end();

        auto_copy_button_updating_ = true;
        auto_copy_button_.set_active(enabled);
        auto_copy_button_updating_ = false;
        auto_copy_button_.set_sensitive(
            !current_thread_id_.empty());
        auto_copy_button_.set_tooltip_text(
            enabled
                ? "Auto-copy is enabled for this thread; newly completed shell command blocks copy to the clipboard while this thread is focused"
                : "Automatically copy newly completed shell command blocks while this thread is focused");
    }

    void handle_auto_copy_toggled() {
        if (auto_copy_button_updating_) {
            return;
        }

        if (current_thread_id_.empty()) {
            update_auto_copy_button();
            return;
        }

        if (auto_copy_button_.get_active()) {
            thread_auto_copy_selections_.insert(
                current_thread_id_);
            status_label_.set_text(
                "Codex: command auto-copy enabled for this thread");
        } else {
            thread_auto_copy_selections_.erase(
                current_thread_id_);
            status_label_.set_text(
                "Codex: command auto-copy disabled for this thread");
        }

        save_ui_state();
        update_auto_copy_button();
    }

    void update_pause_button() {
        const bool has_thread =
            !current_thread_id_.empty();
        const bool paused =
            has_thread &&
            paused_threads_.find(current_thread_id_) !=
                paused_threads_.end();
        const bool pause_requested =
            has_thread &&
            pause_requested_threads_.find(
                current_thread_id_) !=
                pause_requested_threads_.end();

        ThreadTurnSession* session =
            has_thread
                ? find_turn_session(current_thread_id_)
                : nullptr;
        const bool busy =
            session != nullptr && session->busy;
        const bool running_turn =
            busy &&
            session->work_kind ==
                SessionWorkKind::Turn &&
            !session->stop_requested;

        pause_image_.set_from_icon_name(
            paused
                ? "media-playback-start-symbolic"
                : "media-playback-pause-symbolic",
            Gtk::ICON_SIZE_BUTTON);

        auto style =
            pause_button_.get_style_context();
        if (paused || pause_requested) {
            style->add_class("pause-active");
        } else {
            style->remove_class("pause-active");
        }

        pause_button_.set_sensitive(
            has_thread &&
            !pause_requested &&
            (
                paused
                    ? !busy
                    : (!busy || running_turn)
            ));

        if (paused) {
            pause_button_.set_tooltip_text(
                "Resume this thread from its saved checkpoint");
        } else if (pause_requested) {
            pause_button_.set_tooltip_text(
                "Codex is preparing a safe checkpoint");
        } else if (running_turn) {
            pause_button_.set_tooltip_text(
                "Ask Codex to reach a safe checkpoint and pause");
        } else {
            pause_button_.set_tooltip_text(
                "Mark this idle thread as paused");
        }
    }

    void handle_pause_clicked() {
        if (current_thread_id_.empty()) {
            return;
        }

        const std::string thread_id =
            current_thread_id_;
        ThreadTurnSession* session =
            find_turn_session(thread_id);

        if (
            paused_threads_.find(thread_id) !=
                paused_threads_.end()
        ) {
            if (session != nullptr && session->busy) {
                status_label_.set_text(
                    "Codex: this paused thread is already busy");
                update_pause_button();
                return;
            }

            paused_threads_.erase(thread_id);
            save_ui_state();
            update_pause_button();

            nlohmann::json turn_input =
                nlohmann::json::array();
            turn_input.push_back(
                {
                    {"type", "text"},
                    {
                        "text",
                        "Continue from the last safe checkpoint and resume the unfinished task."
                    },
                    {
                        "text_elements",
                        nlohmann::json::array()
                    },
                });

            append_user_content_to_transcript(
                turn_input);
            start_structured_turn(
                thread_id,
                turn_input,
                turn_input,
                current_session_options());
            return;
        }

        if (session == nullptr || !session->busy) {
            paused_threads_.insert(thread_id);
            status_label_.set_text(
                "Codex: thread paused at its current checkpoint");
            save_ui_state();
            update_pause_button();
            return;
        }

        if (
            session->work_kind !=
                SessionWorkKind::Turn ||
            session->stop_requested
        ) {
            status_label_.set_text(
                "Codex: this thread cannot pause safely yet");
            update_pause_button();
            return;
        }

        PendingFollowUp follow_up;
        follow_up.entry_id =
            "threaddeck-pause-" +
            std::to_string(++follow_up_sequence_);
        follow_up.input.push_back(
            {
                {"type", "text"},
                {
                    "text",
                    "Find a safe place to pause. Finish only the operation currently in progress so the workspace is left coherent. Record a concise checkpoint of completed work and the exact next step, then end this turn. Do not begin additional work."
                },
                {
                    "text_elements",
                    nlohmann::json::array()
                },
            });

        LiveTurnEntry entry;
        entry.kind = LiveEntryKind::FollowUp;
        entry.item_id = follow_up.entry_id;
        entry.state = "queued";
        entry.text =
            "Pause requested: reach a safe checkpoint and end this turn.";
        session->live_entries.push_back(
            std::move(entry));

        bool accepted = false;
        if (session->active_turn_id.empty()) {
            session->pending_follow_ups.push_back(
                std::move(follow_up));
            accepted = true;
            status_label_.set_text(
                "Codex: safe-pause request queued");
        } else if (send_follow_up(*session, follow_up)) {
            accepted = true;
            status_label_.set_text(
                "Codex: preparing a safe checkpoint");
        } else {
            status_label_.set_text(
                "Codex: safe-pause request could not be sent");
        }

        if (accepted) {
            pause_requested_threads_.insert(thread_id);
        }

        render_live_turn(*session);
        update_pause_button();
        update_send_button_state();
    }

    void start_shield_operation(
        ShieldOperation operation
    ) {
        if (shield_operation_running_) {
            return;
        }

        if (shield_worker_.joinable()) {
            shield_worker_.join();
        }

        const auto control_path =
            shield_control_path();
        const auto executor_path =
            shield_executor_path();
        const auto sudo_path =
            shield_sudo_directory() /
            "sudo";

        if (
            control_path.empty() ||
            !std::filesystem::is_regular_file(
                control_path) ||
            !std::filesystem::is_regular_file(
                executor_path) ||
            !std::filesystem::is_regular_file(
                sudo_path)
        ) {
            shield_operation_running_ = false;
            shield_button_.set_sensitive(false);
            shield_button_.set_tooltip_text(
                "Shield control helper is not available");
            return;
        }

        shield_operation_running_ = true;
        update_shield_button();

        const std::string uid =
            std::to_string(
                static_cast<unsigned long>(
                    ::getuid()));

        if (operation == ShieldOperation::Enable) {
            status_label_.set_text(
                "Shield: waiting for Ubuntu authentication");
        } else if (
            operation == ShieldOperation::Disable
        ) {
            status_label_.set_text(
                "Shield: revoking root authorization");
        }

        shield_worker_ = std::thread(
            [
                this,
                operation,
                control_path,
                executor_path,
                uid
            ]() {
                std::vector<std::string> arguments;
                bool quiet = false;
                int authentication_lock = -1;

                if (operation == ShieldOperation::Enable) {
                    const std::filesystem::path lock_path =
                        std::filesystem::path("/run/user") /
                        uid /
                        "threaddeck-shield-auth.lock";
                    authentication_lock = ::open(
                        lock_path.c_str(),
                        O_WRONLY | O_CREAT | O_CLOEXEC,
                        0600);

                    if (
                        authentication_lock < 0 ||
                        ::flock(
                            authentication_lock,
                            LOCK_EX | LOCK_NB) != 0
                    ) {
                        if (authentication_lock >= 0) {
                            ::close(authentication_lock);
                        }

                        CompletedShieldOperation completed;
                        completed.operation = operation;
                        completed.exit_code = 75;
                        completed.success = false;
                        completed.enabled = false;

                        {
                            std::lock_guard<std::mutex> lock(
                                shield_result_mutex_);
                            pending_shield_results_.push_back(
                                completed);
                        }

                        shield_dispatcher_.emit();
                        return;
                    }

                    arguments = {
                        "/usr/bin/pkexec",
                        control_path.string(),
                        "--enable",
                        uid,
                        executor_path.string(),
                    };
                } else {
                    arguments = {
                        "/usr/bin/sudo",
                        "-n",
                        control_path.string(),
                        operation == ShieldOperation::Disable
                            ? "--disable"
                            : "--status",
                        uid,
                        executor_path.string(),
                    };

                    quiet =
                        operation ==
                            ShieldOperation::Status;
                }

                CompletedShieldOperation completed;
                completed.operation = operation;
                completed.exit_code =
                    run_program(arguments, quiet);

                if (authentication_lock >= 0) {
                    ::flock(authentication_lock, LOCK_UN);
                    ::close(authentication_lock);
                }
                completed.enabled =
                    operation == ShieldOperation::Disable
                        ? completed.exit_code != 0
                        : completed.exit_code == 0;
                completed.success =
                    operation == ShieldOperation::Status
                        ? completed.exit_code == 0 ||
                            completed.exit_code == 1
                        : completed.exit_code == 0;

                {
                    std::lock_guard<std::mutex> lock(
                        shield_result_mutex_);
                    pending_shield_results_.push_back(
                        completed);
                }

                shield_dispatcher_.emit();
            });
    }

    void handle_shield_clicked() {
        if (shield_button_updating_) {
            return;
        }

        if (current_thread_id_.empty()) {
            update_shield_button();
            return;
        }

        const auto selected =
            thread_shield_selections_.find(
                current_thread_id_);

        if (selected != thread_shield_selections_.end()) {
            thread_shield_selections_.erase(selected);
            ++codex_environment_generation_;
            save_ui_state();

            if (
                shield_enabled_ &&
                thread_shield_selections_.empty()
            ) {
                start_shield_operation(
                    ShieldOperation::Disable);
            } else {
                status_label_.set_text(
                    "Shield: sudo disabled for this thread");
                update_shield_button();
            }

            return;
        }

        if (shield_enabled_) {
            thread_shield_selections_.insert(
                current_thread_id_);
            ++codex_environment_generation_;
            status_label_.set_text(
                "Shield: sudo enabled for this thread");
            update_shield_button();
            save_ui_state();
            return;
        }

        shield_operation_thread_id_ =
            current_thread_id_;
        start_shield_operation(
            ShieldOperation::Enable);
    }

    void handle_shield_operation_finished() {
        std::deque<CompletedShieldOperation> completed;

        {
            std::lock_guard<std::mutex> lock(
                shield_result_mutex_);
            completed.swap(
                pending_shield_results_);
        }

        if (shield_worker_.joinable()) {
            shield_worker_.join();
        }

        for (const auto& result : completed) {
            shield_operation_running_ = false;

            if (!result.success) {
                shield_operation_thread_id_.clear();
                update_shield_button();

                if (
                    result.operation !=
                        ShieldOperation::Status
                ) {
                    Gtk::MessageDialog dialog(
                        *this,
                        result.operation ==
                                ShieldOperation::Enable
                            ? "Shield was not enabled."
                            : "Shield could not be disabled.",
                        false,
                        Gtk::MESSAGE_ERROR,
                        Gtk::BUTTONS_OK,
                        true);

                    dialog.set_secondary_text(
                        result.operation ==
                                ShieldOperation::Enable
                            ? "Ubuntu authentication was cancelled or the privileged helper failed."
                            : "The root authorization is still active. Check the terminal output for details.");
                    dialog.run();
                }

                continue;
            }

            const bool shield_changed =
                shield_enabled_ != result.enabled;
            shield_enabled_ = result.enabled;

            if (shield_changed) {
                ++codex_environment_generation_;
            }

            if (
                result.operation ==
                    ShieldOperation::Enable
            ) {
                if (!shield_operation_thread_id_.empty()) {
                    thread_shield_selections_.insert(
                        shield_operation_thread_id_);
                }
                shield_operation_thread_id_.clear();

                status_label_.set_text(
                    "Shield: sudo enabled for this thread");
            } else if (
                result.operation ==
                    ShieldOperation::Disable
            ) {
                thread_shield_selections_.clear();
                shield_operation_thread_id_.clear();

                status_label_.set_text(
                    "Shield: sudo authorization disabled");
            }

            update_shield_button();
            refresh_session_controls();
            save_ui_state();
        }
    }

    void load_splunk_token() {
        splunk_token_.clear();

        if (!splunk_environment_managed_) {
            return;
        }

        std::string error;

        if (!SecretStore::load_splunk_token(
                splunk_token_,
                error)) {
            std::cerr
                << "FAIL: could not load the Splunk token "
                << "from Ubuntu's keyring: "
                << error
                << '\n';
        }
    }

    bool save_splunk_settings(
        const std::string& requested_host,
        const std::string& requested_token,
        bool remove,
        std::string& error
    ) {
        error.clear();

        if (remove) {
            if (!SecretStore::clear_splunk_token(error)) {
                return false;
            }

            splunk_environment_managed_ = true;
            splunk_host_.clear();
            splunk_token_.clear();
            ++codex_environment_generation_;
            save_ui_state();

            std::cout
                << "PASS: removed saved Splunk environment "
                << "without printing secret values\n";
            return true;
        }

        const std::string host = trim(requested_host);

        if (host.empty()) {
            error = "Enter the Splunk host URL.";
            return false;
        }

        std::string token = requested_token;

        if (token.empty()) {
            token = splunk_token_;
        }

        if (token.empty()) {
            error = "Enter the Splunk token.";
            return false;
        }

        if (
            !requested_token.empty() &&
            !SecretStore::save_splunk_token(
                requested_token,
                error)
        ) {
            return false;
        }

        splunk_environment_managed_ = true;
        splunk_host_ = host;
        splunk_token_ = std::move(token);
        ++codex_environment_generation_;
        save_ui_state();

        std::cout
            << "PASS: saved Splunk host and keyring token "
            << "without printing secret values\n";
        return true;
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

            selected_project_id_ =
                state.value(
                    "selectedProjectId",
                    std::string{});

            const std::string saved_project_sort =
                state.value(
                    "projectSort",
                    std::string{"updated-desc"});

            if (valid_sort_id(saved_project_sort)) {
                sidebar_project_sort_ = saved_project_sort;
            }

            if (
                state.contains("projectThreadSorts") &&
                state["projectThreadSorts"].is_object()
            ) {
                const auto saved =
                    state["projectThreadSorts"].get<
                        std::map<std::string, std::string>>();

                for (const auto& entry : saved) {
                    if (valid_sort_id(entry.second)) {
                        project_thread_sorts_[entry.first] =
                            entry.second;
                    }
                }
            }

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

            splunk_environment_managed_ =
                state.value(
                    "splunkEnvironmentManaged",
                    false);

            splunk_host_ =
                state.value(
                    "splunkHost",
                    std::string{});

            sidebar_visible_ =
                state.value(
                    "sidebarVisible",
                    true);

            sidebar_width_ =
                std::max(
                    state.value(
                        "sidebarWidth",
                        260),
                    kMinimumPaneWidth);

            context_panel_visible_ =
                state.value(
                    "contextPanelVisible",
                    true);

            remote_hosts_panel_visible_ =
                state.value(
                    "remoteHostsPanelVisible",
                    false);

            context_panel_width_ =
                std::clamp(
                    state.value(
                        "contextPanelWidth",
                        320),
                    kMinimumPaneWidth,
                    600);

            if (!is_known_theme_id(theme_id_)) {
                theme_id_ = "system";
            }

            selected_project_folders_.clear();
            project_paths_.clear();

            if (
                state.contains("projectPaths") &&
                state["projectPaths"].is_object()
            ) {
                project_paths_ =
                    state["projectPaths"].get<
                        std::map<std::string, std::string>>();
            }

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
                        const std::string project_id =
                            folder.get<std::string>();
                        selected_project_folders_.push_back(
                            project_id);
                        if (
                            project_paths_.find(project_id) ==
                            project_paths_.end()
                        ) {
                            project_paths_[project_id] =
                                project_id;
                        }
                    }
                }
            }

            if (
                !selected_folder_path_.empty() &&
                primary_project_id_for_cwd(
                    selected_folder_path_).empty()
            ) {
                selected_project_folders_.push_back(
                    selected_folder_path_);
                project_paths_[selected_folder_path_] =
                    selected_folder_path_;
            }

            if (
                selected_project_id_.empty() ||
                std::find(
                    selected_project_folders_.begin(),
                    selected_project_folders_.end(),
                    selected_project_id_) ==
                    selected_project_folders_.end()
            ) {
                selected_project_id_ =
                    primary_project_id_for_cwd(
                        selected_folder_path_);
            }

            collapsed_project_folders_.clear();

            if (
                state.contains("collapsedProjectFolders") &&
                state["collapsedProjectFolders"].is_array()
            ) {
                for (
                    const auto& folder :
                    state["collapsedProjectFolders"]
                ) {
                    if (
                        folder.is_string() &&
                        !folder.get<std::string>().empty()
                    ) {
                        collapsed_project_folders_.insert(
                            folder.get<std::string>());
                    }
                }
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

            if (
                state.contains("threadAccessSelections") &&
                state["threadAccessSelections"].is_object()
            ) {
                const auto saved =
                    state["threadAccessSelections"].get<
                        std::map<
                            std::string,
                            std::string>>();

                for (const auto& entry : saved) {
                    if (
                        entry.second == "configured" ||
                        entry.second == "yolo"
                    ) {
                        thread_access_selections_[
                            entry.first] = entry.second;
                    } else if (entry.second == "shield") {
                        thread_access_selections_[
                            entry.first] = "configured";
                        thread_shield_selections_.insert(
                            entry.first);
                    }
                }
            }

            if (
                state.contains("threadShieldSelections") &&
                state["threadShieldSelections"].is_array()
            ) {
                for (const auto& thread_id :
                     state["threadShieldSelections"]) {
                    if (
                        thread_id.is_string() &&
                        !thread_id.get<std::string>().empty()
                    ) {
                        thread_shield_selections_.insert(
                            thread_id.get<std::string>());
                    }
                }
            }

            if (
                state.contains("threadAutoCopySelections") &&
                state["threadAutoCopySelections"].is_array()
            ) {
                for (const auto& thread_id :
                     state["threadAutoCopySelections"]) {
                    if (
                        thread_id.is_string() &&
                        !thread_id.get<std::string>().empty()
                    ) {
                        thread_auto_copy_selections_.insert(
                            thread_id.get<std::string>());
                    }
                }
            }

            if (
                state.contains("remoteHostLabels") &&
                state["remoteHostLabels"].is_object()
            ) {
                remote_host_labels_ =
                    state["remoteHostLabels"].get<
                        std::map<std::string, std::string>>();
            }

            if (
                state.contains("remoteHostCredentialSaved") &&
                state["remoteHostCredentialSaved"].is_array()
            ) {
                remote_host_credential_saved_ =
                    state["remoteHostCredentialSaved"].get<
                        std::set<std::string>>();
            }

            if (
                state.contains("threadRemoteShieldHosts") &&
                state["threadRemoteShieldHosts"].is_object()
            ) {
                thread_remote_shield_hosts_ =
                    state["threadRemoteShieldHosts"].get<
                        std::map<
                            std::string,
                            std::set<std::string>>>();
            }

            if (
                state.contains("threadObservedRemoteHosts") &&
                state["threadObservedRemoteHosts"].is_object()
            ) {
                thread_observed_remote_hosts_ =
                    state["threadObservedRemoteHosts"].get<
                        std::map<
                            std::string,
                            std::set<std::string>>>();
            }

            if (
                state.contains("pausedThreads") &&
                state["pausedThreads"].is_array()
            ) {
                for (const auto& thread_id :
                     state["pausedThreads"]) {
                    if (
                        thread_id.is_string() &&
                        !thread_id.get<std::string>().empty()
                    ) {
                        paused_threads_.insert(
                            thread_id.get<std::string>());
                    }
                }
            }

            if (
                state.contains("threadModelSelections") &&
                state["threadModelSelections"].is_object()
            ) {
                thread_model_selections_ =
                    state["threadModelSelections"].get<
                        std::map<std::string, std::string>>();
            }

            if (
                state.contains("threadReasoningSelections") &&
                state["threadReasoningSelections"].is_object()
            ) {
                thread_reasoning_selections_ =
                    state["threadReasoningSelections"].get<
                        std::map<std::string, std::string>>();
            }

            if (
                state.contains("threadProjectAssignments") &&
                state["threadProjectAssignments"].is_object()
            ) {
                thread_project_assignments_ =
                    state["threadProjectAssignments"].get<
                        std::map<std::string, std::string>>();
            }

            if (
                state.contains("movedThreadSummaries") &&
                state["movedThreadSummaries"].is_object()
            ) {
                moved_thread_summaries_ =
                    state["movedThreadSummaries"].get<
                        std::map<std::string, nlohmann::json>>();
            }

            if (
                state.contains("threadConfiguredApprovalPolicies") &&
                state["threadConfiguredApprovalPolicies"].is_object()
            ) {
                thread_configured_approval_policies_ =
                    state["threadConfiguredApprovalPolicies"].get<
                        std::map<
                            std::string,
                            nlohmann::json>>();
            }

            if (
                state.contains("threadConfiguredSandboxPolicies") &&
                state["threadConfiguredSandboxPolicies"].is_object()
            ) {
                thread_configured_sandbox_policies_ =
                    state["threadConfiguredSandboxPolicies"].get<
                        std::map<
                            std::string,
                            nlohmann::json>>();
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

            const int saved_sidebar_width =
                sidebar_width_;

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
                            kMinimumPaneWidth,
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
                    "selectedProjectId",
                    selected_project_id_,
                },
                {
                    "projectSort",
                    sidebar_project_sort_,
                },
                {
                    "projectThreadSorts",
                    project_thread_sorts_,
                },
                {
                    "projectFolders",
                    selected_project_folders_,
                },
                {
                    "projectPaths",
                    project_paths_,
                },
                {
                    "collapsedProjectFolders",
                    collapsed_project_folders_,
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
                    "threadAccessSelections",
                    thread_access_selections_,
                },
                {
                    "threadModelSelections",
                    thread_model_selections_,
                },
                {
                    "threadReasoningSelections",
                    thread_reasoning_selections_,
                },
                {
                    "threadShieldSelections",
                    thread_shield_selections_,
                },
                {
                    "threadAutoCopySelections",
                    thread_auto_copy_selections_,
                },
                {
                    "remoteHostLabels",
                    remote_host_labels_,
                },
                {
                    "remoteHostCredentialSaved",
                    remote_host_credential_saved_,
                },
                {
                    "threadRemoteShieldHosts",
                    thread_remote_shield_hosts_,
                },
                {
                    "threadObservedRemoteHosts",
                    thread_observed_remote_hosts_,
                },
                {
                    "pausedThreads",
                    paused_threads_,
                },
                {
                    "threadProjectAssignments",
                    thread_project_assignments_,
                },
                {
                    "movedThreadSummaries",
                    moved_thread_summaries_,
                },
                {
                    "threadConfiguredApprovalPolicies",
                    thread_configured_approval_policies_,
                },
                {
                    "threadConfiguredSandboxPolicies",
                    thread_configured_sandbox_policies_,
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
                    "splunkEnvironmentManaged",
                    splunk_environment_managed_,
                },
                {
                    "splunkHost",
                    splunk_host_,
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
                    "remoteHostsPanelVisible",
                    remote_hosts_panel_visible_,
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

    void capture_main_window_geometry() {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;

        get_position(x, y);
        get_size(width, height);

        if (!main_window_maximized_) {
            main_window_x_ = x;
            main_window_y_ = y;
            main_window_width_ = std::max(width, 640);
            main_window_height_ = std::max(height, 480);
            main_window_has_geometry_ = true;
        }

        const auto screen =
            Gdk::Screen::get_default();

        if (screen && width > 0 && height > 0) {
            main_window_monitor_ =
                screen->get_monitor_at_point(
                    x + (width / 2),
                    y + (height / 2));
        }
    }

    bool save_main_window_state_after_delay() {
        main_window_state_save_pending_ = false;
        capture_main_window_geometry();
        save_ui_state();
        return false;
    }

    void schedule_main_window_state_save() {
        if (main_window_state_save_pending_) {
            return;
        }

        main_window_state_save_pending_ = true;
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::save_main_window_state_after_delay),
            500);
    }

    bool handle_main_window_configure(
        GdkEventConfigure* event
    ) {
        if (event == nullptr) {
            return false;
        }

        if (!main_window_maximized_) {
            main_window_x_ =
                event->x;
            main_window_y_ =
                event->y;
            main_window_width_ =
                event->width;
            main_window_height_ =
                event->height;
            main_window_has_geometry_ = true;
        }

        const auto screen =
            Gdk::Screen::get_default();

        if (screen) {
            main_window_monitor_ =
                screen->get_monitor_at_point(
                    event->x +
                        (event->width / 2),
                    event->y +
                        (event->height / 2));
        }

        schedule_main_window_state_save();

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

        capture_main_window_geometry();
        schedule_main_window_state_save();

        return false;
    }

    bool handle_main_window_delete(
        GdkEventAny*
    ) {
        capture_main_window_geometry();
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

.session-controls {
    padding: 2px 4px 6px 4px;
}

.session-control-label {
    opacity: 0.72;
    font-size: 0.92em;
}

.session-controls combobox {
    min-width: 108px;
}

.usage-strip {
    opacity: 0.82;
    padding-left: 10px;
}

.agents-dirty-label {
    font-weight: bold;
}

.agents-file-row {
    padding: 5px 7px;
}

.agents-file-row-active {
    background-color: alpha(@theme_selected_bg_color, 0.20);
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

.shield-button:checked,
.shield-button.shield-active {
    background-color: alpha(#c94f4f, 0.32);
    color: #ffdede;
    border-color: alpha(#ef7777, 0.58);
}

.shield-button:checked:hover,
.shield-button.shield-active:hover {
    background-color: alpha(#c94f4f, 0.40);
}

.auto-copy-button:checked {
    background-image: none;
    background-color: #d5a000;
    color: #181300;
    border-color: #ffd45c;
}

.auto-copy-button:checked:hover {
    background-color: #e4af00;
}

.thread-pause-button.pause-active {
    background-image: none;
    background-color: alpha(#d5a000, 0.38);
    color: #ffe7a3;
    border-color: alpha(#ffd45c, 0.68);
}

.thread-pause-button.pause-active:hover {
    background-color: alpha(#d5a000, 0.50);
}

.remote-shield-button.remote-shield-active {
    background-image: none;
    background-color: alpha(#c94f4f, 0.32);
    color: #ffdede;
    border-color: alpha(#ef7777, 0.58);
}

.remote-shield-button.remote-shield-active:hover {
    background-color: alpha(#c94f4f, 0.40);
}

.remote-hosts-panel {
    background-color: shade(@theme_bg_color, 0.96);
    border-left: 1px solid alpha(@theme_fg_color, 0.14);
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

#sidebar-thread-search {
    background-image: none;
    background-color: @theme_base_color;
    color: @theme_text_color;
    border: 1px solid alpha(@theme_fg_color, 0.22);
    border-radius: 9px;
    box-shadow: none;
}

#sidebar-thread-search:focus {
    border-color: alpha(@theme_selected_bg_color, 0.72);
    box-shadow: 0 0 0 1px alpha(@theme_selected_bg_color, 0.24);
}

#sidebar-thread-search image {
    color: alpha(@theme_text_color, 0.78);
}

#sidebar-thread-scroll scrollbar {
    background-color: transparent;
}

#sidebar-thread-scroll scrollbar trough {
    background-color: alpha(@theme_fg_color, 0.055);
    border: 1px solid alpha(@theme_fg_color, 0.10);
    border-radius: 999px;
}

#sidebar-thread-scroll scrollbar slider {
    min-width: 8px;
    min-height: 32px;
    background-color: alpha(@theme_fg_color, 0.32);
    border: 1px solid alpha(@theme_fg_color, 0.12);
    border-radius: 999px;
}

#sidebar-thread-scroll scrollbar slider:hover {
    background-color: alpha(@theme_selected_bg_color, 0.62);
}

#sidebar-thread-scroll scrollbar slider:active {
    background-color: @theme_selected_bg_color;
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

.thread-row.working-thread {
    background-image: none;
    background-color: rgba(205, 159, 45, 0.18);
    border-color: rgba(205, 159, 45, 0.55);
}

.thread-row.working-thread:hover {
    background-color: rgba(205, 159, 45, 0.26);
}

.thread-row.question-thread {
    background-image: none;
    background-color: rgba(205, 159, 45, 0.14);
    border-color: rgba(205, 159, 45, 0.62);
    transition: background-color 320ms ease-in-out;
}

.thread-row.question-thread.question-blink {
    background-color: rgba(225, 179, 55, 0.34);
}

.thread-row.completed-thread {
    background-image: none;
    background-color: rgba(55, 158, 91, 0.16);
    border-color: rgba(55, 158, 91, 0.46);
}

.thread-row.completed-thread:hover {
    background-color: rgba(55, 158, 91, 0.24);
}

.working-spinner {
    color: #d6a535;
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

    static Gdk::RGBA text_color(
        const char* specification
    ) {
        Gdk::RGBA color;
        color.set(specification);
        return color;
    }

    bool text_renderer_uses_dark_palette() const {
        const ThemePalette* palette =
            find_theme_palette(theme_id_);

        if (palette != nullptr) {
            const Gdk::RGBA background =
                text_color(palette->view_bg_color);

            const double luminance =
                (0.2126 * background.get_red()) +
                (0.7152 * background.get_green()) +
                (0.0722 * background.get_blue());

            return luminance < 0.5;
        }

        const Gdk::RGBA foreground =
            transcript_.get_style_context()->get_color(
                Gtk::STATE_FLAG_NORMAL);

        const double luminance =
            (0.2126 * foreground.get_red()) +
            (0.7152 * foreground.get_green()) +
            (0.0722 * foreground.get_blue());

        return luminance > 0.58;
    }

    void initialize_text_tags() {
        const auto transcript_buffer =
            transcript_.get_buffer();
        const auto prompt_buffer =
            prompt_.get_buffer();

        transcript_user_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-user-heading");
        transcript_codex_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-codex-heading");
        transcript_commentary_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-commentary-heading");
        transcript_reasoning_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-reasoning-heading");
        transcript_activity_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-activity-heading");
        transcript_error_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-error-heading");
        transcript_section_heading_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-section-heading");
        transcript_user_section_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-user-section");
        transcript_user_marker_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-user-marker");
        transcript_user_top_padding_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-user-top-padding");
        transcript_user_bottom_padding_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-user-bottom-padding");
        transcript_expand_activity_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-expand-activity");
        transcript_expand_token_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-expand-token");
        transcript_code_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-code");
        transcript_code_header_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-code-header");
        transcript_code_copy_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-code-copy");
        transcript_markdown_marker_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-markdown-marker");
        transcript_markdown_heading_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-markdown-heading");
        transcript_markdown_bold_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-markdown-bold");
        transcript_markdown_inline_code_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-markdown-inline-code");
        transcript_markdown_quote_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-markdown-quote");
        transcript_markdown_list_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-markdown-list");
        transcript_markdown_link_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-markdown-link");
        transcript_code_keyword_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-code-keyword");
        transcript_code_string_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-code-string");
        transcript_code_comment_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-code-comment");
        transcript_code_number_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-code-number");
        transcript_diff_add_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-diff-add");
        transcript_diff_delete_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-diff-delete");
        transcript_diff_header_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-diff-header");
        transcript_command_tag_ =
            transcript_buffer->create_tag(
                "threaddeck-command");
        prompt_pasted_tag_ =
            prompt_buffer->create_tag(
                "threaddeck-pasted-input");

        for (
            const auto& tag :
            {
                transcript_user_tag_,
                transcript_codex_tag_,
                transcript_commentary_tag_,
                transcript_reasoning_tag_,
                transcript_activity_tag_,
                transcript_error_tag_,
            }
        ) {
            tag->property_weight() =
                Pango::WEIGHT_BOLD;
        }

        for (
            const auto& tag :
            {
                transcript_code_tag_,
                transcript_code_header_tag_,
                transcript_code_copy_tag_,
                transcript_code_keyword_tag_,
                transcript_code_string_tag_,
                transcript_code_comment_tag_,
                transcript_code_number_tag_,
                transcript_diff_add_tag_,
                transcript_diff_delete_tag_,
                transcript_diff_header_tag_,
                transcript_command_tag_,
            }
        ) {
            tag->property_family() = "monospace";
        }

        transcript_diff_add_tag_
            ->property_background_full_height() = true;
        transcript_diff_delete_tag_
            ->property_background_full_height() = true;
        transcript_section_heading_tag_
            ->property_pixels_above_lines() = 12;
        transcript_section_heading_tag_
            ->property_pixels_below_lines() = 3;
        transcript_user_section_tag_
            ->property_left_margin() = 10;
        transcript_user_section_tag_
            ->property_right_margin() = 10;
        // GTK 3 can abort in its mouse-position mapping when a text
        // layout contains invisible tagged runs. Keep internal markers
        // in the normal layout, but make them visually negligible.
        transcript_user_marker_tag_
            ->property_scale() = 0.01;
        transcript_user_top_padding_tag_
            ->property_pixels_above_lines() = 10;
        transcript_user_bottom_padding_tag_
            ->property_pixels_below_lines() = 10;
        transcript_expand_activity_tag_
            ->property_underline() =
                Pango::UNDERLINE_SINGLE;
        transcript_expand_token_tag_
            ->property_scale() = 0.01;
        transcript_expand_activity_tag_
            ->signal_event().connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::handle_activity_expand_event));
        transcript_code_copy_tag_
            ->property_weight() =
                Pango::WEIGHT_NORMAL;
        transcript_code_copy_tag_
            ->property_family() = "sans";
        transcript_code_copy_tag_
            ->property_style() =
                Pango::STYLE_NORMAL;
        transcript_code_copy_tag_
            ->property_scale() = 1.1;
        transcript_code_copy_tag_
            ->property_underline() =
                Pango::UNDERLINE_NONE;
        transcript_code_copy_tag_
            ->signal_event().connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::handle_code_copy_event));
        transcript_code_header_tag_
            ->property_style() =
                Pango::STYLE_NORMAL;
        transcript_code_header_tag_
            ->property_weight() =
                Pango::WEIGHT_BOLD;
        transcript_markdown_marker_tag_
            ->property_scale() = 0.01;
        transcript_markdown_heading_tag_
            ->property_weight() =
                Pango::WEIGHT_BOLD;
        transcript_markdown_heading_tag_
            ->property_scale() = 1.12;
        transcript_markdown_heading_tag_
            ->property_pixels_above_lines() = 8;
        transcript_markdown_heading_tag_
            ->property_pixels_below_lines() = 3;
        transcript_markdown_bold_tag_
            ->property_weight() =
                Pango::WEIGHT_BOLD;
        transcript_markdown_inline_code_tag_
            ->property_family() = "monospace";
        transcript_markdown_quote_tag_
            ->property_left_margin() = 16;
        transcript_markdown_quote_tag_
            ->property_style() =
                Pango::STYLE_ITALIC;
        transcript_markdown_list_tag_
            ->property_left_margin() = 16;
        transcript_markdown_link_tag_
            ->property_underline() =
                Pango::UNDERLINE_SINGLE;
        transcript_markdown_link_tag_
            ->signal_event().connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::handle_markdown_link_event));

        refresh_text_tag_colors();
        apply_transcript_tags(0);
    }

    void refresh_text_tag_colors() {
        if (!transcript_user_tag_ || !prompt_pasted_tag_) {
            return;
        }

        const bool dark =
            text_renderer_uses_dark_palette();

        const ThemePalette* palette =
            find_theme_palette(theme_id_);

        transcript_user_tag_->property_foreground_rgba() =
            palette != nullptr
                ? text_color(palette->accent_color)
                : text_color(
                    dark ? "#79c0ff" : "#245f91");
        transcript_codex_tag_->property_foreground_rgba() =
            text_color(dark ? "#7ee787" : "#267344");
        transcript_commentary_tag_->property_foreground_rgba() =
            text_color(dark ? "#79c0ff" : "#256d94");
        transcript_reasoning_tag_->property_foreground_rgba() =
            text_color(dark ? "#d2a8ff" : "#75519a");
        transcript_activity_tag_->property_foreground_rgba() =
            text_color(dark ? "#e3b341" : "#8a5b00");
        transcript_expand_activity_tag_
            ->property_foreground_rgba() =
            text_color(dark ? "#79c0ff" : "#245f91");
        const Gdk::RGBA transparent_text =
            text_color("rgba(0, 0, 0, 0)");
        transcript_user_marker_tag_
            ->property_foreground_rgba() =
            transparent_text;
        transcript_expand_token_tag_
            ->property_foreground_rgba() =
            transparent_text;
        transcript_markdown_marker_tag_
            ->property_foreground_rgba() =
            transparent_text;
        transcript_error_tag_->property_foreground_rgba() =
            text_color(dark ? "#ff7b72" : "#b42318");
        transcript_user_section_tag_
            ->property_paragraph_background_rgba() =
            text_color(
                dark
                    ? "rgba(88, 116, 150, 0.20)"
                    : "rgba(72, 112, 150, 0.11)");

        transcript_code_tag_->property_foreground_rgba() =
            text_color(dark ? "#c9d1d9" : "#263238");
        transcript_code_tag_
            ->property_paragraph_background_rgba() =
            text_color(
                dark
                    ? "rgba(255, 255, 255, 0.035)"
                    : "rgba(30, 41, 59, 0.045)");
        transcript_code_header_tag_
            ->property_foreground_rgba() =
            text_color(dark ? "#c9d1d9" : "#263238");
        transcript_code_header_tag_
            ->property_paragraph_background_rgba() =
            text_color(
                dark
                    ? "rgba(255, 255, 255, 0.09)"
                    : "rgba(30, 41, 59, 0.10)");
        transcript_code_copy_tag_
            ->property_foreground_rgba() =
            text_color(dark ? "#79c0ff" : "#245f91");
        transcript_code_copy_tag_
            ->property_background_rgba() =
            text_color(
                dark
                    ? "rgba(121, 192, 255, 0.12)"
                    : "rgba(36, 95, 145, 0.10)");
        transcript_markdown_inline_code_tag_
            ->property_foreground_rgba() =
            text_color(dark ? "#f0b8ff" : "#704080");
        transcript_markdown_inline_code_tag_
            ->property_background_rgba() =
            text_color(
                dark
                    ? "rgba(255, 255, 255, 0.07)"
                    : "rgba(30, 41, 59, 0.08)");
        transcript_markdown_quote_tag_
            ->property_foreground_rgba() =
            text_color(dark ? "#aeb8c5" : "#586474");
        transcript_markdown_link_tag_
            ->property_foreground_rgba() =
            text_color(dark ? "#58a6ff" : "#0969da");
        transcript_code_keyword_tag_->property_foreground_rgba() =
            text_color(dark ? "#d2a8ff" : "#7c3aed");
        transcript_code_string_tag_->property_foreground_rgba() =
            text_color(dark ? "#a5d6ff" : "#087f8c");
        transcript_code_comment_tag_->property_foreground_rgba() =
            text_color(dark ? "#8b949e" : "#667085");
        transcript_code_comment_tag_->property_style() =
            Pango::STYLE_ITALIC;
        transcript_code_number_tag_->property_foreground_rgba() =
            text_color(dark ? "#ffa657" : "#b54708");

        transcript_diff_add_tag_->property_foreground_rgba() =
            text_color(dark ? "#7ee787" : "#176b35");
        transcript_diff_add_tag_->property_background_rgba() =
            text_color(
                dark
                    ? "rgba(35, 134, 54, 0.22)"
                    : "rgba(46, 160, 67, 0.14)");
        transcript_diff_delete_tag_->property_foreground_rgba() =
            text_color(dark ? "#ff7b72" : "#a52820");
        transcript_diff_delete_tag_->property_background_rgba() =
            text_color(
                dark
                    ? "rgba(248, 81, 73, 0.22)"
                    : "rgba(248, 81, 73, 0.13)");
        transcript_diff_header_tag_->property_foreground_rgba() =
            text_color(dark ? "#79c0ff" : "#245f91");
        transcript_command_tag_->property_foreground_rgba() =
            text_color(dark ? "#f2cc60" : "#785800");

        prompt_pasted_tag_->property_foreground_rgba() =
            text_color(dark ? "#9bdcff" : "#256a8a");
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

            << ".remote-hosts-panel {\n"
            << "    background-color: "
            << palette->sidebar_bg_color
            << ";\n"
            << "    color: "
            << palette->foreground_color
            << ";\n"
            << "    border-left: 1px solid "
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

            << "button.shield-button.shield-active,\n"
            << "button.shield-button:checked {\n"
            << "    background-image: none;\n"
            << "    background-color: rgba(139, 48, 48, 0.82);\n"
            << "    color: #ffe7e7;\n"
            << "    border-color: #c96464;\n"
            << "}\n\n"

            << "button.shield-button.shield-active:hover,\n"
            << "button.shield-button:checked:hover {\n"
            << "    background-color: rgba(157, 56, 56, 0.90);\n"
            << "}\n\n"

            << "button.auto-copy-button:checked {\n"
            << "    background-image: none;\n"
            << "    background-color: #d5a000;\n"
            << "    color: #181300;\n"
            << "    border-color: #ffd45c;\n"
            << "}\n\n"

            << "button.auto-copy-button:checked:hover {\n"
            << "    background-color: #e4af00;\n"
            << "}\n\n"

            << "button.thread-pause-button.pause-active {\n"
            << "    background-image: none;\n"
            << "    background-color: rgba(175, 132, 0, 0.48);\n"
            << "    color: #ffe7a3;\n"
            << "    border-color: #d7ad3f;\n"
            << "}\n\n"

            << "button.thread-pause-button.pause-active:hover {\n"
            << "    background-color: rgba(191, 145, 0, 0.58);\n"
            << "}\n\n"

            << "button.remote-shield-button.remote-shield-active {\n"
            << "    background-image: none;\n"
            << "    background-color: rgba(139, 48, 48, 0.82);\n"
            << "    color: #ffe7e7;\n"
            << "    border-color: #c96464;\n"
            << "}\n\n"

            << "button.remote-shield-button.remote-shield-active:hover {\n"
            << "    background-color: rgba(157, 56, 56, 0.90);\n"
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

            << ".thread-row.working-thread {\n"
            << "    background-image: none;\n"
            << "    background-color: rgba(205, 159, 45, 0.18);\n"
            << "    border-color: rgba(205, 159, 45, 0.55);\n"
            << "}\n\n"

            << ".thread-row.working-thread:hover {\n"
            << "    background-color: rgba(205, 159, 45, 0.26);\n"
            << "}\n\n"

            << ".thread-row.question-thread {\n"
            << "    background-image: none;\n"
            << "    background-color: rgba(205, 159, 45, 0.14);\n"
            << "    border-color: rgba(205, 159, 45, 0.62);\n"
            << "    transition: background-color 320ms ease-in-out;\n"
            << "}\n\n"

            << ".thread-row.question-thread.question-blink {\n"
            << "    background-color: rgba(225, 179, 55, 0.34);\n"
            << "}\n\n"

            << ".thread-row.completed-thread {\n"
            << "    background-image: none;\n"
            << "    background-color: rgba(55, 158, 91, 0.16);\n"
            << "    border-color: rgba(55, 158, 91, 0.46);\n"
            << "}\n\n"

            << ".thread-row.completed-thread:hover {\n"
            << "    background-color: rgba(55, 158, 91, 0.24);\n"
            << "}\n\n"

            << ".working-spinner {\n"
            << "    color: #d6a535;\n"
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

            << "#sidebar-thread-search {\n"
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
            << "    box-shadow: none;\n"
            << "}\n\n"

            << "#sidebar-thread-search:focus {\n"
            << "    border-color: "
            << palette->accent_bg_color
            << ";\n"
            << "    box-shadow: 0 0 0 1px alpha("
            << palette->accent_bg_color
            << ", 0.24);\n"
            << "}\n\n"

            << "#sidebar-thread-search image {\n"
            << "    color: alpha("
            << palette->foreground_color
            << ", 0.78);\n"
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
            << "}\n\n"

            << "#sidebar-thread-scroll scrollbar {\n"
            << "    background-color: transparent;\n"
            << "}\n\n"

            << "#sidebar-thread-scroll scrollbar trough {\n"
            << "    background-color: alpha("
            << palette->foreground_color
            << ", 0.055);\n"
            << "    border-color: "
            << palette->scrollbar_outline_color
            << ";\n"
            << "}\n\n"

            << "#sidebar-thread-scroll scrollbar slider {\n"
            << "    background-color: alpha("
            << palette->foreground_color
            << ", 0.34);\n"
            << "    border-color: "
            << palette->scrollbar_outline_color
            << ";\n"
            << "}\n\n"

            << "#sidebar-thread-scroll scrollbar slider:hover {\n"
            << "    background-color: alpha("
            << palette->accent_bg_color
            << ", 0.62);\n"
            << "}\n\n"

            << "#sidebar-thread-scroll scrollbar slider:active {\n"
            << "    background-color: "
            << palette->accent_bg_color
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

        refresh_text_tag_colors();
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

    std::filesystem::path
    clipboard_attachment_directory() {
        if (!clipboard_attachment_directory_.empty()) {
            std::filesystem::create_directories(
                clipboard_attachment_directory_);
            return clipboard_attachment_directory_;
        }

        std::string data_directory =
            Glib::get_user_data_dir();

        if (data_directory.empty()) {
            data_directory =
                Glib::get_user_config_dir();
        }

        clipboard_attachment_directory_ =
            std::filesystem::path(data_directory) /
            "threaddeck" /
            "attachments" /
            "clipboard";

        std::filesystem::create_directories(
            clipboard_attachment_directory_);

        return clipboard_attachment_directory_;
    }

    void remove_temporary_attachment_files(
        std::vector<std::string>& paths
    ) {
        for (const auto& path : paths) {
            std::error_code error;
            std::filesystem::remove(path, error);

            if (error) {
                std::cerr
                    << "FAIL: could not remove temporary "
                    << "clipboard attachment "
                    << path
                    << ": "
                    << error.message()
                    << '\n';
            }
        }

        paths.clear();

        if (!clipboard_attachment_directory_.empty()) {
            std::error_code ignored_error;
            std::filesystem::remove(
                clipboard_attachment_directory_,
                ignored_error);
        }
    }

    void receive_clipboard_image(
        const Glib::RefPtr<Gdk::Pixbuf>& image,
        const std::string& target_thread_id
    ) {
        if (!image) {
            if (target_thread_id == current_thread_id_) {
                status_label_.set_text(
                    "Codex: clipboard image could not be read");
            }
            return;
        }

        if (target_thread_id.empty()) {
            return;
        }

        std::filesystem::path image_path;
        gchar* generated_id =
            g_uuid_string_random();

        const std::string image_id =
            generated_id != nullptr
                ? generated_id
                : std::to_string(
                    std::time(nullptr));

        g_free(generated_id);

        try {
            image_path =
                clipboard_attachment_directory() /
                (
                    "pasted-image-" +
                    image_id +
                    ".png"
                );

            image->save(
                image_path.string(),
                "png");

        } catch (const std::exception& error) {
            if (!image_path.empty()) {
                std::error_code ignored_error;
                std::filesystem::remove(
                    image_path,
                    ignored_error);
            }

            status_label_.set_text(
                "Codex: clipboard image could not be attached");

            std::cerr
                << "FAIL: clipboard image attachment: "
                << error.what()
                << '\n';
            return;
        }

        const std::string path =
            image_path.string();

        if (target_thread_id == composer_thread_id_) {
            attached_image_paths_.push_back(path);
            queued_temporary_attachment_paths_.push_back(path);
            refresh_attachment_row();
            update_send_button_state();

            status_label_.set_text(
                turn_in_progress_
                    ? "Codex: screenshot queued for the follow-up"
                    : "Codex: screenshot queued for the next message");
        } else {
            ComposerDraft& draft =
                composer_drafts_[target_thread_id];
            draft.image_paths.push_back(path);
            draft.temporary_attachment_paths.push_back(path);
        }

        std::cout
            << "PASS: queued clipboard image attachment "
            << path
            << '\n';
    }

    bool paste_clipboard_image() {
        if (
            !current_composer_accepts_attachments()
        ) {
            return false;
        }

        const auto clipboard =
            Gtk::Clipboard::get();

        if (
            !clipboard ||
            !clipboard->wait_is_image_available()
        ) {
            return false;
        }

        status_label_.set_text(
            "Codex: reading screenshot from clipboard");

        const std::string target_thread_id =
            current_thread_id_;
        clipboard->request_image(
            [this, target_thread_id](
                const Glib::RefPtr<Gdk::Pixbuf>& image
            ) {
                receive_clipboard_image(
                    image,
                    target_thread_id);
            });

        return true;
    }

    PromptEditSnapshot capture_prompt_edit_snapshot() {
        PromptEditSnapshot snapshot;
        const auto buffer = prompt_.get_buffer();

        if (!buffer) {
            return snapshot;
        }

        snapshot.text = buffer->get_text();
        snapshot.insert_offset =
            buffer->get_iter_at_mark(
                buffer->get_insert()).get_offset();
        snapshot.selection_bound_offset =
            buffer->get_iter_at_mark(
                buffer->get_selection_bound()).get_offset();

        if (!prompt_pasted_tag_) {
            return snapshot;
        }

        auto position = buffer->begin();
        const auto end = buffer->end();

        while (position != end) {
            if (!position.has_tag(prompt_pasted_tag_)) {
                if (!position.forward_to_tag_toggle(
                        prompt_pasted_tag_)) {
                    break;
                }
            }

            if (
                position == end ||
                !position.has_tag(prompt_pasted_tag_)
            ) {
                continue;
            }

            const int start_offset =
                position.get_offset();
            auto range_end = position;

            if (!range_end.forward_to_tag_toggle(
                    prompt_pasted_tag_)) {
                range_end = end;
            }

            snapshot.pasted_ranges.emplace_back(
                start_offset,
                range_end.get_offset());
            position = range_end;
        }

        return snapshot;
    }

    static bool prompt_snapshots_equal(
        const PromptEditSnapshot& left,
        const PromptEditSnapshot& right
    ) {
        return
            left.text == right.text &&
            left.pasted_ranges == right.pasted_ranges;
    }

    void trim_prompt_history() {
        constexpr std::size_t maximum_history = 200;

        if (prompt_undo_history_.size() > maximum_history) {
            prompt_undo_history_.erase(
                prompt_undo_history_.begin(),
                prompt_undo_history_.begin() +
                    static_cast<std::ptrdiff_t>(
                        prompt_undo_history_.size() -
                        maximum_history));
        }
    }

    void begin_prompt_history_transaction() {
        if (prompt_history_transaction_depth_++ == 0) {
            prompt_history_transaction_start_ =
                capture_prompt_edit_snapshot();
        }
    }

    void end_prompt_history_transaction() {
        if (prompt_history_transaction_depth_ <= 0) {
            prompt_history_transaction_depth_ = 0;
            return;
        }

        if (--prompt_history_transaction_depth_ != 0) {
            return;
        }

        const auto after =
            capture_prompt_edit_snapshot();

        if (!prompt_snapshots_equal(
                prompt_history_transaction_start_,
                after)) {
            prompt_undo_history_.push_back(
                prompt_history_transaction_start_);
            trim_prompt_history();
            prompt_redo_history_.clear();
        }

        prompt_history_current_ = after;
    }

    void restore_prompt_edit_snapshot(
        const PromptEditSnapshot& snapshot
    ) {
        const auto buffer = prompt_.get_buffer();

        if (!buffer) {
            return;
        }

        prompt_history_restoring_ = true;
        buffer->set_text(snapshot.text);

        if (prompt_pasted_tag_) {
            const int character_count =
                buffer->get_char_count();

            for (const auto& range :
                 snapshot.pasted_ranges) {
                const int start_offset =
                    std::clamp(
                        range.first,
                        0,
                        character_count);
                const int end_offset =
                    std::clamp(
                        range.second,
                        start_offset,
                        character_count);
                auto start =
                    buffer->get_iter_at_offset(
                        start_offset);
                auto end =
                    buffer->get_iter_at_offset(
                        end_offset);
                buffer->apply_tag(
                    prompt_pasted_tag_,
                    start,
                    end);
            }
        }

        const int character_count =
            buffer->get_char_count();
        auto insertion = buffer->get_iter_at_offset(
            std::clamp(
                snapshot.insert_offset,
                0,
                character_count));
        auto selection_bound =
            buffer->get_iter_at_offset(
                std::clamp(
                    snapshot.selection_bound_offset,
                    0,
                    character_count));
        buffer->select_range(
            insertion,
            selection_bound);
        prompt_history_restoring_ = false;
        prompt_history_current_ = snapshot;
        prompt_.scroll_to(buffer->get_insert());
    }

    void undo_prompt_edit() {
        if (prompt_undo_history_.empty()) {
            return;
        }

        prompt_redo_history_.push_back(
            capture_prompt_edit_snapshot());
        const auto snapshot =
            prompt_undo_history_.back();
        prompt_undo_history_.pop_back();
        restore_prompt_edit_snapshot(snapshot);
    }

    void redo_prompt_edit() {
        if (prompt_redo_history_.empty()) {
            return;
        }

        prompt_undo_history_.push_back(
            capture_prompt_edit_snapshot());
        trim_prompt_history();
        const auto snapshot =
            prompt_redo_history_.back();
        prompt_redo_history_.pop_back();
        restore_prompt_edit_snapshot(snapshot);
    }

    void reset_prompt_edit_history() {
        prompt_undo_history_.clear();
        prompt_redo_history_.clear();
        prompt_history_transaction_depth_ = 0;
        prompt_history_current_ =
            capture_prompt_edit_snapshot();
    }

    void save_current_composer_draft() {
        if (composer_thread_id_.empty()) {
            return;
        }

        ComposerDraft& draft =
            composer_drafts_[composer_thread_id_];
        draft.current =
            capture_prompt_edit_snapshot();
        draft.undo_history =
            prompt_undo_history_;
        draft.redo_history =
            prompt_redo_history_;
        draft.image_paths =
            attached_image_paths_;
        draft.audio_paths =
            attached_audio_paths_;
        draft.temporary_attachment_paths =
            queued_temporary_attachment_paths_;
    }

    void switch_composer_to_thread(
        const std::string& thread_id
    ) {
        if (composer_thread_id_ == thread_id) {
            return;
        }

        save_current_composer_draft();
        composer_thread_id_ = thread_id;

        PromptEditSnapshot current;
        std::vector<PromptEditSnapshot> undo_history;
        std::vector<PromptEditSnapshot> redo_history;
        std::vector<std::string> image_paths;
        std::vector<std::string> audio_paths;
        std::vector<std::string> temporary_paths;

        const auto saved =
            composer_drafts_.find(thread_id);

        if (
            !thread_id.empty() &&
            saved != composer_drafts_.end()
        ) {
            current = saved->second.current;
            undo_history =
                saved->second.undo_history;
            redo_history =
                saved->second.redo_history;
            image_paths =
                saved->second.image_paths;
            audio_paths =
                saved->second.audio_paths;
            temporary_paths =
                saved->second
                    .temporary_attachment_paths;
        }

        prompt_history_transaction_depth_ = 0;
        restore_prompt_edit_snapshot(current);
        prompt_undo_history_ =
            std::move(undo_history);
        prompt_redo_history_ =
            std::move(redo_history);
        prompt_history_current_ = current;
        attached_image_paths_ =
            std::move(image_paths);
        attached_audio_paths_ =
            std::move(audio_paths);
        queued_temporary_attachment_paths_ =
            std::move(temporary_paths);

        skill_popover_.popdown();
        refresh_attachment_row();
        update_prompt_height();
        update_send_button_state();
    }

    void discard_composer_draft(
        const std::string& thread_id
    ) {
        if (composer_thread_id_ == thread_id) {
            switch_composer_to_thread({});
        }

        const auto draft =
            composer_drafts_.find(thread_id);

        if (draft == composer_drafts_.end()) {
            return;
        }

        remove_temporary_attachment_files(
            draft->second.temporary_attachment_paths);
        composer_drafts_.erase(draft);
    }

    void clear_prompt_after_submission() {
        record_prompt_history(
            composer_thread_id_,
            prompt_.get_buffer()->get_text());
        prompt_history_restoring_ = true;
        prompt_.get_buffer()->set_text("");
        prompt_history_restoring_ = false;
        reset_prompt_edit_history();
        prompt_command_history_navigation_[
            composer_thread_id_] = {};
    }

    void record_prompt_history(
        const std::string& thread_id,
        const Glib::ustring& text
    ) {
        if (
            thread_id.empty() ||
            trim(text.raw()).empty()
        ) {
            return;
        }

        auto& history =
            prompt_command_histories_[thread_id];

        if (history.empty() || history.back() != text) {
            history.push_back(text);
        }

        constexpr std::size_t maximum_entries = 200;
        if (history.size() > maximum_entries) {
            history.erase(
                history.begin(),
                history.begin() +
                    static_cast<std::ptrdiff_t>(
                        history.size() -
                        maximum_entries));
        }
    }

    void seed_prompt_history_from_thread(
        const std::string& thread_id,
        const nlohmann::json& thread
    ) {
        if (
            thread_id.empty() ||
            prompt_history_seeded_threads_.find(thread_id) !=
                prompt_history_seeded_threads_.end() ||
            !thread.is_object() ||
            !thread.contains("turns") ||
            !thread["turns"].is_array()
        ) {
            return;
        }

        prompt_history_seeded_threads_.insert(thread_id);

        const auto locally_recorded =
            prompt_command_histories_[thread_id];
        prompt_command_histories_[thread_id].clear();

        for (const auto& turn : thread["turns"]) {
            if (
                !turn.is_object() ||
                !turn.contains("items") ||
                !turn["items"].is_array()
            ) {
                continue;
            }

            for (const auto& item : turn["items"]) {
                if (
                    !item.is_object() ||
                    json_string_field(item, "type") !=
                        "userMessage" ||
                    !item.contains("content") ||
                    !item["content"].is_array()
                ) {
                    continue;
                }

                std::string text;
                for (const auto& input : item["content"]) {
                    if (
                        !input.is_object() ||
                        json_string_field(input, "type") !=
                            "text"
                    ) {
                        continue;
                    }

                    const std::string part =
                        json_string_field(input, "text");
                    if (part.empty()) {
                        continue;
                    }

                    if (!text.empty()) {
                        text += '\n';
                    }
                    text += part;
                }

                record_prompt_history(
                    thread_id,
                    Glib::ustring(text));
            }
        }

        for (const auto& local_entry : locally_recorded) {
            record_prompt_history(
                thread_id,
                local_entry);
        }
    }

    bool navigate_prompt_history(bool older) {
        if (composer_thread_id_.empty()) {
            return false;
        }

        const auto found =
            prompt_command_histories_.find(
                composer_thread_id_);
        if (
            found == prompt_command_histories_.end() ||
            found->second.empty()
        ) {
            return false;
        }

        auto& navigation =
            prompt_command_history_navigation_[
                composer_thread_id_];
        const auto& history = found->second;
        Glib::ustring replacement;

        if (!navigation.active) {
            if (!older) {
                return false;
            }

            navigation.active = true;
            navigation.draft =
                prompt_.get_buffer()->get_text();
            navigation.index = history.size() - 1;
            replacement = history[navigation.index];
        } else if (older) {
            if (navigation.index > 0) {
                --navigation.index;
            }
            replacement = history[navigation.index];
        } else if (
            navigation.index + 1 < history.size()
        ) {
            ++navigation.index;
            replacement = history[navigation.index];
        } else {
            replacement = navigation.draft;
            navigation = {};
        }

        prompt_history_restoring_ = true;
        const auto buffer = prompt_.get_buffer();
        buffer->set_text(replacement);
        buffer->place_cursor(buffer->end());
        prompt_history_restoring_ = false;
        reset_prompt_edit_history();
        prompt_.scroll_to(buffer->get_insert());
        return true;
    }

    bool paste_clipboard_text_with_style() {
        if (
            !prompt_.get_editable() ||
            !prompt_pasted_tag_
        ) {
            return false;
        }

        const auto clipboard =
            Gtk::Clipboard::get();

        if (!clipboard) {
            return false;
        }

        const Glib::ustring pasted_text =
            clipboard->wait_for_text();

        if (pasted_text.empty()) {
            return false;
        }

        const auto buffer =
            prompt_.get_buffer();
        Gtk::TextBuffer::iterator selection_start;
        Gtk::TextBuffer::iterator selection_end;

        if (
            buffer->get_selection_bounds(
                selection_start,
                selection_end)
        ) {
            begin_prompt_history_transaction();
            buffer->erase(
                selection_start,
                selection_end);
        } else {
            begin_prompt_history_transaction();
        }

        auto insertion =
            buffer->get_iter_at_mark(
                buffer->get_insert());

        pasting_prompt_text_ = true;
        const auto after_paste =
            buffer->insert_with_tag(
                insertion,
                pasted_text,
                prompt_pasted_tag_);
        pasting_prompt_text_ = false;

        buffer->place_cursor(after_paste);
        prompt_.scroll_to(
            buffer->get_insert());
        end_prompt_history_transaction();
        return true;
    }

    void handle_prompt_text_inserted(
        const Gtk::TextBuffer::iterator& position,
        const Glib::ustring& text,
        int
    ) {
        if (
            pasting_prompt_text_ ||
            !prompt_pasted_tag_ ||
            text.empty()
        ) {
            return;
        }

        const auto buffer =
            prompt_.get_buffer();
        const int end_offset =
            position.get_offset();
        const int start_offset =
            std::max(
                0,
                end_offset -
                    static_cast<int>(text.size()));

        auto start =
            buffer->get_iter_at_offset(start_offset);
        auto end =
            buffer->get_iter_at_offset(end_offset);

        buffer->remove_tag(
            prompt_pasted_tag_,
            start,
            end);
    }

    bool on_prompt_key_press(GdkEventKey* event) {
        if (event == nullptr) {
            return false;
        }

        const bool control_pressed =
            (event->state & GDK_CONTROL_MASK) != 0;
        const guint lower_key =
            gdk_keyval_to_lower(event->keyval);

        if (
            control_pressed &&
            lower_key == GDK_KEY_z
        ) {
            if ((event->state & GDK_SHIFT_MASK) != 0) {
                redo_prompt_edit();
            } else {
                undo_prompt_edit();
            }
            return true;
        }

        if (
            control_pressed &&
            lower_key == GDK_KEY_y
        ) {
            redo_prompt_edit();
            return true;
        }

        const bool is_control_v =
            control_pressed &&
            lower_key == GDK_KEY_v;

        if (
            is_control_v &&
            (
                (
                    (event->state & GDK_SHIFT_MASK) == 0 &&
                    paste_clipboard_image()
                ) ||
                paste_clipboard_text_with_style()
            )
        ) {
            return true;
        }

        const bool is_enter =
            event->keyval == GDK_KEY_Return ||
            event->keyval == GDK_KEY_KP_Enter;

        const bool plain_navigation_key =
            !control_pressed &&
            (event->state & GDK_MOD1_MASK) == 0 &&
            (event->state & GDK_SUPER_MASK) == 0 &&
            (event->state & GDK_SHIFT_MASK) == 0;

        if (
            plain_navigation_key &&
            (
                event->keyval == GDK_KEY_Up ||
                event->keyval == GDK_KEY_KP_Up ||
                event->keyval == GDK_KEY_Down ||
                event->keyval == GDK_KEY_KP_Down
            )
        ) {
            const bool older =
                event->keyval == GDK_KEY_Up ||
                event->keyval == GDK_KEY_KP_Up;
            const auto buffer = prompt_.get_buffer();
            const auto insertion =
                buffer->get_iter_at_mark(
                    buffer->get_insert());
            Gtk::TextBuffer::iterator selection_start;
            Gtk::TextBuffer::iterator selection_end;
            const bool has_selection =
                buffer->get_selection_bounds(
                    selection_start,
                    selection_end);
            const bool navigating_history =
                prompt_command_history_navigation_[
                    composer_thread_id_].active;
            auto display_line_probe = insertion;
            const bool at_boundary =
                older
                    ? !prompt_.backward_display_line(
                        display_line_probe)
                    : !prompt_.forward_display_line(
                        display_line_probe);

            if (
                !has_selection &&
                (navigating_history || at_boundary) &&
                navigate_prompt_history(older)
            ) {
                return true;
            }
        }

        if (!is_enter) {
            return false;
        }

        const bool shift_pressed =
            (event->state & GDK_SHIFT_MASK) != 0;

        if (shift_pressed) {
            return false;
        }

        handle_send_or_stop();
        return true;
    }

    bool on_window_key_press(GdkEventKey* event) {
        if (
            event != nullptr &&
            event->keyval == GDK_KEY_F11
        ) {
            continue_current_thread();
            return true;
        }

        if (
            event == nullptr ||
            event->keyval != GDK_KEY_Escape ||
            !turn_in_progress_
        ) {
            return false;
        }

        request_turn_stop();
        return true;
    }

    void clear_skill_suggestions() {
        const auto children =
            skill_suggestions_.get_children();

        for (auto* child : children) {
            if (child != nullptr) {
                skill_suggestions_.remove(*child);
            }
        }
    }

    void load_skills_for_cwd(
        const std::string& cwd
    ) {
        clear_skill_suggestions();
        skill_popover_.popdown();

        if (
            cwd.empty() ||
            !app_server_.is_running()
        ) {
            skill_catalog_.clear();
            skill_catalog_cwd_.clear();
            return;
        }

        if (skill_catalog_cwd_ == cwd) {
            return;
        }

        const auto cached =
            skill_catalog_by_cwd_.find(cwd);

        if (cached != skill_catalog_by_cwd_.end()) {
            skill_catalog_ = cached->second;
            skill_catalog_cwd_ = cwd;
            return;
        }

        skill_catalog_.clear();
        skill_catalog_cwd_.clear();

        if (skill_loaders_.find(cwd) !=
            skill_loaders_.end()) {
            return;
        }

        const AppServerClient::ProcessEnvironment
            environment =
                current_codex_process_environment();

        auto& loader = skill_loaders_[cwd];

        loader = std::thread(
            [this, cwd, environment]() {
                CompletedSkillLoad completed;
                completed.cwd = cwd;

                AppServerClient client;
                std::string error;

                if (!client.start(error, environment)) {
                    completed.error =
                        "Could not start Codex App Server: " +
                        error;
                } else {
                    const auto initialized =
                        client.initialize(
                            "threaddeck",
                            "ThreadDeck",
                            "0.1.0");

                    if (!initialized.success) {
                        completed.error =
                            initialized.error;
                    } else {
                        const auto result =
                            client.list_skills(
                                cwd,
                                false,
                                10000);

                        if (!result.success) {
                            completed.error =
                                result.error;
                        } else if (
                            !result.result.contains("data") ||
                            !result.result["data"].is_array()
                        ) {
                            completed.error =
                                "skills/list returned no data array";
                        } else {
                            for (
                                const auto& entry :
                                result.result["data"]
                            ) {
                                if (
                                    !entry.is_object() ||
                                    !entry.contains("skills") ||
                                    !entry["skills"].is_array()
                                ) {
                                    continue;
                                }

                                for (
                                    const auto& skill :
                                    entry["skills"]
                                ) {
                                    if (
                                        !skill.is_object() ||
                                        !skill.value(
                                            "enabled",
                                            false) ||
                                        !skill.contains("name") ||
                                        !skill["name"].is_string() ||
                                        !skill.contains("path") ||
                                        !skill["path"].is_string()
                                    ) {
                                        continue;
                                    }

                                    completed.skills.push_back(
                                        skill);
                                }
                            }
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(
                        skill_load_result_mutex_);
                    pending_skill_load_results_.push_back(
                        std::move(completed));
                }

                skill_load_dispatcher_.emit();
            });
    }

    void handle_skill_load_finished() {
        std::deque<CompletedSkillLoad> completed_loads;

        {
            std::lock_guard<std::mutex> lock(
                skill_load_result_mutex_);
            completed_loads.swap(
                pending_skill_load_results_);
        }

        for (auto& completed : completed_loads) {
            const auto loader =
                skill_loaders_.find(completed.cwd);

            if (loader != skill_loaders_.end()) {
                if (loader->second.joinable()) {
                    loader->second.join();
                }

                skill_loaders_.erase(loader);
            }

            if (!completed.error.empty()) {
                std::cerr
                    << "SKIPPED: skills/list for "
                    << completed.cwd
                    << ": "
                    << completed.error
                    << '\n';
                continue;
            }

            skill_catalog_by_cwd_[completed.cwd] =
                completed.skills;

            if (
                completed.cwd ==
                    last_active_thread_cwd_ ||
                (
                    current_thread_id_.empty() &&
                    completed.cwd ==
                        selected_folder_path_
                )
            ) {
                skill_catalog_ = completed.skills;
                skill_catalog_cwd_ = completed.cwd;
            }

            std::cout
                << "PASS: loaded "
                << completed.skills.size()
                << " enabled Codex skill(s) for "
                << completed.cwd
                << '\n';
        }
    }

    const nlohmann::json* find_skill(
        const std::string& name
    ) const {
        for (
            const auto& skill :
            skill_catalog_
        ) {
            if (
                skill.value(
                    "name",
                    std::string{}) ==
                name
            ) {
                return &skill;
            }
        }

        return nullptr;
    }

    void select_skill_suggestion(
        const std::string& name
    ) {
        prompt_.get_buffer()->set_text(
            "$" + name + " ");

        skill_popover_.popdown();
        prompt_.grab_focus();

        const auto buffer =
            prompt_.get_buffer();

        buffer->place_cursor(
            buffer->end());
    }

    void update_skill_suggestions() {
        clear_skill_suggestions();

        if (
            turn_in_progress_ ||
            shell_command_in_progress_ ||
            current_thread_id_.empty() ||
            skill_catalog_.empty()
        ) {
            skill_popover_.popdown();
            return;
        }

        const std::string text = trim(
            prompt_.get_buffer()
                ->get_text()
                .raw());

        if (
            text.empty() ||
            text.front() != '$'
        ) {
            skill_popover_.popdown();
            return;
        }

        const std::size_t whitespace =
            text.find_first_of(
                " \t\r\n");

        if (whitespace != std::string::npos) {
            skill_popover_.popdown();
            return;
        }

        const std::string query =
            text.substr(1);

        std::size_t shown = 0;

        for (
            const auto& skill :
            skill_catalog_
        ) {
            const std::string name =
                skill.value(
                    "name",
                    std::string{});

            if (
                name.empty() ||
                name.compare(
                    0,
                    query.size(),
                    query) != 0
            ) {
                continue;
            }

            std::string description;

            if (
                skill.contains("interface") &&
                skill["interface"].is_object()
            ) {
                description =
                    skill["interface"].value(
                        "shortDescription",
                        std::string{});
            }

            if (description.empty()) {
                description =
                    skill.value(
                        "shortDescription",
                        std::string{});
            }

            if (description.empty()) {
                description =
                    skill.value(
                        "description",
                        std::string{});
            }

            if (description.size() > 72) {
                description.resize(72);
                description += "...";
            }

            std::string label =
                "$" + name;

            if (!description.empty()) {
                label +=
                    "  —  " +
                    description;
            }

            auto* button =
                Gtk::manage(
                    new Gtk::Button(
                        label));

            button->set_relief(
                Gtk::RELIEF_NONE);
            button->set_alignment(
                0.0F,
                0.5F);

            button->set_tooltip_text(
                skill.value(
                    "description",
                    std::string{}));

            button->signal_clicked().connect(
                [
                    this,
                    name
                ]() {
                    select_skill_suggestion(
                        name);
                });

            skill_suggestions_.pack_start(
                *button,
                Gtk::PACK_SHRINK);

            ++shown;

            if (shown >= 8) {
                break;
            }
        }

        if (shown == 0) {
            skill_popover_.popdown();
            return;
        }

        skill_suggestions_.show_all_children();
        skill_popover_.show();
        skill_popover_.popup();
    }

    static Glib::RefPtr<Gdk::Pixbuf>
    load_image_preview(
        const std::string& path,
        int maximum_width,
        int maximum_height
    ) {
        const auto source =
            Gdk::Pixbuf::create_from_file(path);

        const int source_width =
            source->get_width();
        const int source_height =
            source->get_height();

        if (
            source_width <= maximum_width &&
            source_height <= maximum_height
        ) {
            return source;
        }

        const double scale = std::min(
            static_cast<double>(maximum_width) /
                static_cast<double>(source_width),
            static_cast<double>(maximum_height) /
                static_cast<double>(source_height));

        return source->scale_simple(
            std::max(
                1,
                static_cast<int>(
                    source_width * scale)),
            std::max(
                1,
                static_cast<int>(
                    source_height * scale)),
            Gdk::INTERP_BILINEAR);
    }

    void refresh_attachment_row() {
        const auto preview_children =
            attachment_previews_.get_children();

        for (auto* child : preview_children) {
            if (child != nullptr) {
                attachment_previews_.remove(*child);
            }
        }

        if (
            attached_image_paths_.empty() &&
            attached_audio_paths_.empty()
        ) {
            attachment_row_.hide();
            return;
        }

        for (
            const auto& image_path :
            attached_image_paths_
        ) {
            auto* preview =
                Gtk::manage(
                    new Gtk::Image());

            try {
                preview->set(
                    load_image_preview(
                        image_path,
                        96,
                        68));
            } catch (...) {
                preview->set_from_icon_name(
                    "image-missing-symbolic",
                    Gtk::ICON_SIZE_DIALOG);

                std::cerr
                    << "WARN: could not render attachment preview "
                    << image_path
                    << '\n';
            }

            preview->set_tooltip_text(image_path);
            preview->get_style_context()->add_class(
                "attachment-preview");

            attachment_previews_.pack_start(
                *preview,
                Gtk::PACK_SHRINK);
        }

        for (
            const auto& audio_path :
            attached_audio_paths_
        ) {
            auto* audio =
                Gtk::manage(
                    new Gtk::Label(
                        "♪ " +
                        std::filesystem::path(
                            audio_path)
                            .filename()
                            .string()));

            audio->set_ellipsize(
                Pango::ELLIPSIZE_END);
            audio->set_max_width_chars(24);
            audio->set_tooltip_text(audio_path);

            attachment_previews_.pack_start(
                *audio,
                Gtk::PACK_SHRINK);
        }

        attachment_row_.show_all();
    }

    void clear_composer_attachments() {
        remove_temporary_attachment_files(
            queued_temporary_attachment_paths_);
        attached_image_paths_.clear();
        attached_audio_paths_.clear();
        refresh_attachment_row();
        update_send_button_state();
        prompt_.grab_focus();
    }

    void handle_composer_file_drop(
        const Glib::RefPtr<Gdk::DragContext>& context,
        int,
        int,
        const Gtk::SelectionData& selection_data,
        guint,
        guint time
    ) {
        bool accepted = false;
        std::size_t added = 0;

        if (
            current_composer_accepts_attachments() &&
            !current_thread_id_.empty()
        ) {
            for (const auto& uri : selection_data.get_uris()) {
                std::string filename;

                try {
                    filename =
                        Glib::filename_from_uri(uri);
                } catch (const Glib::Error&) {
                    continue;
                }

                std::error_code error;

                if (
                    filename.empty() ||
                    !std::filesystem::is_regular_file(
                        filename,
                        error) ||
                    error ||
                    gdk_pixbuf_get_file_info(
                        filename.c_str(),
                        nullptr,
                        nullptr) == nullptr
                ) {
                    continue;
                }

                accepted = true;

                if (
                    std::find(
                        attached_image_paths_.begin(),
                        attached_image_paths_.end(),
                        filename) !=
                    attached_image_paths_.end()
                ) {
                    continue;
                }

                attached_image_paths_.push_back(
                    filename);
                ++added;
            }
        }

        if (context) {
            context->drag_finish(
                accepted,
                false,
                time);
        }

        if (added > 0) {
            refresh_attachment_row();
            update_send_button_state();
            prompt_.grab_focus();
            status_label_.set_text(
                turn_in_progress_
                    ? "Codex: dropped image queued for the follow-up"
                    : "Codex: dropped image ready to send");

            std::cout
                << "PASS: queued "
                << added
                << " dropped local image attachment(s)\n";
        } else if (!accepted) {
            status_label_.set_text(
                current_composer_accepts_attachments()
                    ? "Codex: drop a local image file"
                    : "Codex: attachments are unavailable right now");
        }
    }

    void choose_local_images() {
        if (!current_composer_accepts_attachments()) {
            return;
        }

        if (current_thread_id_.empty()) {
            status_label_.set_text(
                "Codex: no active thread");
            return;
        }

        Gtk::FileChooserDialog dialog(
            *this,
            "Attach images",
            Gtk::FILE_CHOOSER_ACTION_OPEN);

        dialog.add_button(
            "_Cancel",
            Gtk::RESPONSE_CANCEL);

        dialog.add_button(
            "_Attach",
            Gtk::RESPONSE_OK);

        dialog.set_select_multiple(true);

        const std::string initial_folder =
            !last_active_thread_cwd_.empty()
                ? last_active_thread_cwd_
                : selected_folder_path_;

        if (!initial_folder.empty()) {
            dialog.set_current_folder(
                initial_folder);
        }

        const auto image_filter =
            Gtk::FileFilter::create();

        image_filter->set_name("Images");
        image_filter->add_mime_type("image/*");
        dialog.add_filter(image_filter);

        if (dialog.run() != Gtk::RESPONSE_OK) {
            return;
        }

        const auto selected =
            dialog.get_filenames();

        std::size_t added = 0;

        for (
            const auto& filename :
            selected
        ) {
            std::error_code error;

            if (
                filename.empty() ||
                !std::filesystem::is_regular_file(
                    filename,
                    error) ||
                error
            ) {
                continue;
            }

            if (
                std::find(
                    attached_image_paths_.begin(),
                    attached_image_paths_.end(),
                    filename) !=
                attached_image_paths_.end()
            ) {
                continue;
            }

            attached_image_paths_.push_back(
                filename);

            ++added;
        }

        refresh_attachment_row();
        update_send_button_state();
        prompt_.grab_focus();

        std::cout
            << "PASS: queued "
            << added
            << " local image attachment(s)"
            << '\n';
    }

    void choose_local_audio() {
        if (!current_composer_accepts_attachments()) {
            return;
        }

        if (current_thread_id_.empty()) {
            status_label_.set_text(
                "Codex: no active thread");
            return;
        }

        Gtk::FileChooserDialog dialog(
            *this,
            "Attach audio",
            Gtk::FILE_CHOOSER_ACTION_OPEN);

        dialog.add_button(
            "_Cancel",
            Gtk::RESPONSE_CANCEL);

        dialog.add_button(
            "_Attach",
            Gtk::RESPONSE_OK);

        dialog.set_select_multiple(true);

        const std::string initial_folder =
            !last_active_thread_cwd_.empty()
                ? last_active_thread_cwd_
                : selected_folder_path_;

        if (!initial_folder.empty()) {
            dialog.set_current_folder(
                initial_folder);
        }

        const auto audio_filter =
            Gtk::FileFilter::create();

        audio_filter->set_name("Audio");
        audio_filter->add_mime_type("audio/*");
        dialog.add_filter(audio_filter);

        if (dialog.run() != Gtk::RESPONSE_OK) {
            return;
        }

        const auto selected =
            dialog.get_filenames();

        std::size_t added = 0;

        for (
            const auto& filename :
            selected
        ) {
            std::error_code error;

            if (
                filename.empty() ||
                !std::filesystem::is_regular_file(
                    filename,
                    error) ||
                error
            ) {
                continue;
            }

            if (
                std::find(
                    attached_audio_paths_.begin(),
                    attached_audio_paths_.end(),
                    filename) !=
                attached_audio_paths_.end()
            ) {
                continue;
            }

            attached_audio_paths_.push_back(
                filename);

            ++added;
        }

        refresh_attachment_row();
        update_send_button_state();
        prompt_.grab_focus();

        std::cout
            << "PASS: queued "
            << added
            << " local audio attachment(s)"
            << '\n';
    }

    void handle_prompt_changed() {
        if (!prompt_history_restoring_) {
            auto navigation =
                prompt_command_history_navigation_.find(
                    composer_thread_id_);

            if (
                navigation !=
                    prompt_command_history_navigation_.end() &&
                navigation->second.active
            ) {
                navigation->second = {};
            }
        }

        if (
            !prompt_history_restoring_ &&
            prompt_history_transaction_depth_ == 0
        ) {
            const auto current =
                capture_prompt_edit_snapshot();

            if (!prompt_snapshots_equal(
                    prompt_history_current_,
                    current)) {
                prompt_undo_history_.push_back(
                    prompt_history_current_);
                trim_prompt_history();
                prompt_redo_history_.clear();
                prompt_history_current_ = current;
            }
        }

        update_prompt_height();
        update_send_button_state();
        update_skill_suggestions();
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

    bool current_session_turn_busy() const {
        const ThreadTurnSession* session =
            find_turn_session(current_thread_id_);

        return
            session != nullptr &&
            session->busy &&
            session->work_kind !=
                SessionWorkKind::ShellCommand;
    }

    bool current_session_shell_busy() const {
        const ThreadTurnSession* session =
            find_turn_session(current_thread_id_);

        return
            session != nullptr &&
            session->busy &&
            session->work_kind ==
                SessionWorkKind::ShellCommand;
    }

    bool current_composer_accepts_attachments() const {
        if (
            current_thread_id_.empty() ||
            current_session_shell_busy()
        ) {
            return false;
        }

        const ThreadTurnSession* session =
            find_turn_session(current_thread_id_);

        return
            session == nullptr ||
            !session->busy ||
            session->work_kind ==
                SessionWorkKind::Turn;
    }

    void update_send_button_state() {
        turn_in_progress_ =
            current_session_turn_busy();
        shell_command_in_progress_ =
            current_session_shell_busy();

        const ThreadTurnSession* current_session =
            find_turn_session(current_thread_id_);
        continue_button_.set_sensitive(
            !current_thread_id_.empty() &&
            !(
                current_session != nullptr &&
                current_session->stop_requested
            ) &&
            (
                current_session == nullptr ||
                !current_session->busy ||
                current_session->work_kind ==
                    SessionWorkKind::Turn
            ));

        const bool accepts_attachments =
            current_composer_accepts_attachments();
        attachment_button_.set_sensitive(
            accepts_attachments);
        audio_attachment_button_.set_sensitive(
            accepts_attachments);

        if (shell_command_in_progress_) {
            send_button_.set_sensitive(false);
            return;
        }

        if (turn_in_progress_) {
            const ThreadTurnSession* session =
                find_turn_session(current_thread_id_);

            const bool accepts_follow_up =
                session != nullptr &&
                session->work_kind ==
                    SessionWorkKind::Turn;

            const std::string follow_up_text =
                accepts_follow_up
                    ? trim(
                        prompt_.get_buffer()
                            ->get_text().raw())
                    : std::string{};

            const bool has_follow_up =
                !follow_up_text.empty() ||
                !attached_image_paths_.empty() ||
                !attached_audio_paths_.empty();

            send_image_.set_from_icon_name(
                has_follow_up
                    ? "mail-send-symbolic"
                    : "media-playback-stop-symbolic",
                Gtk::ICON_SIZE_BUTTON);

            send_button_.set_tooltip_text(
                stop_requested_
                    ? "Stop requested"
                    : (
                        has_follow_up
                            ? "Send this follow-up into the active turn (Enter)"
                            : "Stop the active turn (Esc)"
                    ));

            send_button_.set_sensitive(
                !stop_requested_ &&
                (
                    has_follow_up ||
                    !active_turn_id_.empty()
                ));
            return;
        }

        const std::string prompt_text = trim(
            prompt_.get_buffer()->get_text().raw());

        send_button_.set_sensitive(
            !current_thread_id_.empty() &&
            (
                !prompt_text.empty() ||
                !attached_image_paths_.empty() ||
                !attached_audio_paths_.empty()
            ));
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

    static std::string generated_thread_title(
        const std::string& response
    ) {
        static const std::string marker =
            "THREADDECK_TITLE:";

        const std::size_t marker_position =
            response.rfind(marker);

        if (marker_position == std::string::npos) {
            return {};
        }

        const std::size_t title_start =
            marker_position + marker.size();
        const std::size_t title_end =
            response.find_first_of("\r\n", title_start);

        std::string title = trim(
            response.substr(
                title_start,
                title_end == std::string::npos
                    ? std::string::npos
                    : title_end - title_start));

        const auto is_wrapper = [](char character) {
            return
                character == '*' ||
                character == '`' ||
                character == '\'' ||
                character == '"';
        };

        while (!title.empty() && is_wrapper(title.front())) {
            title.erase(title.begin());
        }

        while (!title.empty() && is_wrapper(title.back())) {
            title.pop_back();
        }

        title = trim(title);

        if (title.empty()) {
            return {};
        }

        Glib::ustring unicode_title(title);
        constexpr Glib::ustring::size_type maximum_length = 48;

        if (unicode_title.size() > maximum_length) {
            unicode_title = unicode_title.substr(
                0,
                maximum_length);

            const auto last_space =
                unicode_title.rfind(' ');

            if (
                last_space != Glib::ustring::npos &&
                last_space >= 28
            ) {
                unicode_title = unicode_title.substr(
                    0,
                    last_space);
            }
        }

        return trim(unicode_title.raw());
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
        const std::string& project_id
    ) const {
        const auto custom_label =
            folder_labels_.find(project_id);

        if (
            custom_label != folder_labels_.end() &&
            !custom_label->second.empty()
        ) {
            return custom_label->second;
        }

        const std::string cwd =
            project_cwd(project_id);
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

    void handle_thread_search_changed() {
        if (sidebar_search_connection_.connected()) {
            sidebar_search_connection_.disconnect();
        }

        const std::string search_term =
            trim(sidebar_search_.get_text().raw());

        if (search_term.empty()) {
            {
                std::lock_guard<std::mutex> lock(
                    thread_search_mutex_);
                ++thread_search_generation_;
                thread_search_latest_generation_ =
                    thread_search_generation_;
                thread_search_has_request_ = false;
            }

            thread_search_results_.clear();
            thread_search_result_term_.clear();
            thread_search_error_.clear();
            thread_search_loading_ = false;
            schedule_sidebar_refresh();
            return;
        }

        sidebar_search_connection_ =
            Glib::signal_timeout().connect(
                [this, search_term]() {
                    queue_thread_search(search_term);
                    return false;
                },
                300);
    }

    void queue_thread_search(
        const std::string& search_term
    ) {
        if (search_term.empty()) {
            return;
        }

        ThreadSearchRequest request;
        request.search_term = search_term;
        request.environment =
            current_codex_process_environment();

        {
            std::lock_guard<std::mutex> lock(
                thread_search_mutex_);
            request.generation =
                ++thread_search_generation_;
            thread_search_latest_generation_ =
                request.generation;
            thread_search_request_ = request;
            thread_search_has_request_ = true;
        }

        thread_search_results_.clear();
        thread_search_result_term_.clear();
        thread_search_error_.clear();
        thread_search_loading_ = true;
        schedule_sidebar_refresh();
        thread_search_condition_.notify_one();
    }

    bool thread_search_is_superseded(
        std::size_t generation
    ) {
        std::lock_guard<std::mutex> lock(
            thread_search_mutex_);
        return
            thread_search_stop_ ||
            generation !=
                thread_search_latest_generation_;
    }

    void run_thread_search_worker() {
        AppServerClient client;
        bool client_ready = false;

        while (true) {
            ThreadSearchRequest request;

            {
                std::unique_lock<std::mutex> lock(
                    thread_search_mutex_);
                thread_search_condition_.wait(
                    lock,
                    [this]() {
                        return
                            thread_search_stop_ ||
                            thread_search_has_request_;
                    });

                if (thread_search_stop_) {
                    break;
                }

                request = thread_search_request_;
                thread_search_has_request_ = false;
            }

            CompletedThreadSearch completed;
            completed.generation = request.generation;
            completed.search_term = request.search_term;

            if (!client_ready) {
                std::string error;

                if (!client.start(error, request.environment)) {
                    completed.error =
                        "Could not start the Codex search service: " +
                        error;
                } else {
                    const auto initialized =
                        client.initialize(
                            "threaddeck-search",
                            "ThreadDeck Search",
                            "0.1.0");

                    if (!initialized.success) {
                        completed.error = initialized.error;
                        client.shutdown();
                    } else {
                        client_ready = true;
                    }
                }
            }

            std::string cursor;
            std::set<std::string> result_ids;

            while (
                completed.error.empty() &&
                !thread_search_is_superseded(
                    request.generation)
            ) {
                const auto page =
                    client.search_threads(
                        request.search_term,
                        100,
                        10000,
                        cursor);

                if (!page.success) {
                    completed.error = page.error;
                    client.shutdown();
                    client_ready = false;
                    break;
                }

                for (const auto& match : page.matches) {
                    nlohmann::json thread = match["thread"];
                    const std::string thread_id =
                        thread.value(
                            "id",
                            std::string{});

                    if (
                        thread_id.empty() ||
                        !result_ids.insert(thread_id).second
                    ) {
                        continue;
                    }

                    if (
                        match.contains("snippet") &&
                        match["snippet"].is_string()
                    ) {
                        thread["_threaddeckSearchSnippet"] =
                            match["snippet"];
                    }

                    completed.threads.push_back(
                        std::move(thread));
                }

                if (page.next_cursor.empty()) {
                    break;
                }

                cursor = page.next_cursor;
            }

            if (thread_search_is_superseded(
                    request.generation)) {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(
                    thread_search_mutex_);
                thread_search_completed_.push_back(
                    std::move(completed));
            }

            thread_search_dispatcher_.emit();
        }

        client.shutdown();
    }

    void handle_thread_search_finished() {
        std::deque<CompletedThreadSearch> completed;

        {
            std::lock_guard<std::mutex> lock(
                thread_search_mutex_);
            completed.swap(thread_search_completed_);
        }

        for (auto& result : completed) {
            if (
                result.generation !=
                    thread_search_generation_ ||
                trim(sidebar_search_.get_text().raw()) !=
                    result.search_term
            ) {
                continue;
            }

            thread_search_results_ =
                std::move(result.threads);
            thread_search_result_term_ =
                std::move(result.search_term);
            thread_search_error_ =
                std::move(result.error);
            thread_search_loading_ = false;
        }

        schedule_sidebar_refresh();
    }

    static bool valid_sort_id(
        const std::string& sort_id
    ) {
        return
            sort_id == "updated-desc" ||
            sort_id == "updated-asc" ||
            sort_id == "name-asc" ||
            sort_id == "name-desc";
    }

    std::string thread_sort_for_project(
        const std::string& cwd
    ) const {
        const auto saved =
            project_thread_sorts_.find(cwd);

        return saved != project_thread_sorts_.end()
            ? saved->second
            : "updated-desc";
    }

    void sort_sidebar_threads(
        std::vector<nlohmann::json>& threads,
        const std::string& sort_id
    ) const {
        const bool sort_by_name =
            sort_id == "name-asc" ||
            sort_id == "name-desc";
        const bool descending =
            sort_id == "updated-desc" ||
            sort_id == "name-desc";

        std::stable_sort(
            threads.begin(),
            threads.end(),
            [this, sort_by_name, descending](
                const nlohmann::json& left,
                const nlohmann::json& right
            ) {
                if (sort_by_name) {
                    const auto left_name =
                        Glib::ustring(
                            display_thread_label(left))
                            .casefold();
                    const auto right_name =
                        Glib::ustring(
                            display_thread_label(right))
                            .casefold();

                    if (left_name != right_name) {
                        return descending
                            ? left_name > right_name
                            : left_name < right_name;
                    }
                } else {
                    const double left_updated =
                        left.contains("updatedAt")
                            ? json_number(left["updatedAt"])
                            : 0.0;
                    const double right_updated =
                        right.contains("updatedAt")
                            ? json_number(right["updatedAt"])
                            : 0.0;

                    if (left_updated != right_updated) {
                        return descending
                            ? left_updated > right_updated
                            : left_updated < right_updated;
                    }
                }

                return left.value(
                    "id",
                    std::string{}) <
                    right.value(
                        "id",
                        std::string{});
            });
    }

    bool thread_belongs_to_project(
        const std::string& thread_id,
        const std::string& project_id,
        const std::string& cwd
    ) const {
        const auto assigned =
            thread_project_assignments_.find(thread_id);

        if (assigned != thread_project_assignments_.end()) {
            return assigned->second == project_id;
        }

        return primary_project_id_for_cwd(cwd) ==
            project_id;
    }

    double project_latest_thread_update(
        const std::string& project_id
    ) const {
        double latest = 0.0;
        const std::string cwd =
            project_cwd(project_id);
        const auto cached = thread_catalog_.find(cwd);

        if (cached != thread_catalog_.end()) {
            for (const auto& thread : cached->second) {
                const std::string thread_id =
                    thread.value("id", std::string{});
                if (
                    thread.is_object() &&
                    thread_belongs_to_project(
                        thread_id,
                        project_id,
                        cwd) &&
                    thread.contains("updatedAt")
                ) {
                    latest = std::max(
                        latest,
                        json_number(thread["updatedAt"]));
                }
            }
        }

        for (const auto& assignment :
             thread_project_assignments_) {
            if (assignment.second != project_id) {
                continue;
            }

            const auto summary =
                moved_thread_summaries_.find(
                    assignment.first);

            if (
                summary != moved_thread_summaries_.end() &&
                summary->second.is_object() &&
                summary->second.contains("updatedAt")
            ) {
                latest = std::max(
                    latest,
                    json_number(
                        summary->second["updatedAt"]));
            }
        }

        return latest;
    }

    void sort_sidebar_projects(
        std::vector<std::string>& projects
    ) const {
        const bool sort_by_name =
            sidebar_project_sort_ == "name-asc" ||
            sidebar_project_sort_ == "name-desc";
        const bool descending =
            sidebar_project_sort_ == "updated-desc" ||
            sidebar_project_sort_ == "name-desc";

        std::stable_sort(
            projects.begin(),
            projects.end(),
            [this, sort_by_name, descending](
                const std::string& left,
                const std::string& right
            ) {
                if (sort_by_name) {
                    const auto left_name =
                        Glib::ustring(
                            display_folder_label(left))
                            .casefold();
                    const auto right_name =
                        Glib::ustring(
                            display_folder_label(right))
                            .casefold();

                    if (left_name != right_name) {
                        return descending
                            ? left_name > right_name
                            : left_name < right_name;
                    }
                } else {
                    const double left_updated =
                        project_latest_thread_update(left);
                    const double right_updated =
                        project_latest_thread_update(right);

                    if (left_updated != right_updated) {
                        return descending
                            ? left_updated > right_updated
                            : left_updated < right_updated;
                    }
                }

                return left < right;
            });
    }

    bool project_has_threads(
        const std::string& project_id
    ) const {
        for (const auto& assignment :
             thread_project_assignments_) {
            if (assignment.second == project_id) {
                return true;
            }
        }

        const std::string cwd =
            project_cwd(project_id);
        const auto catalog = thread_catalog_.find(cwd);

        if (catalog != thread_catalog_.end()) {
            for (const auto& thread : catalog->second) {
                if (
                    thread.is_object() &&
                    thread.contains("id") &&
                    thread["id"].is_string() &&
                    thread_belongs_to_project(
                        thread["id"].get<std::string>(),
                        project_id,
                        cwd)
                ) {
                    return true;
                }
            }
        }

        for (const auto& session : turn_sessions_) {
            if (
                session.second &&
                project_id_for_thread(
                    session.first,
                    session.second->cwd) == project_id
            ) {
                return true;
            }
        }

        return
            !current_thread_id_.empty() &&
            selected_project_id_ == project_id;
    }

    void request_project_deletion(
        const std::string& project_id
    ) {
        if (project_has_threads(project_id)) {
            Gtk::MessageDialog dialog(
                *this,
                "This project is not empty.",
                false,
                Gtk::MESSAGE_INFO,
                Gtk::BUTTONS_OK,
                true);
            dialog.set_secondary_text(
                "Move or delete every thread before deleting the project.");
            dialog.run();
            return;
        }

        const std::string project_label =
            display_folder_label(project_id);
        const std::string cwd =
            project_cwd(project_id);

        Gtk::MessageDialog confirmation(
            *this,
            "Delete empty project ‘" +
                project_label + "’?",
            false,
            Gtk::MESSAGE_QUESTION,
            Gtk::BUTTONS_NONE,
            true);
        confirmation.set_secondary_text(
            "This removes only the ThreadDeck project entry. The folder on disk will not be changed.");
        confirmation.add_button(
            "Cancel",
            Gtk::RESPONSE_CANCEL);
        confirmation.add_button(
            "Delete project",
            Gtk::RESPONSE_OK);

        if (confirmation.run() != Gtk::RESPONSE_OK) {
            return;
        }

        selected_project_folders_.erase(
            std::remove(
                selected_project_folders_.begin(),
                selected_project_folders_.end(),
                project_id),
            selected_project_folders_.end());
        collapsed_project_folders_.erase(project_id);
        folder_labels_.erase(project_id);
        project_thread_sorts_.erase(project_id);
        project_paths_.erase(project_id);

        if (editing_folder_cwd_ == project_id) {
            editing_folder_cwd_.clear();
        }

        if (selected_project_id_ == project_id) {
            current_thread_id_.clear();
            last_active_thread_id_.clear();
            last_active_thread_cwd_.clear();
            clear_active_thread_surfaces();

            if (selected_project_folders_.empty()) {
                selected_project_id_.clear();
                selected_folder_path_.clear();
                selected_folder_.set_text(
                    "No folder selected");
            } else {
                selected_project_id_ =
                    selected_project_folders_.front();
                selected_folder_path_ =
                    project_cwd(selected_project_id_);
                selected_folder_.set_text(
                    selected_folder_path_);
            }

            transcript_.get_buffer()->set_text(
                "Empty project deleted from ThreadDeck.\n\nChoose a thread or create a new one.");
        }

        const bool cwd_still_used =
            std::any_of(
                selected_project_folders_.begin(),
                selected_project_folders_.end(),
                [this, &cwd](const std::string& remaining) {
                    return project_cwd(remaining) == cwd;
                });

        if (!cwd_still_used) {
            thread_catalog_.erase(cwd);
        }

        new_thread_button_.set_sensitive(
            app_server_.is_running() &&
            !selected_folder_path_.empty());
        status_label_.set_text("Codex: connected");
        save_ui_state();
        update_send_button_state();
        schedule_sidebar_refresh();
    }

    static bool contains_search_text(
        const std::string& value,
        const std::string& query
    ) {
        if (query.empty()) {
            return true;
        }

        return Glib::ustring(value)
            .casefold()
            .find(
                Glib::ustring(query).casefold()) !=
            Glib::ustring::npos;
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

    bool handle_label_edit_focus_out(
        GdkEventFocus*,
        bool is_folder,
        const std::string& metadata_key
    ) {
        const bool still_editing =
            is_folder
                ? editing_folder_cwd_ == metadata_key
                : editing_thread_id_ == metadata_key;

        if (!still_editing) {
            return false;
        }

        editing_folder_cwd_.clear();
        editing_thread_id_.clear();
        schedule_sidebar_refresh();
        return false;
    }

    void clear_sidebar_list() {
        approval_question_rows_.clear();

        const auto children =
            sidebar_list_.get_children();

        for (auto* child : children) {
            if (child != nullptr) {
                sidebar_list_.remove(*child);
            }
        }
    }

    void remove_thread_from_catalog(
        const std::string& thread_id
    ) {
        for (auto& entry : thread_catalog_) {
            auto& threads = entry.second;

            threads.erase(
                std::remove_if(
                    threads.begin(),
                    threads.end(),
                    [&thread_id](
                        const nlohmann::json& thread
                    ) {
                        return
                            thread.is_object() &&
                            thread.value(
                                "id",
                                std::string{}) ==
                                thread_id;
                    }),
                threads.end());
        }
    }

    void upsert_session_in_thread_catalog(
        const std::string& thread_id,
        const std::string& cwd,
        const nlohmann::json& transcript_input
    ) {
        nlohmann::json summary =
            cached_thread_summary(thread_id);

        if (!summary.is_object()) {
            summary = nlohmann::json::object();
        }

        summary["id"] = thread_id;
        summary["cwd"] = cwd;
        summary["updatedAt"] =
            static_cast<std::int64_t>(
                std::time(nullptr));

        if (
            !summary.contains("preview") ||
            !summary["preview"].is_string() ||
            summary["preview"].get<std::string>().empty()
        ) {
            for (const auto& input :
                 transcript_input) {
                if (
                    input.is_object() &&
                    input.value(
                        "type",
                        std::string{}) == "text" &&
                    input.contains("text") &&
                    input["text"].is_string()
                ) {
                    summary["preview"] =
                        input["text"].get<std::string>();
                    break;
                }
            }
        }

        remove_thread_from_catalog(thread_id);
        thread_catalog_[cwd].push_back(
            summary);

        if (
            thread_project_assignments_.find(thread_id) !=
            thread_project_assignments_.end()
        ) {
            moved_thread_summaries_[thread_id] =
                std::move(summary);
        }
    }

    void request_thread_move(
        const std::string& thread_id,
        const std::string& source_project_id,
        const std::string& destination_project_id
    ) {
        const std::string source_cwd =
            project_cwd(source_project_id);
        const std::string destination_cwd =
            project_cwd(destination_project_id);

        if (
            thread_id.empty() ||
            destination_cwd.empty() ||
            source_project_id == destination_project_id
        ) {
            return;
        }

        if (thread_is_busy(thread_id)) {
            Gtk::MessageDialog dialog(
                *this,
                "This thread is still running.",
                false,
                Gtk::MESSAGE_WARNING,
                Gtk::BUTTONS_OK,
                true);
            dialog.set_secondary_text(
                "Stop or finish the thread before moving it.");
            dialog.run();
            return;
        }

        std::error_code directory_error;

        if (
            !std::filesystem::is_directory(
                destination_cwd,
                directory_error) ||
            directory_error
        ) {
            Gtk::MessageDialog dialog(
                *this,
                "The destination project folder is unavailable.",
                false,
                Gtk::MESSAGE_ERROR,
                Gtk::BUTTONS_OK,
                true);
            dialog.set_secondary_text(destination_cwd);
            dialog.run();
            return;
        }

        if (thread_move_worker_.joinable()) {
            thread_move_worker_.join();
        }

        moving_thread_id_ = thread_id;
        status_label_.set_text(
            "Codex: moving thread to " +
            display_folder_label(
                destination_project_id));
        schedule_sidebar_refresh();

        const auto environment =
            current_codex_process_environment();

        thread_move_worker_ = std::thread(
            [
                this,
                thread_id,
                source_project_id,
                destination_project_id,
                source_cwd,
                destination_cwd,
                environment
            ]() {
                CompletedThreadMove completed;
                completed.thread_id = thread_id;
                completed.source_project_id =
                    source_project_id;
                completed.destination_project_id =
                    destination_project_id;
                completed.source_cwd = source_cwd;
                completed.destination_cwd =
                    destination_cwd;

                AppServerClient client;
                std::string error;

                if (!client.start(error, environment)) {
                    completed.result.error =
                        "Could not start Codex App Server: " +
                        error;
                } else {
                    const auto initialized =
                        client.initialize(
                            "threaddeck",
                            "ThreadDeck",
                            "0.1.0");

                    if (!initialized.success) {
                        completed.result.error =
                            initialized.error;
                    } else {
                        const auto resumed =
                            client.resume_thread(
                                thread_id,
                                10000);

                        if (!resumed.success) {
                            completed.result.error =
                                resumed.error;
                        } else {
                            completed.result =
                                client.update_thread_cwd(
                                    thread_id,
                                    destination_cwd,
                                    10000);
                        }
                    }
                }

                client.shutdown();

                {
                    std::lock_guard<std::mutex> lock(
                        thread_move_result_mutex_);
                    pending_thread_moves_.push_back(
                        std::move(completed));
                }

                thread_move_dispatcher_.emit();
            });
    }

    void handle_thread_move_finished() {
        std::deque<CompletedThreadMove> completed;

        {
            std::lock_guard<std::mutex> lock(
                thread_move_result_mutex_);
            completed.swap(pending_thread_moves_);
        }

        if (thread_move_worker_.joinable()) {
            thread_move_worker_.join();
        }

        for (auto& move : completed) {
            moving_thread_id_.clear();

            if (!move.result.success) {
                status_label_.set_text(
                    "Codex: thread move failed");

                Gtk::MessageDialog dialog(
                    *this,
                    "The thread could not be moved.",
                    false,
                    Gtk::MESSAGE_ERROR,
                    Gtk::BUTTONS_OK,
                    true);
                dialog.set_secondary_text(
                    move.result.error);
                dialog.run();
                schedule_sidebar_refresh();
                continue;
            }

            nlohmann::json moved_thread =
                cached_thread_summary(move.thread_id);
            remove_thread_from_catalog(move.thread_id);

            if (!moved_thread.is_object()) {
                moved_thread = {
                    {"id", move.thread_id},
                };
            }

            moved_thread["cwd"] =
                move.destination_cwd;
            thread_project_assignments_[move.thread_id] =
                move.destination_project_id;
            moved_thread_summaries_[move.thread_id] =
                moved_thread;
            thread_catalog_[move.destination_cwd]
                .push_back(std::move(moved_thread));

            if (auto* session =
                find_turn_session(move.thread_id)) {
                session->cwd = move.destination_cwd;
                session->options.cwd =
                    move.destination_cwd;

                if (session->base_thread.is_object()) {
                    session->base_thread["cwd"] =
                        move.destination_cwd;
                }

                if (session->client) {
                    session->client->shutdown();
                }

                session->client_environment_generation = 0;
            }

            if (current_thread_id_ == move.thread_id) {
                selected_project_id_ =
                    move.destination_project_id;
                selected_folder_path_ =
                    move.destination_cwd;
                last_active_thread_cwd_ =
                    move.destination_cwd;
                selected_folder_.set_text(
                    move.destination_cwd);
            }

            status_label_.set_text(
                "Codex: thread moved to " +
                display_folder_label(
                    move.destination_project_id));
            save_ui_state();
            schedule_sidebar_refresh();
        }
    }

    void request_thread_deletion(
        const std::string& thread_id,
        const std::string& thread_label
    ) {
        if (thread_is_busy(thread_id)) {
            Gtk::MessageDialog dialog(
                *this,
                "This thread is still running.",
                false,
                Gtk::MESSAGE_WARNING,
                Gtk::BUTTONS_OK,
                true);

            dialog.set_secondary_text(
                "Stop the thread before deleting it.");
            dialog.run();
            return;
        }

        Gtk::MessageDialog confirmation(
            *this,
            "Permanently delete this thread?",
            false,
            Gtk::MESSAGE_WARNING,
            Gtk::BUTTONS_NONE,
            true);

        confirmation.set_secondary_text(
            thread_label +
            "\n\nThis also deletes any Codex threads spawned "
            "from it. This cannot be undone.");
        confirmation.add_button(
            "_Cancel",
            Gtk::RESPONSE_CANCEL);
        confirmation.add_button(
            "_Delete",
            Gtk::RESPONSE_OK);
        confirmation.set_default_response(
            Gtk::RESPONSE_CANCEL);

        if (confirmation.run() != Gtk::RESPONSE_OK) {
            return;
        }

        auto session =
            turn_sessions_.find(thread_id);

        if (session != turn_sessions_.end()) {
            if (session->second->worker.joinable()) {
                session->second->worker.join();
            }

            if (session->second->client) {
                session->second->client->shutdown();
            }
        }

        status_label_.set_text(
            "Codex: deleting thread");

        const auto result =
            app_server_.delete_thread(thread_id);

        if (!result.success) {
            status_label_.set_text(
                "Codex: thread deletion failed");

            Gtk::MessageDialog error_dialog(
                *this,
                "The thread could not be deleted.",
                false,
                Gtk::MESSAGE_ERROR,
                Gtk::BUTTONS_OK,
                true);

            error_dialog.set_secondary_text(
                result.error);
            error_dialog.run();
            return;
        }

        if (session != turn_sessions_.end()) {
            turn_sessions_.erase(session);
        }

        thread_labels_.erase(thread_id);
        thread_access_selections_.erase(thread_id);
        thread_model_selections_.erase(thread_id);
        thread_reasoning_selections_.erase(thread_id);
        thread_shield_selections_.erase(thread_id);
        thread_auto_copy_selections_.erase(thread_id);
        thread_remote_shield_hosts_.erase(thread_id);
        thread_observed_remote_hosts_.erase(thread_id);
        prompt_command_histories_.erase(thread_id);
        prompt_history_seeded_threads_.erase(thread_id);
        prompt_command_history_navigation_.erase(
            thread_id);
        paused_threads_.erase(thread_id);
        pause_requested_threads_.erase(thread_id);
        thread_project_assignments_.erase(thread_id);
        moved_thread_summaries_.erase(thread_id);
        thread_configured_approval_policies_.erase(
            thread_id);
        thread_configured_sandbox_policies_.erase(
            thread_id);
        remove_thread_from_catalog(thread_id);
        discard_composer_draft(thread_id);

        if (editing_thread_id_ == thread_id) {
            editing_thread_id_.clear();
        }

        if (last_active_thread_id_ == thread_id) {
            last_active_thread_id_.clear();
            last_active_thread_cwd_.clear();
        }

        if (current_thread_id_ == thread_id) {
            current_thread_id_.clear();
            clear_active_thread_surfaces();
            transcript_.get_buffer()->set_text(
                "Thread deleted.\n\nChoose another thread "
                "or create a new one.");
        }

        status_label_.set_text(
            "Codex: connected");
        save_ui_state();
        update_send_button_state();
        schedule_sidebar_refresh();

        std::cout
            << "PASS: permanently deleted Codex thread "
            << thread_id
            << '\n';
    }

    void start_thread_summary_and_title(
        const std::string& cwd,
        const std::string& thread_id
    ) {
        if (thread_id.empty()) {
            return;
        }

        if (thread_is_busy(thread_id)) {
            Gtk::MessageDialog dialog(
                *this,
                "This thread is still running.",
                false,
                Gtk::MESSAGE_INFO,
                Gtk::BUTTONS_OK,
                true);

            dialog.set_secondary_text(
                "Wait for its current work to finish before "
                "summarizing and titling it.");
            dialog.run();
            return;
        }

        const auto thread_result =
            app_server_.read_thread(
                thread_id,
                true);

        if (!thread_result.success) {
            Gtk::MessageDialog dialog(
                *this,
                "The thread could not be read.",
                false,
                Gtk::MESSAGE_ERROR,
                Gtk::BUTTONS_OK,
                true);

            dialog.set_secondary_text(
                thread_result.error);
            dialog.run();
            return;
        }

        const std::string instruction =
            "Summarize this thread's purpose, important work, "
            "decisions, and current outcome in one concise "
            "paragraph. Do not use tools or modify anything. "
            "After the summary, write one final separate line "
            "exactly as THREADDECK_TITLE: Short title. The title "
            "must be descriptive, contain no quotes, and be at "
            "most 48 characters including spaces. Write nothing "
            "after the title line.";

        const nlohmann::json turn_input =
            nlohmann::json::array(
                {
                    {
                        {"type", "text"},
                        {"text", instruction},
                        {"text_elements",
                         nlohmann::json::array()},
                    },
                });

        const AppServerClient::SessionOptions
            session_options;

        auto& session_pointer =
            turn_sessions_[thread_id];

        if (!session_pointer) {
            session_pointer =
                std::make_unique<
                    ThreadTurnSession>();
            session_pointer->thread_id =
                thread_id;
        }

        ThreadTurnSession& session =
            *session_pointer;

        if (session.worker.joinable()) {
            session.worker.join();
        }

        session.cwd = cwd;
        session.busy = true;
        session.failed = false;
        session.work_kind =
            SessionWorkKind::SummarizeAndTitle;
        session.base_thread =
            thread_result.thread;
        session.transcript_input =
            turn_input;
        session.pending_display.clear();
        session.options = session_options;
        session.options.cwd = session.cwd;
        session.mode = "default";
        prepare_session_process_environment(session);

        if (!session.client) {
            session.client =
                std::make_unique<
                    AppServerClient>();
        }

        const bool is_current =
            thread_id == current_thread_id_;

        if (is_current) {
            current_thread_data_ =
                thread_result.thread;
            append_user_content_to_transcript(
                turn_input);
            begin_live_turn(session);
            status_label_.set_text(
                "Codex: summarizing and titling thread");
            current_thread_turn_failed_ = false;
            set_turn_busy(true);
            prompt_.set_editable(false);
            send_button_.set_tooltip_text(
                "Stop summarizing this thread (Esc)");
        } else {
            session.active_turn_id.clear();
            session.stop_requested = false;
            session.interrupt_sent = false;
            session.reasoning_summary.clear();
            session.agent_text.clear();
            session.live_entries.clear();
            session.rendered_tail.clear();
            session.pending_follow_ups.clear();
            session.follow_up_request_entries.clear();
        }

        refresh_sidebar_threads();

        std::cout
            << "PASS: GTK requested a summary and title for thread "
            << thread_id
            << '\n';

        session.worker = std::thread(
            [
                this,
                session = &session,
                thread_id,
                turn_input,
                session_options
            ]() {
                AppServerClient::TurnResult result;
                std::string error;

                if (
                    ensure_session_client_ready(
                        *session,
                        session_options,
                        error)
                ) {
                    result =
                        session->client
                            ->start_turn_with_input(
                                thread_id,
                                turn_input,
                                60000,
                                [this](
                                    const AppServerClient::TurnEvent& event
                                ) {
                                    {
                                        std::lock_guard<
                                            std::mutex
                                        > lock(
                                            turn_event_mutex_);
                                        pending_turn_events_
                                            .push_back(event);
                                    }

                                    turn_event_dispatcher_.emit();
                                },
                                [this](
                                    const AppServerClient::ApprovalRequest&
                                        request
                                ) {
                                    return request_approval(
                                        request);
                                },
                                session_options);
                } else {
                    result.error =
                        "Could not prepare the thread's "
                        "Codex App Server: " +
                        error;
                }

                {
                    std::lock_guard<std::mutex> lock(
                        turn_result_mutex_);

                    pending_turn_results_.push_back(
                        {
                            thread_id,
                            std::move(result),
                        });
                }

                turn_dispatcher_.emit();
            });
    }

    void add_project_folder(
        const std::string& cwd,
        bool create_duplicate = false
    ) {
        if (cwd.empty()) {
            return;
        }

        std::string project_id;

        if (
            !create_duplicate &&
            !selected_project_id_.empty() &&
            project_cwd(selected_project_id_) == cwd
        ) {
            project_id = selected_project_id_;
        } else if (!create_duplicate) {
            project_id =
                primary_project_id_for_cwd(cwd);
        }

        if (project_id.empty()) {
            project_id =
                primary_project_id_for_cwd(cwd).empty()
                    ? cwd
                    : new_project_id();
            selected_project_folders_.push_back(
                project_id);
            project_paths_[project_id] = cwd;
        }

        selected_project_id_ = project_id;
        selected_folder_path_ = cwd;
        selected_folder_.set_text(cwd);

        new_thread_button_.set_sensitive(
            app_server_.is_running());

        save_ui_state();
    }

    void refresh_sidebar_threads() {
        refresh_active_thread_surfaces_from_labels();

        clear_sidebar_list();

        Gtk::Entry* editor_to_focus = nullptr;
        const std::string search_term =
            trim(sidebar_search_.get_text().raw());
        const bool searching =
            !search_term.empty();

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

        if (searching) {
            std::string search_status;

            if (thread_search_loading_) {
                search_status =
                    "Searching all thread messages…";
            } else if (!thread_search_error_.empty()) {
                search_status =
                    "Full-text search is unavailable: " +
                    thread_search_error_;
            }

            if (!search_status.empty()) {
                auto* status =
                    Gtk::manage(
                        new Gtk::Label(search_status));
                status->set_xalign(0.0F);
                status->set_line_wrap(true);
                sidebar_list_.pack_start(
                    *status,
                    Gtk::PACK_SHRINK);
            }
        }

        if (!searching && app_server_.is_running()) {
            for (const auto& project_id :
                 selected_project_folders_) {
                const std::string cwd =
                    project_cwd(project_id);
                if (thread_catalog_.find(cwd) !=
                    thread_catalog_.end()) {
                    continue;
                }

                const auto result =
                    app_server_.list_threads(
                        cwd,
                        100,
                        10000,
                        {},
                        true);

                if (result.success) {
                    thread_catalog_.emplace(
                        cwd,
                        result.threads);
                }
            }
        }

        auto project_folders =
            selected_project_folders_;
        sort_sidebar_projects(project_folders);

        for (
            const std::string& project_id :
            project_folders
        ) {
            const std::string cwd =
                project_cwd(project_id);
            const bool folder_collapsed =
                collapsed_project_folders_.find(project_id) !=
                collapsed_project_folders_.end();

            auto* folder_row =
                Gtk::manage(
                    new Gtk::Box(
                        Gtk::ORIENTATION_HORIZONTAL));

            folder_row->set_spacing(4);

            auto* folder_toggle_image =
                Gtk::manage(new Gtk::Image());

            folder_toggle_image->set_from_icon_name(
                folder_collapsed
                    ? "pan-end-symbolic"
                    : "pan-down-symbolic",
                Gtk::ICON_SIZE_BUTTON);

            auto* folder_toggle_button =
                Gtk::manage(new Gtk::Button());

            folder_toggle_button->set_image(
                *folder_toggle_image);
            folder_toggle_button->set_always_show_image(
                true);
            folder_toggle_button->set_relief(
                Gtk::RELIEF_NONE);
            folder_toggle_button->set_tooltip_text(
                folder_collapsed
                    ? "Show this project's threads"
                    : "Hide this project's threads");
            folder_toggle_button->get_style_context()
                ->add_class("sidebar-more-button");

            folder_toggle_button
                ->signal_clicked()
                .connect(
                    [this, project_id, folder_collapsed]() {
                        if (folder_collapsed) {
                            collapsed_project_folders_.erase(
                                project_id);
                        } else {
                            collapsed_project_folders_.insert(
                                project_id);
                        }

                        save_ui_state();
                        schedule_sidebar_refresh();
                    });

            folder_row->pack_start(
                *folder_toggle_button,
                Gtk::PACK_SHRINK);

            if (editing_folder_cwd_ == project_id) {
                auto* folder_entry =
                    Gtk::manage(
                        new Gtk::Entry());

                const auto custom_label =
                    folder_labels_.find(project_id);

                folder_entry->set_text(
                    custom_label != folder_labels_.end()
                        ? custom_label->second
                        : display_folder_label(project_id));

                folder_entry->set_hexpand(true);
                folder_entry->set_tooltip_text(
                    "Enter saves; Escape or clicking elsewhere cancels");
                folder_entry->get_style_context()
                    ->add_class(
                        "sidebar-label-editor");

                folder_entry
                    ->signal_key_press_event()
                    .connect(
                        [
                            this,
                            project_id,
                            folder_entry
                        ](GdkEventKey* event) {
                            return handle_label_edit_key(
                                event,
                                true,
                                project_id,
                                folder_entry);
                        },
                        false);

                folder_entry
                    ->signal_focus_out_event()
                    .connect(
                        [this, project_id](GdkEventFocus* event) {
                            return handle_label_edit_focus_out(
                                event,
                                true,
                                project_id);
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
                            display_folder_label(project_id)));

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
                "Project actions");
            folder_more_button->get_style_context()
                ->add_class(
                    "sidebar-more-button");

            auto* folder_actions =
                Gtk::manage(new Gtk::Menu());
            auto* rename_folder_item =
                Gtk::manage(
                    new Gtk::MenuItem(
                        "Rename project"));
            auto* delete_folder_item =
                Gtk::manage(
                    new Gtk::MenuItem(
                        "Delete project"));

            rename_folder_item
                ->signal_activate()
                .connect(
                    [this, project_id]() {
                        begin_folder_label_edit(
                            project_id);
                    });

            const bool folder_is_empty =
                !project_has_threads(project_id);
            delete_folder_item->set_sensitive(
                folder_is_empty);
            delete_folder_item->set_tooltip_text(
                folder_is_empty
                    ? "Delete this empty ThreadDeck project"
                    : "Move or delete every thread first");
            delete_folder_item->get_style_context()
                ->add_class("destructive-action");
            delete_folder_item
                ->signal_activate()
                .connect(
                    [this, project_id]() {
                        request_project_deletion(
                            project_id);
                    });

            folder_actions->append(
                *rename_folder_item);
            folder_actions->append(
                *delete_folder_item);
            folder_actions->attach_to_widget(
                *folder_more_button);
            folder_actions->show_all();

            folder_more_button
                ->signal_clicked()
                .connect(
                    [folder_more_button, folder_actions]() {
                        folder_actions->popup_at_widget(
                            folder_more_button,
                            Gdk::GRAVITY_SOUTH_EAST,
                            Gdk::GRAVITY_NORTH_EAST,
                            nullptr);
                    });

            folder_row->pack_end(
                *folder_more_button,
                Gtk::PACK_SHRINK);

            auto* folder_sort_image =
                Gtk::manage(new Gtk::Image());
            const std::string folder_sort_id =
                thread_sort_for_project(project_id);

            folder_sort_image->set_from_icon_name(
                folder_sort_id == "updated-desc" ||
                        folder_sort_id == "name-desc"
                    ? "view-sort-descending-symbolic"
                    : "view-sort-ascending-symbolic",
                Gtk::ICON_SIZE_BUTTON);

            auto* folder_sort_button =
                Gtk::manage(new Gtk::Button("Sort"));
            folder_sort_button->set_image(
                *folder_sort_image);
            folder_sort_button->set_always_show_image(true);
            folder_sort_button->set_image_position(
                Gtk::POS_LEFT);
            folder_sort_button->set_relief(
                Gtk::RELIEF_NONE);
            folder_sort_button->set_tooltip_text(
                "Sort threads in " +
                display_folder_label(project_id));
            folder_sort_button->get_style_context()
                ->add_class("sidebar-more-button");

            auto* folder_sort_menu =
                Gtk::manage(new Gtk::Menu());

            const std::array<
                std::pair<const char*, const char*>,
                4
            > folder_sort_choices = {{
                {"updated-desc", "Modified · newest first"},
                {"updated-asc", "Modified · oldest first"},
                {"name-asc", "Name · A to Z"},
                {"name-desc", "Name · Z to A"},
            }};

            for (const auto& choice : folder_sort_choices) {
                auto* item = Gtk::manage(
                    new Gtk::MenuItem(choice.second));
                const std::string sort_id = choice.first;

                item->signal_activate().connect(
                    [this, project_id, sort_id]() {
                        project_thread_sorts_[project_id] =
                            sort_id;
                        save_ui_state();
                        schedule_sidebar_refresh();
                    });
                folder_sort_menu->append(*item);
            }

            folder_sort_menu->attach_to_widget(
                *folder_sort_button);
            folder_sort_menu->show_all();
            folder_sort_button->signal_clicked().connect(
                [folder_sort_button, folder_sort_menu]() {
                    folder_sort_menu->popup_at_widget(
                        folder_sort_button,
                        Gdk::GRAVITY_SOUTH_EAST,
                        Gdk::GRAVITY_NORTH_EAST,
                        nullptr);
                });

            folder_row->pack_end(
                *folder_sort_button,
                Gtk::PACK_SHRINK);

            sidebar_list_.pack_start(
                *folder_row,
                Gtk::PACK_SHRINK);

            if (folder_collapsed && !searching) {
                continue;
            }

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

            std::vector<nlohmann::json> threads;
            std::string list_error;

            if (searching) {
                if (
                    thread_search_result_term_ ==
                    search_term
                ) {
                    threads = thread_search_results_;
                }

                std::set<std::string> result_ids;

                for (const auto& thread : threads) {
                    if (
                        thread.is_object() &&
                        thread.contains("id") &&
                        thread["id"].is_string()
                    ) {
                        result_ids.insert(
                            thread["id"]
                                .get<std::string>());
                    }
                }

                const auto cached =
                    thread_catalog_.find(cwd);

                if (cached != thread_catalog_.end()) {
                    for (const auto& thread : cached->second) {
                        if (
                            !thread.is_object() ||
                            !thread.contains("id") ||
                            !thread["id"].is_string()
                        ) {
                            continue;
                        }

                        const std::string thread_id =
                            thread["id"].get<std::string>();

                        if (
                            result_ids.find(thread_id) ==
                                result_ids.end() &&
                            (
                                contains_search_text(
                                    display_thread_label(
                                        thread),
                                    search_term) ||
                                contains_search_text(
                                    thread_id,
                                    search_term)
                            )
                        ) {
                            threads.push_back(thread);
                            result_ids.insert(thread_id);
                        }
                    }
                }
            } else {
                auto cached =
                    thread_catalog_.find(cwd);

                if (cached == thread_catalog_.end()) {
                    const auto result =
                        app_server_.list_threads(
                            cwd,
                            100,
                            10000,
                            {},
                            true);

                    if (result.success) {
                        cached =
                            thread_catalog_
                                .emplace(
                                    cwd,
                                    result.threads)
                                .first;
                    } else {
                        list_error = result.error;
                    }
                }

                if (cached != thread_catalog_.end()) {
                    threads = cached->second;
                }
            }

            if (list_error.empty()) {
                threads.erase(
                    std::remove_if(
                        threads.begin(),
                        threads.end(),
                        [this, &cwd, &project_id](
                            const nlohmann::json& thread
                        ) {
                            if (
                                !thread.is_object() ||
                                !thread.contains("id") ||
                                !thread["id"].is_string()
                            ) {
                                return false;
                            }

                            return !thread_belongs_to_project(
                                thread["id"].get<std::string>(),
                                project_id,
                                cwd);
                        }),
                    threads.end());

                std::set<std::string> listed_ids;
                threads.erase(
                    std::remove_if(
                        threads.begin(),
                        threads.end(),
                        [&listed_ids](
                            const nlohmann::json& thread
                        ) {
                            if (
                                !thread.is_object() ||
                                !thread.contains("id") ||
                                !thread["id"].is_string()
                            ) {
                                return false;
                            }

                            return !listed_ids.insert(
                                thread["id"]
                                    .get<std::string>())
                                .second;
                        }),
                    threads.end());

                for (const auto& assignment :
                     thread_project_assignments_) {
                    if (
                        assignment.second != project_id ||
                        listed_ids.find(assignment.first) !=
                            listed_ids.end()
                    ) {
                        continue;
                    }

                    const auto summary =
                        moved_thread_summaries_.find(
                            assignment.first);

                    if (
                        summary == moved_thread_summaries_.end() ||
                        (
                            searching &&
                            !contains_search_text(
                                display_thread_label(
                                    summary->second),
                                search_term) &&
                            !contains_search_text(
                                assignment.first,
                                search_term)
                        )
                    ) {
                        continue;
                    }

                    threads.push_back(summary->second);
                    listed_ids.insert(assignment.first);
                }

                sort_sidebar_threads(
                    threads,
                    thread_sort_for_project(project_id));
            }

            if (!list_error.empty()) {
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
                    << list_error
                    << '\n';

                continue;
            }

            bool displayed_thread = false;
            bool current_thread_was_listed = false;

            for (
                const auto& thread :
                threads
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

                const bool is_busy =
                    thread_is_busy(thread_id);
                const bool has_question =
                    thread_has_pending_approval(thread_id);
                const bool completed_unseen =
                    !is_busy &&
                    completed_unseen_threads_.find(thread_id) !=
                        completed_unseen_threads_.end();

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
                        "Enter saves; Escape or clicking elsewhere cancels");
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

                    thread_entry
                        ->signal_focus_out_event()
                        .connect(
                            [this, thread_id](GdkEventFocus* event) {
                                return handle_label_edit_focus_out(
                                    event,
                                    false,
                                    thread_id);
                            },
                            false);

                    thread_row->pack_start(
                        *thread_entry,
                        Gtk::PACK_EXPAND_WIDGET);

                    editor_to_focus = thread_entry;

                } else {
                    auto* thread_button =
                        Gtk::manage(
                            new Gtk::Button());

                    auto* thread_button_content =
                        Gtk::manage(
                            new Gtk::Box(
                                Gtk::ORIENTATION_HORIZONTAL));

                    thread_button_content->set_spacing(6);

                    if (is_busy) {
                        auto* spinner =
                            Gtk::manage(
                                new Gtk::Spinner());

                        spinner->set_tooltip_text(
                            has_question
                                ? "This thread is waiting for your approval"
                                : "Codex is working in this thread");
                        spinner->get_style_context()
                            ->add_class("working-spinner");
                        spinner->start();

                        thread_button_content->pack_start(
                            *spinner,
                            Gtk::PACK_SHRINK);

                    } else if (is_current) {
                        auto* current_dot =
                            Gtk::manage(
                                new Gtk::Label("●"));

                        current_dot->set_tooltip_text(
                            "Current thread");

                        thread_button_content->pack_start(
                            *current_dot,
                            Gtk::PACK_SHRINK);
                    }

                    auto* thread_name =
                        Gtk::manage(
                            new Gtk::Label(
                                display_thread_label(
                                    thread)));

                    thread_name->set_xalign(0.0F);
                    thread_name->set_hexpand(true);
                    thread_name->set_ellipsize(
                        Pango::ELLIPSIZE_END);

                    thread_button_content->pack_start(
                        *thread_name,
                        Gtk::PACK_EXPAND_WIDGET);

                    thread_button->add(
                        *thread_button_content);

                    thread_button->set_relief(
                        Gtk::RELIEF_NONE);
                    thread_button->set_alignment(
                        0.0F,
                        0.5F);
                    thread_button->set_hexpand(true);

                    std::string thread_tooltip =
                        thread_id;

                    if (
                        searching &&
                        thread.contains(
                            "_threaddeckSearchSnippet") &&
                        thread[
                            "_threaddeckSearchSnippet"
                        ].is_string()
                    ) {
                        const std::string snippet =
                            single_line_preview(
                                thread[
                                    "_threaddeckSearchSnippet"
                                ].get<std::string>(),
                                180);

                        if (!snippet.empty()) {
                            thread_tooltip +=
                                "\n\nMatch: " + snippet;
                        }
                    }

                    thread_button->set_tooltip_text(
                        thread_tooltip);

                    thread_button->get_style_context()
                        ->add_class("thread-row");

                    if (is_current) {
                        thread_button
                            ->get_style_context()
                            ->add_class(
                                "active-thread");

                        current_thread_was_listed = true;
                    }

                    if (is_busy) {
                        thread_button
                            ->get_style_context()
                            ->add_class(
                                "working-thread");
                    }

                    if (has_question) {
                        thread_button
                            ->get_style_context()
                            ->add_class(
                                "question-thread");

                        if (approval_blink_on_) {
                            thread_button
                                ->get_style_context()
                                ->add_class(
                                    "question-blink");
                        }

                        approval_question_rows_.push_back(
                            thread_button);
                    }

                    if (completed_unseen) {
                        thread_button
                            ->get_style_context()
                            ->add_class(
                                "completed-thread");
                        thread_button->set_tooltip_text(
                            "Codex finished working in this thread");
                    }

                    thread_button
                        ->signal_clicked()
                        .connect(
                            [
                                this,
                                cwd,
                                project_id,
                                thread_id
                            ]() {
                                activate_thread(
                                    cwd,
                                    thread_id,
                                    false,
                                    project_id);
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
                    "Thread actions");
                thread_more_button
                    ->get_style_context()
                    ->add_class(
                        "sidebar-more-button");

                auto* thread_actions =
                    Gtk::manage(
                        new Gtk::Menu());

                auto* rename_thread_item =
                    Gtk::manage(
                        new Gtk::MenuItem("Rename label"));

                rename_thread_item
                    ->signal_activate()
                    .connect(
                        [this, thread_id]() {
                            begin_thread_label_edit(
                                thread_id);
                        });

                auto* summarize_thread_item =
                    Gtk::manage(
                        new Gtk::MenuItem(
                            "Summarize and title"));

                summarize_thread_item->set_sensitive(
                    !is_busy);
                summarize_thread_item
                    ->signal_activate()
                    .connect(
                        [this, cwd, thread_id]() {
                            start_thread_summary_and_title(
                                cwd,
                                thread_id);
                        });

                auto* move_thread_item =
                    Gtk::manage(
                        new Gtk::MenuItem(
                            "Move to project"));
                auto* move_thread_menu =
                    Gtk::manage(new Gtk::Menu());

                for (const auto& destination_project_id :
                     selected_project_folders_) {
                    const std::string destination_cwd =
                        project_cwd(
                            destination_project_id);
                    std::string destination_label =
                        display_folder_label(
                            destination_project_id);

                    if (destination_project_id == project_id) {
                        destination_label += " (current)";
                    }

                    auto* destination_item =
                        Gtk::manage(
                            new Gtk::MenuItem(
                                destination_label));
                    destination_item->set_tooltip_text(
                        destination_cwd);
                    destination_item->set_sensitive(
                        !is_busy &&
                        destination_project_id != project_id);
                    destination_item
                        ->signal_activate()
                        .connect(
                            [
                                this,
                                thread_id,
                                project_id,
                                destination_project_id
                            ]() {
                                request_thread_move(
                                    thread_id,
                                    project_id,
                                    destination_project_id);
                            });
                    move_thread_menu->append(
                        *destination_item);
                }

                move_thread_item->set_submenu(
                    *move_thread_menu);
                move_thread_item->set_sensitive(
                    !is_busy &&
                    selected_project_folders_.size() > 1);

                auto* delete_thread_item =
                    Gtk::manage(
                        new Gtk::MenuItem("Delete thread"));

                delete_thread_item
                    ->get_style_context()
                    ->add_class("destructive-action");

                const std::string thread_label =
                    display_thread_label(thread);

                delete_thread_item
                    ->signal_activate()
                    .connect(
                        [
                            this,
                            thread_id,
                            thread_label
                        ]() {
                            request_thread_deletion(
                                thread_id,
                                thread_label);
                        });

                thread_actions->append(
                    *rename_thread_item);
                thread_actions->append(
                    *summarize_thread_item);
                thread_actions->append(
                    *move_thread_item);
                thread_actions->append(
                    *delete_thread_item);
                thread_actions->attach_to_widget(
                    *thread_more_button);
                thread_actions->show_all();

                thread_more_button
                    ->signal_clicked()
                    .connect(
                        [
                            thread_more_button,
                            thread_actions
                        ]() {
                            thread_actions->popup_at_widget(
                                thread_more_button,
                                Gdk::GRAVITY_SOUTH_EAST,
                                Gdk::GRAVITY_NORTH_EAST,
                                nullptr);
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
                !searching &&
                project_id == selected_project_id_ &&
                !current_thread_id_.empty() &&
                !current_thread_was_listed
            ) {
                const std::string pending_label =
                    current_thread_turn_failed_
                        ? "Current thread - turn failed "
                          "(not saved)"
                        : "Current thread "
                          "(not saved yet)";

                auto* pending_button =
                    Gtk::manage(
                        new Gtk::Button());

                auto* pending_content =
                    Gtk::manage(
                        new Gtk::Box(
                            Gtk::ORIENTATION_HORIZONTAL));

                pending_content->set_spacing(6);

                const bool pending_has_question =
                    thread_has_pending_approval(
                        current_thread_id_);

                if (thread_is_busy(current_thread_id_)) {
                    auto* spinner =
                        Gtk::manage(
                            new Gtk::Spinner());

                    spinner->set_tooltip_text(
                        pending_has_question
                            ? "This thread is waiting for your approval"
                            : "Codex is working in this thread");
                    spinner->get_style_context()
                        ->add_class("working-spinner");
                    spinner->start();
                    pending_content->pack_start(
                        *spinner,
                        Gtk::PACK_SHRINK);
                } else {
                    auto* current_dot =
                        Gtk::manage(
                            new Gtk::Label("●"));

                    pending_content->pack_start(
                        *current_dot,
                        Gtk::PACK_SHRINK);
                }

                auto* pending_name =
                    Gtk::manage(
                        new Gtk::Label(
                            pending_label));

                pending_name->set_xalign(0.0F);
                pending_name->set_hexpand(true);
                pending_name->set_ellipsize(
                    Pango::ELLIPSIZE_END);

                pending_content->pack_start(
                    *pending_name,
                    Gtk::PACK_EXPAND_WIDGET);

                pending_button->add(
                    *pending_content);

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

                if (thread_is_busy(current_thread_id_)) {
                    pending_button->get_style_context()
                        ->add_class("working-thread");
                }

                if (pending_has_question) {
                    pending_button->get_style_context()
                        ->add_class("question-thread");

                    if (approval_blink_on_) {
                        pending_button->get_style_context()
                            ->add_class("question-blink");
                    }

                    approval_question_rows_.push_back(
                        pending_button);
                }

                sidebar_list_.pack_start(
                    *pending_button,
                    Gtk::PACK_SHRINK);

                displayed_thread = true;
            }

            if (
                !displayed_thread &&
                !(searching && thread_search_loading_)
            ) {
                auto* empty_label =
                    Gtk::manage(
                        new Gtk::Label(
                            searching
                                ? "No matching threads."
                                : "No saved threads yet."));

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

    struct CodeCopyPayload {
        std::string text;
        std::string language;
    };

    std::string register_code_copy_payload(
        const std::string& payload,
        const std::string& language
    ) {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto hash_text = [&hash](const std::string& text) {
            for (const unsigned char character : text) {
                hash ^= character;
                hash *= 1099511628211ULL;
            }
        };

        hash_text(language);
        hash ^= 0xffU;
        hash *= 1099511628211ULL;
        hash_text(payload);

        std::ostringstream identity;
        identity << std::hex << hash;
        std::string marker =
            code_copy_button_marker_prefix() +
            identity.str() +
            code_copy_button_marker_suffix();

        const auto existing = code_copy_payloads_.find(marker);

        if (
            existing != code_copy_payloads_.end() &&
            (
                existing->second.text != payload ||
                existing->second.language != language
            )
        ) {
            marker =
                code_copy_button_marker_prefix() +
                identity.str() + "-" +
                std::to_string(++code_copy_marker_sequence_) +
                code_copy_button_marker_suffix();
        }

        code_copy_payloads_[marker] = {
            payload,
            language,
        };
        return marker;
    }

    static bool markdown_code_block_payload(
        const std::string& markdown,
        std::size_t content_start,
        const std::string& delimiter,
        std::string& payload
    ) {
        payload.clear();
        std::size_t line_start = content_start;

        while (line_start <= markdown.size()) {
            const std::size_t line_end =
                markdown.find('\n', line_start);
            const std::string line = markdown.substr(
                line_start,
                line_end == std::string::npos
                    ? std::string::npos
                    : line_end - line_start);

            if (line.rfind(delimiter, 0) == 0) {
                std::size_t payload_end = line_start;

                if (
                    payload_end > content_start &&
                    markdown[payload_end - 1] == '\n'
                ) {
                    --payload_end;
                }

                payload = markdown.substr(
                    content_start,
                    payload_end - content_start);
                return true;
            }

            if (line_end == std::string::npos) {
                break;
            }

            line_start = line_end + 1;
        }

        payload.clear();
        return false;
    }

    std::string decorate_markdown_code_blocks(
        const std::string& markdown
    ) {
        std::string decorated;
        bool inside_fence = false;
        std::size_t line_start = 0;

        while (line_start < markdown.size()) {
            const std::size_t line_end =
                markdown.find('\n', line_start);
            std::string line = markdown.substr(
                line_start,
                line_end == std::string::npos
                    ? std::string::npos
                    : line_end - line_start);

            const bool fence =
                line.rfind("```", 0) == 0 ||
                line.rfind("~~~", 0) == 0;

            if (fence) {
                const std::string language =
                    code_fence_language(line);

                if (!language.empty()) {
                    inside_fence = true;

                    std::string payload;
                    const std::size_t content_start =
                        line_end == std::string::npos
                            ? markdown.size()
                            : line_end + 1;

                    if (markdown_code_block_payload(
                            markdown,
                            content_start,
                            line.substr(0, 3),
                            payload)) {
                        line += "    ";
                        line += register_code_copy_payload(
                            payload,
                            language);
                    }
                } else if (inside_fence) {
                    inside_fence = false;
                } else {
                    inside_fence = true;
                }
            }

            decorated += line;

            if (line_end == std::string::npos) {
                break;
            }

            decorated += '\n';
            line_start = line_end + 1;
        }

        return decorated;
    }

    static std::string unwrap_outer_text_fence(
        const std::string& markdown,
        bool allow_incomplete = false
    ) {
        std::size_t opening_start = 0;

        while (
            opening_start < markdown.size() &&
            (
                markdown[opening_start] == ' ' ||
                markdown[opening_start] == '\t' ||
                markdown[opening_start] == '\r' ||
                markdown[opening_start] == '\n'
            )
        ) {
            ++opening_start;
        }

        const bool backtick_fence =
            markdown.compare(
                opening_start,
                7,
                "```text") == 0;
        const bool tilde_fence =
            markdown.compare(
                opening_start,
                7,
                "~~~text") == 0;

        if (!backtick_fence && !tilde_fence) {
            return markdown;
        }

        const std::size_t opening_end =
            markdown.find('\n', opening_start);

        if (opening_end == std::string::npos) {
            return markdown;
        }

        std::size_t content_end = markdown.size();

        while (
            content_end > opening_end + 1 &&
            (
                markdown[content_end - 1] == ' ' ||
                markdown[content_end - 1] == '\t' ||
                markdown[content_end - 1] == '\r' ||
                markdown[content_end - 1] == '\n'
            )
        ) {
            --content_end;
        }

        const std::string delimiter =
            backtick_fence ? "```" : "~~~";

        if (
            content_end < 3 ||
            markdown.compare(
                content_end - 3,
                3,
                delimiter) != 0
        ) {
            return
                allow_incomplete
                    ? markdown.substr(opening_end + 1)
                    : markdown;
        }

        std::size_t closing_start = content_end - 3;

        if (
            closing_start > opening_end + 1 &&
            markdown[closing_start - 1] == '\n'
        ) {
            --closing_start;
        }

        return markdown.substr(
            opening_end + 1,
            closing_start - (opening_end + 1));
    }

    static const std::string&
    code_copy_button_marker_prefix() {
        static const std::string marker = u8"\ue000";
        return marker;
    }

    static const std::string&
    code_copy_button_marker_suffix() {
        static const std::string marker = u8"\ue001";
        return marker;
    }

    static bool shell_code_language(
        const std::string& language
    ) {
        return
            language == "bash" ||
            language == "sh" ||
            language == "shell" ||
            language == "zsh" ||
            language == "console" ||
            language == "powershell" ||
            language == "pwsh";
    }

    static std::string last_shell_code_block(
        const std::string& markdown
    ) {
        std::string last_block;
        std::string current_block;
        std::string language;
        std::string delimiter;
        bool inside_fence = false;
        std::size_t line_start = 0;

        while (line_start < markdown.size()) {
            const std::size_t line_end =
                markdown.find('\n', line_start);
            const std::string line = markdown.substr(
                line_start,
                line_end == std::string::npos
                    ? std::string::npos
                    : line_end - line_start);

            if (!inside_fence) {
                if (
                    line.rfind("```", 0) == 0 ||
                    line.rfind("~~~", 0) == 0
                ) {
                    inside_fence = true;
                    delimiter = line.substr(0, 3);
                    language = code_fence_language(line);
                    current_block.clear();
                }
            } else if (line.rfind(delimiter, 0) == 0) {
                if (
                    shell_code_language(language) &&
                    !current_block.empty()
                ) {
                    if (
                        !current_block.empty() &&
                        current_block.back() == '\n'
                    ) {
                        current_block.pop_back();
                    }

                    last_block = current_block;
                }

                inside_fence = false;
                delimiter.clear();
                language.clear();
                current_block.clear();
            } else {
                current_block += line;

                if (line_end != std::string::npos) {
                    current_block += '\n';
                }
            }

            if (line_end == std::string::npos) {
                break;
            }

            line_start = line_end + 1;
        }

        return last_block;
    }

    void append_rendered_block(
        std::string& transcript,
        const std::string& heading,
        const std::string& body,
        bool allow_incomplete_outer_text_fence = false
    ) {
        if (body.empty()) {
            return;
        }

        if (!transcript.empty()) {
            transcript += "\n\n";
        }

        transcript += heading;
        transcript += ":\n";
        const std::string display_body =
            unwrap_outer_text_fence(
                body,
                allow_incomplete_outer_text_fence);
        transcript +=
            heading.rfind("Codex", 0) == 0
                ? decorate_markdown_code_blocks(
                    display_body)
                : display_body;
    }

    static void append_rendered_user_block(
        std::string& transcript,
        const std::string& body
    ) {
        if (body.empty()) {
            return;
        }

        if (!transcript.empty()) {
            transcript += "\n\n";
        }

        transcript += "[[THREADDECK_USER_INPUT]]";
        transcript += body;
        transcript += "[[THREADDECK_USER_INPUT_END]]";
    }

    void apply_code_token_tags(
        const Glib::RefPtr<Gtk::TextBuffer>& buffer,
        const Glib::ustring& line,
        int absolute_line_offset,
        const std::string& language
    ) {
        const std::string raw = line.raw();

        const auto apply_matches =
            [
                &buffer,
                &raw,
                absolute_line_offset
            ](
                const std::regex& expression,
                const Glib::RefPtr<Gtk::TextTag>& tag
            ) {
                for (
                    std::sregex_iterator match(
                        raw.begin(),
                        raw.end(),
                        expression),
                        end;
                    match != end;
                    ++match
                ) {
                    const int character_start =
                        static_cast<int>(
                            Glib::ustring(
                                raw.substr(
                                    0,
                                    static_cast<std::size_t>(
                                        match->position())))
                                .size());
                    const int character_length =
                        static_cast<int>(
                            Glib::ustring(
                                match->str())
                                .size());

                    auto start =
                        buffer->get_iter_at_offset(
                            absolute_line_offset +
                            character_start);
                    auto finish =
                        buffer->get_iter_at_offset(
                            absolute_line_offset +
                            character_start +
                            character_length);

                    buffer->apply_tag(
                        tag,
                        start,
                        finish);
                }
            };

        static const std::regex common_keywords(
            R"(\b(alignas|alignof|and|as|async|auto|await|bool|break|case|catch|char|class|const|constexpr|continue|def|delete|do|double|else|enum|except|export|extends|false|final|finally|float|fn|for|from|function|if|implements|import|in|int|interface|let|long|match|namespace|new|null|nullptr|of|override|package|private|protected|public|raise|return|short|signed|static|std|string|struct|super|switch|this|throw|true|try|type|typedef|typename|union|unsigned|using|var|virtual|void|while|yield)\b)");
        static const std::regex python_keywords(
            R"(\b(and|as|assert|async|await|break|class|continue|def|del|elif|else|except|False|finally|for|from|global|if|import|in|is|lambda|None|nonlocal|not|or|pass|raise|return|True|try|while|with|yield)\b)");
        static const std::regex javascript_keywords(
            R"(\b(as|async|await|break|case|catch|class|const|continue|debugger|default|delete|do|else|export|extends|false|finally|for|from|function|get|if|implements|import|in|instanceof|interface|let|new|null|of|package|private|protected|public|return|set|static|super|switch|this|throw|true|try|typeof|undefined|var|void|while|with|yield)\b)");
        static const std::regex shell_keywords(
            R"(\b(case|coproc|do|done|elif|else|esac|fi|for|function|if|in|select|then|time|until|while)\b)");
        static const std::regex sql_keywords(
            R"(\b(ALTER|AND|AS|ASC|BEGIN|BETWEEN|BY|CASE|COMMIT|CREATE|DELETE|DESC|DISTINCT|DROP|ELSE|END|EXISTS|FROM|FULL|GROUP|HAVING|IN|INNER|INSERT|INTO|IS|JOIN|LEFT|LIKE|LIMIT|NOT|NULL|ON|OR|ORDER|OUTER|RIGHT|ROLLBACK|SELECT|SET|TABLE|THEN|UNION|UPDATE|VALUES|WHEN|WHERE|WITH|alter|and|as|asc|begin|between|by|case|commit|create|delete|desc|distinct|drop|else|end|exists|from|full|group|having|in|inner|insert|into|is|join|left|like|limit|not|null|on|or|order|outer|right|rollback|select|set|table|then|union|update|values|when|where|with)\b)");
        static const std::regex numbers(
            R"(\b(0x[0-9a-fA-F]+|[0-9]+(?:\.[0-9]+)?)\b)");
        static const std::regex strings(
            R"(("(?:\\.|[^"\\])*")|('(?:\\.|[^'\\])*'))");
        static const std::regex c_comments(
            R"((//.*$|/\*.*\*/))");
        static const std::regex hash_comments(
            R"((#.*$))");
        static const std::regex sql_comments(
            R"((--.*$))");
        const std::regex* keywords = nullptr;
        const std::regex* comments = nullptr;
        bool highlight_tokens = false;

        if (
            language == "python" ||
            language == "py"
        ) {
            highlight_tokens = true;
            keywords = &python_keywords;
            comments = &hash_comments;
        } else if (
            language == "javascript" ||
            language == "js" ||
            language == "typescript" ||
            language == "ts" ||
            language == "jsx" ||
            language == "tsx"
        ) {
            highlight_tokens = true;
            keywords = &javascript_keywords;
            comments = &c_comments;
        } else if (
            language == "bash" ||
            language == "sh" ||
            language == "shell" ||
            language == "zsh"
        ) {
            highlight_tokens = true;
            keywords = &shell_keywords;
            comments = &hash_comments;
        } else if (language == "sql") {
            highlight_tokens = true;
            keywords = &sql_keywords;
            comments = &sql_comments;
        } else if (
            language == "c" ||
            language == "cpp" ||
            language == "c++" ||
            language == "csharp" ||
            language == "cs" ||
            language == "java" ||
            language == "go" ||
            language == "rust" ||
            language == "rs"
        ) {
            highlight_tokens = true;
            keywords = &common_keywords;
            comments = &c_comments;
        } else if (
            language == "ruby" ||
            language == "rb"
        ) {
            highlight_tokens = true;
            keywords = &common_keywords;
            comments = &hash_comments;
        } else if (
            language == "yaml" ||
            language == "yml"
        ) {
            highlight_tokens = true;
            comments = &hash_comments;
        }

        if (!highlight_tokens) {
            return;
        }

        if (keywords != nullptr) {
            apply_matches(
                *keywords,
                transcript_code_keyword_tag_);
        }
        apply_matches(
            numbers,
            transcript_code_number_tag_);
        apply_matches(
            strings,
            transcript_code_string_tag_);
        if (comments != nullptr) {
            apply_matches(
                *comments,
                transcript_code_comment_tag_);
        }
    }

    static std::string code_fence_language(
        const std::string& fence
    ) {
        if (
            fence.rfind("```", 0) != 0 &&
            fence.rfind("~~~", 0) != 0
        ) {
            return {};
        }

        std::string language = fence.substr(3);
        const auto first =
            language.find_first_not_of(" \t{.");
        const auto last =
            language.find_last_not_of(" \t}");

        if (first == std::string::npos) {
            return {};
        }

        language = language.substr(
            first,
            last - first + 1);

        const auto token_end =
            language.find_first_of(" \t");

        if (token_end != std::string::npos) {
            language.resize(token_end);
        }

        std::transform(
            language.begin(),
            language.end(),
            language.begin(),
            [](unsigned char character) {
                return static_cast<char>(
                    std::tolower(character));
            });
        return language;
    }

    void apply_transcript_tags_to_buffer(
        const Glib::RefPtr<Gtk::TextBuffer>& buffer,
        int requested_start_offset,
        bool initial_inside_code_fence = false,
        bool initial_inside_user_section = false,
        bool initial_inside_diff_activity = false,
        const std::string& initial_code_language = {}
    ) {
        if (!transcript_code_tag_) {
            return;
        }

        const int character_count =
            buffer->get_char_count();
        const int start_offset =
            std::clamp(
                requested_start_offset,
                0,
                character_count);

        auto range_start =
            buffer->get_iter_at_offset(start_offset);
        auto range_end = buffer->end();

        for (
            const auto& tag :
            {
                transcript_user_tag_,
                transcript_codex_tag_,
                transcript_commentary_tag_,
                transcript_reasoning_tag_,
                transcript_activity_tag_,
                transcript_error_tag_,
                transcript_section_heading_tag_,
                transcript_user_section_tag_,
                transcript_user_marker_tag_,
                transcript_user_top_padding_tag_,
                transcript_user_bottom_padding_tag_,
                transcript_expand_activity_tag_,
                transcript_expand_token_tag_,
                transcript_code_tag_,
                transcript_code_header_tag_,
                transcript_code_copy_tag_,
                transcript_markdown_marker_tag_,
                transcript_markdown_heading_tag_,
                transcript_markdown_bold_tag_,
                transcript_markdown_inline_code_tag_,
                transcript_markdown_quote_tag_,
                transcript_markdown_list_tag_,
                transcript_markdown_link_tag_,
                transcript_code_keyword_tag_,
                transcript_code_string_tag_,
                transcript_code_comment_tag_,
                transcript_code_number_tag_,
                transcript_diff_add_tag_,
                transcript_diff_delete_tag_,
                transcript_diff_header_tag_,
                transcript_command_tag_,
            }
        ) {
            buffer->remove_tag(
                tag,
                range_start,
                range_end);
        }

        const Glib::ustring text =
            buffer->get_slice(
                range_start,
                range_end,
                true);

        bool inside_code_fence =
            initial_inside_code_fence;
        bool inside_diff_activity =
            initial_inside_diff_activity;
        std::string code_language =
            initial_code_language;
        int user_section_start =
            initial_inside_user_section
                ? start_offset
                : -1;
        constexpr const char* user_marker =
            "[[THREADDECK_USER_INPUT]]";
        constexpr const char* user_end_marker =
            "[[THREADDECK_USER_INPUT_END]]";
        const int user_marker_length =
            static_cast<int>(
                Glib::ustring(user_marker).size());

        const auto apply_user_section =
            [this, &buffer](
                int content_start,
                int content_end
            ) {
                if (content_end <= content_start) {
                    return;
                }

                auto section_start =
                    buffer->get_iter_at_offset(
                        content_start);
                auto section_end =
                    buffer->get_iter_at_offset(
                        content_end);
                buffer->apply_tag(
                    transcript_user_section_tag_,
                    section_start,
                    section_end);

                auto first_character_end =
                    section_start;
                first_character_end.forward_char();
                buffer->apply_tag(
                    transcript_user_top_padding_tag_,
                    section_start,
                    first_character_end);

                auto last_character_start =
                    section_end;
                last_character_start.set_line_offset(0);
                buffer->apply_tag(
                    transcript_user_bottom_padding_tag_,
                    last_character_start,
                    section_end);
            };
        Glib::ustring::size_type line_start = 0;

        while (line_start <= text.size()) {
            const auto newline =
                text.find('\n', line_start);
            const auto line_end =
                newline == Glib::ustring::npos
                    ? text.size()
                    : newline;
            const Glib::ustring line =
                text.substr(
                    line_start,
                    line_end - line_start);
            const std::string raw = line.raw();
            const int absolute_start =
                start_offset +
                static_cast<int>(line_start);
            const int absolute_end =
                start_offset +
                static_cast<int>(line_end);

            auto start =
                buffer->get_iter_at_offset(
                    absolute_start);
            auto finish =
                buffer->get_iter_at_offset(
                    absolute_end);
            constexpr const char* expand_token_prefix =
                "[[THREADDECK_EXPAND_";
            const std::size_t expand_token_position =
                raw.find(expand_token_prefix);
            auto visible_line_finish = finish;

            if (
                expand_token_position !=
                std::string::npos
            ) {
                const int token_character_offset =
                    static_cast<int>(
                        Glib::ustring(
                            raw.substr(
                                0,
                                expand_token_position))
                            .size());
                auto token_start =
                    buffer->get_iter_at_offset(
                        absolute_start +
                        token_character_offset);
                visible_line_finish = token_start;
                buffer->apply_tag(
                    transcript_expand_token_tag_,
                    token_start,
                    finish);
            }

            const bool fence =
                raw.rfind("```", 0) == 0 ||
                raw.rfind("~~~", 0) == 0;
            const std::size_t user_marker_position =
                raw.find(user_marker);
            const std::size_t user_end_marker_position =
                raw.find(user_end_marker);
            const bool internal_user_marker =
                user_marker_position == 0;
            const bool user_heading =
                !inside_code_fence &&
                (
                    internal_user_marker ||
                    raw == "You:" ||
                    raw.rfind("You · ", 0) == 0 ||
                    raw == "Follow-up:"
                );
            const bool section_heading =
                !inside_code_fence &&
                (
                    user_heading ||
                    raw == "Codex:" ||
                    raw == "Codex commentary:" ||
                    raw == "Codex reasoning:" ||
                    raw == "Codex plan:" ||
                    raw == "Activity:" ||
                    raw == "Hook:" ||
                    raw == "Follow-up rejected:" ||
                    raw.rfind("Codex error", 0) == 0 ||
                    raw.rfind("Codex transport error", 0) == 0 ||
                    raw.rfind("Codex compaction error", 0) == 0 ||
                    raw.rfind("Shell command error", 0) == 0 ||
                    (
                        raw.rfind("Codex ", 0) == 0 &&
                        !raw.empty() &&
                        raw.back() == ':'
                    )
                );

            if (section_heading) {
                inside_diff_activity =
                    raw == "Codex changed files:" ||
                    raw == "Codex changing files:";

                if (user_section_start >= 0) {
                    apply_user_section(
                        user_section_start,
                        absolute_start);
                    user_section_start = -1;
                }

                if (internal_user_marker) {
                    const int marker_character_offset =
                        static_cast<int>(
                            Glib::ustring(
                                raw.substr(
                                    0,
                                    user_marker_position))
                                .size());
                    user_section_start =
                        absolute_start +
                        marker_character_offset;
                } else if (user_heading) {
                    user_section_start = absolute_start;
                }

                if (!internal_user_marker) {
                    buffer->apply_tag(
                        transcript_section_heading_tag_,
                        start,
                        finish);
                }
            }

            if (fence) {
                if (inside_code_fence) {
                    buffer->apply_tag(
                        transcript_markdown_marker_tag_,
                        start,
                        finish);
                    inside_code_fence = false;
                    code_language.clear();
                } else {
                    buffer->apply_tag(
                        transcript_code_header_tag_,
                        start,
                        finish);

                    auto delimiter_finish = start;
                    delimiter_finish.forward_chars(
                        std::min(
                            3,
                            absolute_end -
                                absolute_start));
                    buffer->apply_tag(
                        transcript_markdown_marker_tag_,
                        start,
                        delimiter_finish);

                    const std::size_t copy_position =
                        raw.find(
                            code_copy_button_marker_prefix());

                    if (copy_position != std::string::npos) {
                        const std::size_t copy_end =
                            raw.find(
                                code_copy_button_marker_suffix(),
                                copy_position +
                                    code_copy_button_marker_prefix()
                                        .size());

                        if (copy_end != std::string::npos) {
                            const std::size_t copy_byte_length =
                                copy_end +
                                code_copy_button_marker_suffix()
                                    .size() -
                                copy_position;
                            const int copy_character_offset =
                                static_cast<int>(
                                    Glib::ustring(
                                        raw.substr(
                                            0,
                                            copy_position))
                                        .size());
                            const int copy_character_length =
                                static_cast<int>(
                                    Glib::ustring(
                                        raw.substr(
                                            copy_position,
                                            copy_byte_length))
                                        .size());
                            auto copy_start =
                                buffer->get_iter_at_offset(
                                    absolute_start +
                                    copy_character_offset);
                            auto copy_finish =
                                buffer->get_iter_at_offset(
                                    absolute_start +
                                    copy_character_offset +
                                    copy_character_length);

                            buffer->apply_tag(
                                transcript_markdown_marker_tag_,
                                copy_start,
                                copy_finish);
                        }
                    }

                    inside_code_fence = true;
                    code_language =
                        code_fence_language(raw);
                }
            } else if (inside_code_fence) {
                if (
                    code_language == "diff" ||
                    code_language == "patch"
                ) {
                    if (
                        raw.rfind("diff --git ", 0) == 0 ||
                        raw.rfind("index ", 0) == 0 ||
                        raw.rfind("@@", 0) == 0 ||
                        raw.rfind("--- ", 0) == 0 ||
                        raw.rfind("+++ ", 0) == 0
                    ) {
                        buffer->apply_tag(
                            transcript_diff_header_tag_,
                            start,
                            finish);
                    } else if (
                        !raw.empty() &&
                        raw.front() == '+'
                    ) {
                        buffer->apply_tag(
                            transcript_diff_add_tag_,
                            start,
                            finish);
                    } else if (
                        !raw.empty() &&
                        raw.front() == '-'
                    ) {
                        buffer->apply_tag(
                            transcript_diff_delete_tag_,
                            start,
                            finish);
                    } else {
                        buffer->apply_tag(
                            transcript_code_tag_,
                            start,
                            finish);
                    }
                } else {
                    buffer->apply_tag(
                        transcript_code_tag_,
                        start,
                        finish);
                    apply_code_token_tags(
                        buffer,
                        line,
                        absolute_start,
                        code_language);
                }
            } else if (
                inside_diff_activity &&
                (
                    raw.rfind("diff --git ", 0) == 0 ||
                    raw.rfind("index ", 0) == 0 ||
                    raw.rfind("@@", 0) == 0 ||
                    raw.rfind("--- ", 0) == 0 ||
                    raw.rfind("+++ ", 0) == 0
                )
            ) {
                buffer->apply_tag(
                    transcript_diff_header_tag_,
                    start,
                    finish);
            } else if (
                inside_diff_activity &&
                !raw.empty() &&
                raw.front() == '+'
            ) {
                buffer->apply_tag(
                    transcript_diff_add_tag_,
                    start,
                    finish);
            } else if (
                inside_diff_activity &&
                !raw.empty() &&
                raw.front() == '-'
            ) {
                buffer->apply_tag(
                    transcript_diff_delete_tag_,
                    start,
                    finish);
            } else if (
                internal_user_marker ||
                (
                    user_section_start >= 0 &&
                    user_end_marker_position !=
                        std::string::npos
                )
            ) {
                // Internal user-block markers are hidden below.
            } else if (
                raw == "You:" ||
                raw.rfind("You · ", 0) == 0 ||
                raw == "Follow-up:"
            ) {
                buffer->apply_tag(
                    transcript_user_tag_,
                    start,
                    finish);
            } else if (
                raw == "Codex commentary:"
            ) {
                buffer->apply_tag(
                    transcript_commentary_tag_,
                    start,
                    finish);
            } else if (
                raw == "Codex reasoning:"
            ) {
                buffer->apply_tag(
                    transcript_reasoning_tag_,
                    start,
                    finish);
            } else if (
                raw.rfind("Codex error", 0) == 0 ||
                raw.rfind("Codex transport error", 0) == 0 ||
                raw.rfind("Codex compaction error", 0) == 0 ||
                raw.rfind("Shell command error", 0) == 0
            ) {
                buffer->apply_tag(
                    transcript_error_tag_,
                    start,
                    finish);
            } else if (
                raw == "Codex:" ||
                raw == "Codex plan:"
            ) {
                buffer->apply_tag(
                    transcript_codex_tag_,
                    start,
                    finish);
            } else if (
                raw.rfind("Codex ", 0) == 0 &&
                !raw.empty() &&
                raw.back() == ':'
            ) {
                buffer->apply_tag(
                    transcript_activity_tag_,
                    start,
                    finish);
            } else if (
                raw.rfind("$ ", 0) == 0 ||
                raw.rfind("↳ ", 0) == 0
            ) {
                buffer->apply_tag(
                    transcript_command_tag_,
                    start,
                    finish);
            } else if (
                raw.rfind("… +", 0) == 0 ||
                raw.rfind(
                    "… (click to expand full details)",
                    0) == 0 ||
                raw.rfind(
                    "… (click to re-abbreviate)",
                    0) == 0
            ) {
                buffer->apply_tag(
                    transcript_expand_activity_tag_,
                    start,
                    visible_line_finish);
            } else {
                const auto apply_line_tag =
                    [&buffer, absolute_start](
                        const Glib::RefPtr<Gtk::TextTag>& tag,
                        Glib::ustring::size_type begin,
                        Glib::ustring::size_type end
                    ) {
                        if (end <= begin) {
                            return;
                        }

                        auto tagged_start =
                            buffer->get_iter_at_offset(
                                absolute_start +
                                static_cast<int>(begin));
                        auto tagged_end =
                            buffer->get_iter_at_offset(
                                absolute_start +
                                static_cast<int>(end));
                        buffer->apply_tag(
                            tag,
                            tagged_start,
                            tagged_end);
                    };

                Glib::ustring::size_type heading_length = 0;

                while (
                    heading_length < line.size() &&
                    heading_length < 6 &&
                    line[heading_length] == '#'
                ) {
                    ++heading_length;
                }

                if (
                    heading_length > 0 &&
                    heading_length < line.size() &&
                    line[heading_length] == ' '
                ) {
                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        0,
                        heading_length + 1);
                    apply_line_tag(
                        transcript_markdown_heading_tag_,
                        heading_length + 1,
                        line.size());
                }

                Glib::ustring::size_type content_start = 0;

                while (
                    content_start < line.size() &&
                    (
                        line[content_start] == ' ' ||
                        line[content_start] == '\t'
                    )
                ) {
                    ++content_start;
                }

                if (
                    content_start + 1 < line.size() &&
                    (
                        line[content_start] == '-' ||
                        line[content_start] == '*' ||
                        line[content_start] == '+'
                    ) &&
                    line[content_start + 1] == ' '
                ) {
                    buffer->apply_tag(
                        transcript_markdown_list_tag_,
                        start,
                        finish);
                } else if (
                    content_start + 1 < line.size() &&
                    line[content_start] == '>' &&
                    line[content_start + 1] == ' '
                ) {
                    buffer->apply_tag(
                        transcript_markdown_quote_tag_,
                        start,
                        finish);
                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        content_start,
                        content_start + 2);
                }

                Glib::ustring::size_type bold_search = 0;

                while (bold_search < line.size()) {
                    const auto open =
                        line.find("**", bold_search);

                    if (open == Glib::ustring::npos) {
                        break;
                    }

                    const auto close =
                        line.find("**", open + 2);

                    if (close == Glib::ustring::npos) {
                        break;
                    }

                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        open,
                        open + 2);
                    apply_line_tag(
                        transcript_markdown_bold_tag_,
                        open + 2,
                        close);
                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        close,
                        close + 2);
                    bold_search = close + 2;
                }

                const auto character_offset =
                    [&raw](std::size_t byte_offset) {
                        return static_cast<
                            Glib::ustring::size_type>(
                                Glib::ustring(
                                    raw.substr(
                                        0,
                                        byte_offset))
                                    .size());
                    };
                static const std::regex markdown_link(
                    R"(\[([^\]]+)\]\((https?://[A-Za-z0-9][^\s\)]*)\))");
                std::vector<
                    std::pair<
                        Glib::ustring::size_type,
                        Glib::ustring::size_type>>
                    markdown_url_ranges;

                for (
                    std::sregex_iterator link(
                        raw.begin(),
                        raw.end(),
                        markdown_link),
                        end;
                    link != end;
                    ++link
                ) {
                    const std::size_t full_byte_start =
                        static_cast<std::size_t>(
                            link->position());
                    const std::size_t full_byte_end =
                        full_byte_start +
                        static_cast<std::size_t>(
                            link->length());
                    const std::size_t label_byte_start =
                        static_cast<std::size_t>(
                            (*link).position(1));
                    const std::size_t label_byte_end =
                        label_byte_start +
                        static_cast<std::size_t>(
                            (*link).length(1));
                    const std::size_t url_byte_start =
                        static_cast<std::size_t>(
                            (*link).position(2));
                    const std::size_t url_byte_end =
                        url_byte_start +
                        static_cast<std::size_t>(
                            (*link).length(2));
                    const auto full_start =
                        character_offset(full_byte_start);
                    const auto full_end =
                        character_offset(full_byte_end);
                    const auto label_start =
                        character_offset(label_byte_start);
                    const auto label_end =
                        character_offset(label_byte_end);
                    const auto url_start =
                        character_offset(url_byte_start);
                    const auto url_end =
                        character_offset(url_byte_end);

                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        full_start,
                        label_start);
                    apply_line_tag(
                        transcript_markdown_link_tag_,
                        label_start,
                        label_end);
                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        label_end,
                        full_end);
                    markdown_url_ranges.emplace_back(
                        url_start,
                        url_end);
                }

                static const std::regex bare_link(
                    R"URL((https?://[A-Za-z0-9][^\s<>()\[\]{}'"`]+))URL");

                for (
                    std::sregex_iterator link(
                        raw.begin(),
                        raw.end(),
                        bare_link),
                        end;
                    link != end;
                    ++link
                ) {
                    std::size_t byte_start =
                        static_cast<std::size_t>(
                            link->position());
                    std::size_t byte_end =
                        byte_start +
                        static_cast<std::size_t>(
                            link->length());

                    while (
                        byte_end > byte_start &&
                        (
                            raw[byte_end - 1] == '.' ||
                            raw[byte_end - 1] == ',' ||
                            raw[byte_end - 1] == ';' ||
                            raw[byte_end - 1] == ':' ||
                            raw[byte_end - 1] == '!' ||
                            raw[byte_end - 1] == '?'
                        )
                    ) {
                        --byte_end;
                    }

                    const auto url_start =
                        character_offset(byte_start);
                    const auto url_end =
                        character_offset(byte_end);
                    const bool hidden_markdown_url =
                        std::any_of(
                            markdown_url_ranges.begin(),
                            markdown_url_ranges.end(),
                            [url_start, url_end](
                                const auto& range
                            ) {
                                return
                                    url_start >= range.first &&
                                    url_end <= range.second;
                            });

                    if (!hidden_markdown_url) {
                        apply_line_tag(
                            transcript_markdown_link_tag_,
                            url_start,
                            url_end);
                    }
                }

                Glib::ustring::size_type search = 0;

                while (search < line.size()) {
                    const auto open =
                        line.find('`', search);

                    if (open == Glib::ustring::npos) {
                        break;
                    }

                    const auto close =
                        line.find('`', open + 1);

                    if (close == Glib::ustring::npos) {
                        break;
                    }

                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        open,
                        open + 1);
                    apply_line_tag(
                        transcript_markdown_inline_code_tag_,
                        open + 1,
                        close);
                    apply_line_tag(
                        transcript_markdown_marker_tag_,
                        close,
                        close + 1);
                    search = close + 1;
                }
            }

            if (internal_user_marker) {
                const int marker_character_offset =
                    static_cast<int>(
                        Glib::ustring(
                            raw.substr(
                                0,
                                user_marker_position))
                            .size());
                auto marker_start =
                    buffer->get_iter_at_offset(
                        absolute_start +
                        marker_character_offset);
                auto marker_finish =
                    buffer->get_iter_at_offset(
                        absolute_start +
                        marker_character_offset +
                        user_marker_length);
                buffer->apply_tag(
                    transcript_user_marker_tag_,
                    marker_start,
                    marker_finish);
            }

            if (
                user_section_start >= 0 &&
                user_end_marker_position != std::string::npos
            ) {
                const int end_marker_character_offset =
                    static_cast<int>(
                        Glib::ustring(
                            raw.substr(
                                0,
                                user_end_marker_position))
                            .size());
                const int end_marker_length =
                    static_cast<int>(
                        Glib::ustring(
                            user_end_marker).size());
                const int content_end =
                    absolute_start +
                    end_marker_character_offset;
                auto marker_start =
                    buffer->get_iter_at_offset(
                        content_end);
                auto marker_finish =
                    buffer->get_iter_at_offset(
                        content_end +
                        end_marker_length);
                buffer->apply_tag(
                    transcript_user_marker_tag_,
                    marker_start,
                    marker_finish);

                if (user_section_start >= 0) {
                    apply_user_section(
                        user_section_start,
                        content_end);
                    user_section_start = -1;
                }

                inside_code_fence = false;
            }

            if (newline == Glib::ustring::npos) {
                break;
            }

            line_start = newline + 1;
        }

        if (user_section_start >= 0) {
            apply_user_section(
                user_section_start,
                range_end.get_offset());
        }

        materialize_code_copy_buttons(buffer);
    }

    void apply_transcript_tags(
        int requested_start_offset,
        bool initial_inside_code_fence = false,
        bool initial_inside_user_section = false,
        bool initial_inside_diff_activity = false,
        const std::string& initial_code_language = {}
    ) {
        apply_transcript_tags_to_buffer(
            transcript_.get_buffer(),
            requested_start_offset,
            initial_inside_code_fence,
            initial_inside_user_section,
            initial_inside_diff_activity,
            initial_code_language);
    }

    struct TranscriptImage {
        std::string marker;
        std::string path;
    };

    struct TranscriptCopyButton {
        Glib::RefPtr<Gtk::TextBuffer> buffer;
        Glib::RefPtr<Gtk::TextBuffer::ChildAnchor>
            anchor;
        std::string marker;
        std::unique_ptr<Gtk::Image> image;
        std::unique_ptr<Gtk::Button> button;
    };

    struct IncrementalTranscriptRender {
        std::string thread_id;
        std::string text;
        std::vector<TranscriptImage> images;
        Glib::RefPtr<Gtk::TextBuffer> buffer;
        std::size_t byte_offset{0};
        std::size_t generation{0};
        bool inside_code_fence{false};
        bool inside_user_section{false};
        bool inside_diff_activity{false};
        std::string code_language;
    };

    std::string render_user_content(
        const nlohmann::json& content,
        std::vector<TranscriptImage>& images
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
                TranscriptImage image;
                image.marker =
                    "[[THREADDECK_IMAGE_" +
                    std::to_string(
                        ++transcript_image_marker_sequence_) +
                    "]]";
                image.path = input.value(
                    "path",
                    std::string{});

                append_line(image.marker);
                images.push_back(
                    std::move(image));

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

    void materialize_transcript_images(
        const Glib::RefPtr<Gtk::TextBuffer>& buffer,
        const std::string& rendered,
        const std::vector<TranscriptImage>& images,
        int base_character_offset = 0
    ) {
        const int maximum_width = std::clamp(
            transcript_scroll_.get_allocated_width() - 72,
            240,
            720);

        for (
            auto image = images.rbegin();
            image != images.rend();
            ++image
        ) {
            const std::size_t marker_position =
                rendered.find(image->marker);

            if (marker_position == std::string::npos) {
                continue;
            }

            const int start_offset =
                base_character_offset +
                static_cast<int>(
                    Glib::ustring(
                        rendered.substr(
                            0,
                            marker_position))
                        .size());

            auto start =
                buffer->get_iter_at_offset(
                    start_offset);
            auto end =
                buffer->get_iter_at_offset(
                    start_offset +
                    static_cast<int>(
                        Glib::ustring(
                            image->marker)
                            .size()));

            buffer->erase(start, end);
            auto insertion =
                buffer->get_iter_at_offset(
                    start_offset);

            try {
                buffer->insert_pixbuf(
                    insertion,
                    load_image_preview(
                        image->path,
                        maximum_width,
                        480));
            } catch (...) {
                buffer->insert(
                    insertion,
                    "[Image unavailable: " +
                    std::filesystem::path(
                        image->path)
                        .filename()
                        .string() +
                    "]");

                std::cerr
                    << "WARN: could not restore transcript image "
                    << image->path
                    << '\n';
            }
        }
    }

    static std::string json_string_field(
        const nlohmann::json& object,
        const char* key,
        const std::string& fallback = {}
    ) {
        if (!object.is_object()) {
            return fallback;
        }

        const auto value = object.find(key);

        if (
            value == object.end() ||
            !value->is_string()
        ) {
            return fallback;
        }

        return value->get<std::string>();
    }

    static std::string json_display_field(
        const nlohmann::json& object,
        const char* key,
        const std::string& fallback = {}
    ) {
        if (!object.is_object()) {
            return fallback;
        }

        const auto value = object.find(key);

        if (
            value == object.end() ||
            value->is_null()
        ) {
            return fallback;
        }

        if (value->is_string()) {
            return value->get<std::string>();
        }

        return value->dump();
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
            json_string_field(item, "status");

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
            json_string_field(item, "type") ==
            "subAgentActivity"
        ) {
            const std::string kind =
                json_string_field(item, "kind");

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
            json_string_field(item, "type");

        if (type == "commandExecution") {
            return json_display_field(
                item,
                "command");
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
                        json_string_field(
                            change,
                            "path");

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
                json_string_field(
                    item,
                    "server");

            const std::string tool =
                json_string_field(
                    item,
                    "tool");

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
                json_string_field(
                    item,
                    "tool");

            if (name_space.empty()) {
                return tool;
            }

            if (tool.empty()) {
                return name_space;
            }

            return name_space + "::" + tool;
        }

        if (type == "collabAgentToolCall") {
            return json_string_field(
                item,
                "tool");
        }

        if (type == "subAgentActivity") {
            return json_string_field(
                item,
                "agentPath");
        }

        if (type == "webSearch") {
            return json_display_field(
                item,
                "query");
        }

        if (type == "imageView") {
            return json_string_field(
                item,
                "path");
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

    static std::string activity_heading(
        const nlohmann::json& item,
        const std::string& turn_status
    ) {
        const std::string type =
            json_string_field(item, "type");

        const std::string state =
            activity_state(
                item,
                turn_status,
                "completed");

        if (type == "commandExecution") {
            if (state == "running") {
                return "Codex running command";
            }

            if (
                state == "failed" ||
                state == "declined"
            ) {
                return "Codex command " + state;
            }

            return "Codex ran command";
        }

        if (type == "fileChange") {
            return state == "running"
                ? "Codex changing files"
                : "Codex changed files";
        }

        if (
            type == "mcpToolCall" ||
            type == "dynamicToolCall"
        ) {
            return state == "running"
                ? "Codex calling tool"
                : "Codex called tool";
        }

        if (type == "webSearch") {
            return state == "running"
                ? "Codex searching the web"
                : "Codex searched the web";
        }

        if (type == "imageView") {
            return "Codex viewed image";
        }

        if (type == "imageGeneration") {
            return state == "running"
                ? "Codex generating image"
                : "Codex generated image";
        }

        if (
            type == "collabAgentToolCall" ||
            type == "subAgentActivity"
        ) {
            return state == "running"
                ? "Codex coordinating agent"
                : "Codex agent activity";
        }

        return "Codex " +
            activity_label(type);
    }

    static void append_activity_result_text(
        std::string& rendered,
        const nlohmann::json& items
    ) {
        if (!items.is_array()) {
            return;
        }

        for (const auto& item : items) {
            if (!item.is_object()) {
                continue;
            }

            const std::string text =
                json_string_field(item, "text");

            if (text.empty()) {
                continue;
            }

            if (!rendered.empty()) {
                rendered += '\n';
            }

            rendered += text;
        }
    }

    static std::string compact_activity_text(
        const std::string& text,
        std::size_t maximum_lines = 6,
        std::size_t maximum_line_length = 180
    ) {
        if (text.empty()) {
            return {};
        }

        std::vector<std::string> lines;
        std::size_t start = 0;

        while (start <= text.size()) {
            const std::size_t end =
                text.find('\n', start);

            lines.push_back(
                text.substr(
                    start,
                    end == std::string::npos
                        ? std::string::npos
                        : end - start));

            if (end == std::string::npos) {
                break;
            }

            start = end + 1;
        }

        if (
            lines.size() > 1 &&
            lines.back().empty()
        ) {
            lines.pop_back();
        }

        bool line_was_truncated = false;
        const auto compact_line =
            [maximum_line_length, &line_was_truncated](
                const std::string& line
            ) {
                if (
                    maximum_line_length == 0 ||
                    line.size() <= maximum_line_length
                ) {
                    return line;
                }

                line_was_truncated = true;
                return
                    line.substr(
                        0,
                        maximum_line_length) +
                    "…";
            };

        std::ostringstream rendered;
        const std::size_t visible_lines =
            std::min(maximum_lines, lines.size());

        for (
            std::size_t index = 0;
            index < visible_lines;
            ++index
        ) {
            if (index > 0) {
                rendered << '\n';
            }

            rendered << compact_line(lines[index]);
        }

        if (lines.size() > visible_lines) {
            if (visible_lines > 0) {
                rendered << '\n';
            }

            rendered
                << "… +"
                << (lines.size() - visible_lines)
                << " lines (click to expand)";
        } else if (line_was_truncated) {
            if (visible_lines > 0) {
                rendered << '\n';
            }

            rendered
                << "… (click to expand full details)";
        }

        return rendered.str();
    }

    static std::string command_output_for_display(
        const std::string& output
    ) {
        std::ostringstream cleaned;
        std::size_t start = 0;
        bool wrote_line = false;

        while (start <= output.size()) {
            const std::size_t end =
                output.find('\n', start);
            const std::string line =
                output.substr(
                    start,
                    end == std::string::npos
                        ? std::string::npos
                        : end - start);
            const bool chromium_registration_noise =
                line.find(
                    "google_apis/gcm/engine/registration_request.cc") !=
                    std::string::npos &&
                line.find("DEPRECATED_ENDPOINT") !=
                    std::string::npos;
            const bool chromium_tensorflow_noise =
                line.find(
                    "Created TensorFlow Lite XNNPACK delegate for CPU.") !=
                    std::string::npos;

            if (
                !chromium_registration_noise &&
                !chromium_tensorflow_noise &&
                (
                    !line.empty() ||
                    end != std::string::npos
                )
            ) {
                if (wrote_line) {
                    cleaned << '\n';
                }

                cleaned << line;
                wrote_line = true;
            }

            if (end == std::string::npos) {
                break;
            }

            start = end + 1;
        }

        return cleaned.str();
    }

    static std::string activity_body(
        const nlohmann::json& item,
        const std::string& turn_status,
        const std::string& streamed_output = {},
        bool expanded = false
    ) {
        const std::string type =
            json_string_field(item, "type");

        if (type == "commandExecution") {
            std::string rendered =
                "$ " +
                (
                    expanded
                        ? json_display_field(
                            item,
                            "command",
                            "(command pending)")
                        : compact_activity_text(
                            json_display_field(
                                item,
                                "command",
                                "(command pending)"),
                            3)
                );

            const std::string cwd =
                json_string_field(item, "cwd");

            if (!cwd.empty()) {
                rendered += "\n↳ " + cwd;
            }

            std::string output = streamed_output;

            if (
                output.empty() &&
                item.contains("aggregatedOutput") &&
                item["aggregatedOutput"].is_string()
            ) {
                output = item["aggregatedOutput"]
                    .get<std::string>();
            }

            output = command_output_for_display(output);

            if (!output.empty()) {
                rendered += '\n';
                rendered += expanded
                    ? output
                    : compact_activity_text(output);
            }

            std::string completion;

            if (
                item.contains("exitCode") &&
                item["exitCode"].is_number_integer()
            ) {
                completion =
                    "exit " +
                    std::to_string(
                        item["exitCode"].get<int>());
            }

            if (
                item.contains("durationMs") &&
                item["durationMs"].is_number_integer()
            ) {
                if (!completion.empty()) {
                    completion += " · ";
                }

                completion +=
                    std::to_string(
                        item["durationMs"].get<long long>()) +
                    " ms";
            }

            if (!completion.empty()) {
                rendered += "\n[" + completion + "]";
            }

            return rendered;
        }

        if (type == "fileChange") {
            std::string rendered;
            const auto changes = item.value(
                "changes",
                nlohmann::json::array());

            if (changes.is_array()) {
                for (const auto& change : changes) {
                    if (!change.is_object()) {
                        continue;
                    }

                    if (!rendered.empty()) {
                        rendered += '\n';
                    }

                    rendered +=
                        json_string_field(
                            change,
                            "kind",
                            "change") +
                        " " +
                        json_string_field(
                            change,
                            "path",
                            "(unknown path)");

                    const std::string diff =
                        json_string_field(
                            change,
                            "diff");

                    if (!diff.empty()) {
                        rendered += '\n';
                        rendered += expanded
                            ? diff
                            : compact_activity_text(
                                diff,
                                14,
                                240);
                    }
                }
            }

            return rendered.empty()
                ? "(file details pending)"
                : rendered;
        }

        std::string rendered =
            activity_detail(item);

        if (
            (
                type == "mcpToolCall" ||
                type == "dynamicToolCall"
            ) &&
            item.contains("arguments") &&
            !item["arguments"].is_null()
        ) {
            if (!rendered.empty()) {
                rendered += '\n';
            }

            rendered += "Arguments: ";
            rendered += item["arguments"].is_string()
                ? item["arguments"].get<std::string>()
                : item["arguments"].dump(2);
        }

        if (
            type == "mcpToolCall" &&
            item.contains("error") &&
            item["error"].is_object()
        ) {
            rendered +=
                "\nError: " +
                json_display_field(
                    item["error"],
                    "message",
                    "unknown tool error");
        }

        if (
            type == "mcpToolCall" &&
            item.contains("result") &&
            item["result"].is_object()
        ) {
            append_activity_result_text(
                rendered,
                item["result"].value(
                    "content",
                    nlohmann::json::array()));
        }

        if (
            type == "dynamicToolCall" &&
            item.contains("contentItems")
        ) {
            append_activity_result_text(
                rendered,
                item["contentItems"]);
        }

        if (type == "sleep") {
            long long duration = 0;

            if (
                item.contains("durationMs") &&
                item["durationMs"]
                    .is_number_integer()
            ) {
                duration = item["durationMs"]
                    .get<long long>();
            }

            if (duration > 0) {
                rendered =
                    std::to_string(duration) +
                    " ms";
            }
        }

        if (rendered.empty()) {
            rendered =
                "[" +
                activity_state(
                    item,
                    turn_status,
                    "completed") +
                "]";
        }

        return rendered;
    }

    std::string expandable_activity_text(
        const std::string& full_text,
        const std::string& activity_identity
    ) {
        std::string normalized_full_text =
            full_text;

        while (
            !normalized_full_text.empty() &&
            normalized_full_text.back() == '\n'
        ) {
            normalized_full_text.pop_back();
        }

        const std::string preview =
            compact_activity_text(
                normalized_full_text);

        if (
            preview == normalized_full_text ||
            preview.find("click to expand") ==
                std::string::npos
        ) {
            return preview;
        }

        std::string& token =
            activity_expansion_tokens_[
                activity_identity];

        if (token.empty()) {
            token =
                "[[THREADDECK_EXPAND_" +
                std::to_string(
                    ++activity_expansion_sequence_) +
                "]]";
        }

        activity_expansion_payloads_[token] = {
            activity_identity,
            preview,
            normalized_full_text,
        };

        if (
            expanded_activity_ids_.find(
                activity_identity) !=
            expanded_activity_ids_.end()
        ) {
            return
                normalized_full_text +
                (
                    !normalized_full_text.empty() &&
                    normalized_full_text.back() == '\n'
                        ? ""
                        : "\n"
                ) +
                "… (click to re-abbreviate)" +
                token;
        }

        return preview + token;
    }

    enum class LiveEntryKind {
        AgentMessage,
        Reasoning,
        Plan,
        FollowUp,
        Activity,
    };

    struct LiveTurnEntry {
        LiveEntryKind kind{LiveEntryKind::Activity};
        std::string item_id;
        std::string type;
        std::string label;
        std::string state;
        std::string phase;
        std::string text;
        std::string displayed_text;
        std::string output;
        nlohmann::json item;
        std::chrono::steady_clock::time_point
            reveal_after{};
        std::chrono::steady_clock::time_point
            last_delta_at{};
        bool stream_complete{false};
    };

    enum class SessionWorkKind {
        None,
        Turn,
        Compaction,
        SummarizeAndTitle,
        ShellCommand,
    };

    struct PendingFollowUp {
        std::string entry_id;
        nlohmann::json input =
            nlohmann::json::array();
    };

    struct PendingApprovalState {
        AppServerClient::ApprovalRequest request;
        std::string decision;
        bool resolved{false};
        std::condition_variable condition;
    };

    struct ThreadTurnSession {
        std::string thread_id;
        std::string cwd;
        bool busy{false};
        bool failed{false};
        SessionWorkKind work_kind{
            SessionWorkKind::None};
        std::string active_turn_id;
        bool stop_requested{false};
        bool interrupt_sent{false};
        std::string reasoning_summary;
        std::string agent_text;
        std::vector<LiveTurnEntry> live_entries;
        std::string rendered_tail;
        nlohmann::json base_thread =
            nlohmann::json::object();
        nlohmann::json transcript_input =
            nlohmann::json::array();
        std::string pending_display;
        std::string mode{"default"};
        std::deque<PendingFollowUp>
            pending_follow_ups;
        std::map<int, std::string>
            follow_up_request_entries;
        AppServerClient::SessionOptions options;
        AppServerClient::ProcessEnvironment
            process_environment;
        std::size_t desired_environment_generation{0};
        std::size_t client_environment_generation{0};
        Glib::RefPtr<Gtk::TextBuffer>
            transcript_buffer;
        Glib::RefPtr<Gtk::TextBuffer::Mark>
            live_turn_start_mark;
        bool live_turn_has_base_transcript{false};
        std::unique_ptr<AppServerClient> client;
        std::thread worker;
        bool loading{false};
        std::thread loader;
        std::size_t history_render_generation{0};
        bool history_rendering{false};
    };

    struct CompletedTurn {
        std::string thread_id;
        AppServerClient::TurnResult result;
    };

    struct CompletedShellCommand {
        std::string thread_id;
        AppServerClient::JsonResult result;
    };

    struct CompletedThreadActivation {
        std::string thread_id;
        std::string expected_cwd;
        bool clear_saved_state_on_failure{false};
        std::size_t environment_generation{0};
        AppServerClient::ThreadResumeResult result;
        std::unique_ptr<AppServerClient> client;
    };

    struct CompletedSkillLoad {
        std::string cwd;
        std::vector<nlohmann::json> skills;
        std::string error;
    };

    struct CompletedThreadMove {
        std::string thread_id;
        std::string source_project_id;
        std::string destination_project_id;
        std::string source_cwd;
        std::string destination_cwd;
        AppServerClient::JsonResult result;
    };

    Glib::RefPtr<Gtk::TextBuffer>
    create_thread_transcript_buffer() {
        return Gtk::TextBuffer::create(
            transcript_.get_buffer()->get_tag_table());
    }

    void attach_thread_transcript_buffer(
        const Glib::RefPtr<Gtk::TextBuffer>& buffer
    ) {
        if (!buffer) {
            return;
        }

        if (transcript_end_mark_) {
            const auto owner =
                transcript_end_mark_->get_buffer();

            if (owner) {
                owner->delete_mark(
                    transcript_end_mark_);
            }

            transcript_end_mark_.reset();
        }

        const auto current_buffer =
            transcript_.get_buffer();

        if (
            current_buffer &&
            current_buffer->gobj() != buffer->gobj()
        ) {
            dematerialize_code_copy_buttons();
        }

        transcript_scroll_pending_ = false;
        transcript_.set_buffer(buffer);
        materialize_code_copy_buttons(buffer);
    }

    void render_thread_transcript(
        const nlohmann::json& thread,
        const std::string& thread_id
    ) {
        std::string rendered;
        std::size_t activity_index = 0;
        std::vector<TranscriptImage>
            transcript_images;

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
                    json_string_field(
                        turn,
                        "status");

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

                    try {
                        const std::string type =
                            json_string_field(
                                item,
                                "type");

                    if (type == "userMessage") {
                        append_rendered_user_block(
                            rendered,
                            render_user_content(
                                item.value(
                                    "content",
                                    nlohmann::json::array()),
                                transcript_images));

                    } else if (type == "agentMessage") {
                        const std::string agent_text =
                            json_string_field(
                                item,
                                "text");

                        if (!agent_text.empty()) {
                            rendered_agent_message = true;
                        }

                        const std::string phase =
                            json_string_field(
                                item,
                                "phase");

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
                            json_string_field(
                                item,
                                "text"));

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
                        const std::string item_id =
                            json_string_field(
                                item,
                                "id");
                        const std::string activity_identity =
                            thread_id + ":" +
                            (
                                item_id.empty()
                                    ? "stored-" +
                                        std::to_string(
                                            activity_index)
                                    : item_id
                            );
                        ++activity_index;
                        const std::string full_activity =
                            activity_body(
                                item,
                                turn_status,
                                {},
                                true);

                        append_rendered_block(
                            rendered,
                            activity_heading(
                                item,
                                turn_status),
                            expandable_activity_text(
                                full_activity,
                                activity_identity));

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
                    } catch (
                        const nlohmann::json::exception&
                            error
                    ) {
                        append_rendered_block(
                            rendered,
                            "Activity",
                            "[Unsupported stored activity data]");

                        std::cerr
                            << "WARN: skipped unsupported stored activity: "
                            << error.what()
                            << '\n';
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
                            json_display_field(
                                turn["error"],
                                "message");
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

        ThreadTurnSession* session =
            find_turn_session(thread_id);
        const auto buffer =
            session != nullptr && session->transcript_buffer
                ? session->transcript_buffer
                : transcript_.get_buffer();

        const std::size_t generation =
            session != nullptr
                ? ++session->history_render_generation
                : 0;

        if (session != nullptr) {
            session->history_rendering = true;
        }

        buffer->set_text("");

        auto job =
            std::make_shared<IncrementalTranscriptRender>();
        job->thread_id = thread_id;
        job->text = std::move(rendered);
        job->images = std::move(transcript_images);
        job->buffer = buffer;
        job->generation = generation;

        Glib::signal_idle().connect(
            [this, job]() mutable {
                ThreadTurnSession* current_session =
                    find_turn_session(job->thread_id);

                if (
                    current_session != nullptr &&
                    current_session->history_render_generation !=
                        job->generation
                ) {
                    return false;
                }

                if (job->byte_offset >= job->text.size()) {
                    materialize_transcript_images(
                        job->buffer,
                        job->text,
                        job->images);

                    if (current_session != nullptr) {
                        current_session->history_rendering = false;
                    }

                    if (
                        job->thread_id == current_thread_id_ &&
                        transcript_follow_output_
                    ) {
                        scroll_transcript_to_end();
                    }

                    return false;
                }

                constexpr std::size_t slice_size = 16384;
                std::size_t end = std::min(
                    job->byte_offset + slice_size,
                    job->text.size());

                if (end < job->text.size()) {
                    const auto newline =
                        job->text.find('\n', end);

                    if (newline != std::string::npos) {
                        end = newline + 1;
                    } else {
                        end = job->text.size();
                    }
                }

                const std::string chunk =
                    job->text.substr(
                        job->byte_offset,
                        end - job->byte_offset);
                const int character_start =
                    job->buffer->get_char_count();
                auto insertion = job->buffer->end();
                job->buffer->insert(
                    insertion,
                    Glib::ustring(chunk));

                apply_transcript_tags_to_buffer(
                    job->buffer,
                    character_start,
                    job->inside_code_fence,
                    job->inside_user_section,
                    job->inside_diff_activity,
                    job->code_language);

                std::size_t line_start = 0;
                while (line_start < chunk.size()) {
                    const auto line_end =
                        chunk.find('\n', line_start);
                    const std::string line =
                        chunk.substr(
                            line_start,
                            line_end == std::string::npos
                                ? std::string::npos
                                : line_end - line_start);
                    const bool fence =
                        line.rfind("```", 0) == 0 ||
                        line.rfind("~~~", 0) == 0;

                    if (fence) {
                        if (job->inside_code_fence) {
                            job->inside_code_fence = false;
                            job->code_language.clear();
                        } else {
                            job->inside_code_fence = true;
                            job->code_language =
                                code_fence_language(line);
                        }
                    } else if (!job->inside_code_fence) {
                        const bool internal_user_marker =
                            line.rfind(
                                "[[THREADDECK_USER_INPUT]]",
                                0) == 0;
                        const bool user_heading =
                            internal_user_marker ||
                            line == "You:" ||
                            line.rfind("You · ", 0) == 0 ||
                            line == "Follow-up:";
                        const bool section_heading =
                            user_heading ||
                            line == "Codex:" ||
                            line == "Codex commentary:" ||
                            line == "Codex reasoning:" ||
                            line == "Codex plan:" ||
                            line == "Activity:" ||
                            line == "Hook:" ||
                            line == "Follow-up rejected:" ||
                            line.rfind("Codex error", 0) == 0 ||
                            line.rfind("Codex transport error", 0) == 0 ||
                            line.rfind("Codex compaction error", 0) == 0 ||
                            line.rfind("Shell command error", 0) == 0 ||
                            (
                                line.rfind("Codex ", 0) == 0 &&
                                !line.empty() &&
                                line.back() == ':'
                            );

                        if (section_heading) {
                            job->inside_user_section =
                                user_heading;
                            job->inside_diff_activity =
                                line ==
                                    "Codex changed files:" ||
                                line ==
                                    "Codex changing files:";
                        }
                    }

                    if (
                        job->inside_user_section &&
                        line.find(
                            "[[THREADDECK_USER_INPUT_END]]") !=
                            std::string::npos
                    ) {
                        job->inside_user_section = false;
                        job->inside_code_fence = false;
                        job->code_language.clear();
                    }

                    if (line_end == std::string::npos) {
                        break;
                    }

                    line_start = line_end + 1;
                }

                job->byte_offset = end;

                if (
                    job->thread_id == current_thread_id_ &&
                    transcript_follow_output_
                ) {
                    scroll_transcript_to_end();
                }

                return true;
            },
            Glib::PRIORITY_DEFAULT_IDLE);
    }

    void show_running_thread(
        ThreadTurnSession& session
    ) {
        const std::string cwd =
            session.cwd.empty()
                ? selected_folder_path_
                : session.cwd;

        add_project_folder(cwd);

        current_thread_id_ = session.thread_id;
        last_active_thread_id_ = session.thread_id;
        last_active_thread_cwd_ = cwd;
        current_thread_turn_failed_ = false;
        thread_token_usage_ =
            nlohmann::json::object();

        effective_mode_ =
            session.mode.empty()
                ? "default"
                : session.mode;

        apply_effective_thread_settings(
            session.options.model,
            session.options.reasoning_effort,
            session.options.approval_policy,
            session.options.sandbox_policy,
            false);

        const std::string label =
            session.base_thread.is_object()
                ? display_thread_label(
                    session.base_thread)
                : "Thread";

        set_active_thread_surfaces(
            label,
            cwd,
            session.thread_id,
            session.base_thread);

        if (!session.transcript_buffer) {
            session.transcript_buffer =
                create_thread_transcript_buffer();
        }

        attach_thread_transcript_buffer(
            session.transcript_buffer);

        if (
            session.transcript_buffer->get_char_count() == 0 &&
            !session.history_rendering
        ) {
            render_thread_transcript(
                session.base_thread,
                session.thread_id);
        }

        if (
            session.work_kind ==
            SessionWorkKind::ShellCommand
        ) {
            append_transcript(
                session.pending_display,
                true);
            set_turn_busy(false);
            set_shell_command_busy(true);
            status_label_.set_text(
                "Codex: running shell command");
        } else {
            render_live_turn(session);

            set_shell_command_busy(false);
            set_turn_busy(true);
            active_turn_id_ =
                session.active_turn_id;
            stop_requested_ =
                session.stop_requested;

            status_label_.set_text(
                "Codex: working");
        }

        update_send_button_state();
        save_ui_state();
        schedule_sidebar_refresh();
        approval_dispatcher_.emit();
    }

    void show_cached_thread(
        ThreadTurnSession& session
    ) {
        const std::string cwd =
            session.cwd.empty()
                ? selected_folder_path_
                : session.cwd;

        add_project_folder(cwd);

        current_thread_id_ = session.thread_id;
        last_active_thread_id_ = session.thread_id;
        last_active_thread_cwd_ = cwd;
        current_thread_turn_failed_ = false;
        thread_token_usage_ =
            nlohmann::json::object();

        effective_mode_ =
            session.mode.empty()
                ? "default"
                : session.mode;

        apply_effective_thread_settings(
            session.options.model,
            session.options.reasoning_effort,
            session.options.approval_policy,
            session.options.sandbox_policy,
            false);

        set_active_thread_surfaces(
            display_thread_label(
                session.base_thread),
            cwd,
            session.thread_id,
            session.base_thread);

        if (session.transcript_buffer) {
            attach_thread_transcript_buffer(
                session.transcript_buffer);
            scroll_transcript_to_end(true);
        } else {
            session.transcript_buffer =
                create_thread_transcript_buffer();
            attach_thread_transcript_buffer(
                session.transcript_buffer);
            render_thread_transcript(
                session.base_thread,
                session.thread_id);
        }

        set_turn_busy(false);
        set_shell_command_busy(false);
        status_label_.set_text(
            "Codex: connected");
        prompt_.grab_focus();
        update_send_button_state();
        save_ui_state();
        schedule_sidebar_refresh();
    }

    nlohmann::json cached_thread_summary(
        const std::string& thread_id
    ) const {
        for (const auto& catalog : thread_catalog_) {
            for (const auto& thread : catalog.second) {
                if (
                    thread.is_object() &&
                    thread.value(
                        "id",
                        std::string{}) == thread_id
                ) {
                    return thread;
                }
            }
        }

        return nlohmann::json::object();
    }

    void show_loading_thread(
        ThreadTurnSession& session
    ) {
        const std::string cwd =
            session.cwd.empty()
                ? selected_folder_path_
                : session.cwd;

        add_project_folder(cwd);

        current_thread_id_ = session.thread_id;
        last_active_thread_id_ = session.thread_id;
        last_active_thread_cwd_ = cwd;
        current_thread_turn_failed_ = false;
        thread_token_usage_ =
            nlohmann::json::object();

        apply_effective_thread_settings(
            session.options.model,
            session.options.reasoning_effort,
            session.options.approval_policy,
            session.options.sandbox_policy,
            false);

        const std::string label =
            session.base_thread.is_object() &&
                !session.base_thread.empty()
                ? display_thread_label(
                    session.base_thread)
                : "Thread";

        set_active_thread_surfaces(
            label,
            cwd,
            session.thread_id,
            session.base_thread);

        if (!session.transcript_buffer) {
            session.transcript_buffer =
                create_thread_transcript_buffer();
            session.transcript_buffer->set_text(
                "Loading thread history in the background…\n\n"
                "You can type here while it loads.");
        }

        attach_thread_transcript_buffer(
            session.transcript_buffer);

        set_turn_busy(false);
        set_shell_command_busy(false);
        status_label_.set_text(
            "Codex: loading thread in background");
        prompt_.set_editable(true);
        prompt_.grab_focus();
        update_send_button_state();
        save_ui_state();
        schedule_sidebar_refresh();
    }

    bool activate_thread(
        const std::string& expected_cwd,
        const std::string& thread_id,
        bool clear_saved_state_on_failure = false,
        const std::string& expected_project_id = {}
    ) {
        if (thread_id.empty()) {
            return false;
        }

        completed_unseen_threads_.erase(thread_id);

        const std::string activation_project_id =
            expected_project_id.empty()
                ? project_id_for_thread(
                    thread_id,
                    expected_cwd)
                : expected_project_id;

        if (!activation_project_id.empty()) {
            selected_project_id_ =
                activation_project_id;
            selected_folder_path_ =
                project_cwd(activation_project_id);
            selected_folder_.set_text(
                selected_folder_path_);
        }

        if (
            auto* session =
                find_turn_session(thread_id);
            session != nullptr
        ) {
            if (session->busy) {
                show_running_thread(*session);
            } else if (session->loading) {
                show_loading_thread(*session);
            } else if (
                session->base_thread.is_object() &&
                !session->base_thread.empty()
            ) {
                show_cached_thread(*session);
            } else {
                session = nullptr;
            }

            if (session != nullptr) {
                prompt_.grab_focus();
                return true;
            }
        }

        auto& session_pointer =
            turn_sessions_[thread_id];

        if (!session_pointer) {
            session_pointer =
                std::make_unique<ThreadTurnSession>();
            session_pointer->thread_id = thread_id;
        }

        ThreadTurnSession& session =
            *session_pointer;

        if (session.loader.joinable()) {
            session.loader.join();
        }

        session.cwd = expected_cwd;
        session.base_thread =
            cached_thread_summary(thread_id);
        session.options = current_session_options();

        const auto target_model =
            thread_model_selections_.find(thread_id);
        if (
            target_model !=
                thread_model_selections_.end() &&
            !target_model->second.empty()
        ) {
            session.options.model =
                target_model->second;
        }

        const auto target_access =
            thread_access_selections_.find(thread_id);
        const bool target_uses_yolo =
            target_access !=
                thread_access_selections_.end() &&
            target_access->second == "yolo";

        if (target_uses_yolo) {
            session.options.approval_policy =
                "never";
            session.options.sandbox_mode =
                "danger-full-access";
            session.options.sandbox_policy = {
                {"type", "dangerFullAccess"},
            };
        } else {
            session.options.approval_policy =
                "on-request";
            session.options.sandbox_mode =
                "workspace-write";
            session.options.sandbox_policy = {
                {"type", "workspaceWrite"},
            };
        }

        session.options.cwd = expected_cwd;
        session.options.shield_enabled =
            shield_enabled_ &&
            thread_shield_selections_.find(thread_id) !=
                thread_shield_selections_.end();
        session.options.remote_shield_hosts =
            remote_shield_hosts_for_thread(thread_id);
        session.mode = effective_mode_;
        session.loading = true;
        prepare_session_process_environment(session);

        show_loading_thread(session);

        const AppServerClient::SessionOptions options =
            session.options;
        const AppServerClient::ProcessEnvironment environment =
            session.process_environment;
        const std::size_t environment_generation =
            session.desired_environment_generation;

        session.loader = std::thread(
            [
                this,
                thread_id,
                expected_cwd,
                clear_saved_state_on_failure,
                options,
                environment,
                environment_generation
            ]() mutable {
                CompletedThreadActivation completed;
                completed.thread_id = thread_id;
                completed.expected_cwd = expected_cwd;
                completed.clear_saved_state_on_failure =
                    clear_saved_state_on_failure;
                completed.environment_generation =
                    environment_generation;
                completed.client =
                    std::make_unique<AppServerClient>();

                std::string error;

                if (!completed.client->start(
                    error,
                    environment)
                ) {
                    completed.result.error =
                        "Could not start Codex App Server: " +
                        error;
                } else {
                    const auto initialized =
                        completed.client->initialize(
                            "threaddeck",
                            "ThreadDeck",
                            "0.1.0");

                    if (!initialized.success) {
                        completed.result.error =
                            initialized.error;
                    } else {
                        completed.result =
                            completed.client->resume_thread(
                                thread_id,
                                10000,
                                options);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(
                        thread_activation_result_mutex_);
                    pending_thread_activations_.push_back(
                        std::move(completed));
                }

                thread_activation_dispatcher_.emit();
            });

        return true;
    }

    void handle_thread_activation_finished() {
        std::deque<CompletedThreadActivation> completed;

        {
            std::lock_guard<std::mutex> lock(
                thread_activation_result_mutex_);
            completed.swap(
                pending_thread_activations_);
        }

        for (auto& activation : completed) {
            ThreadTurnSession* session =
                find_turn_session(
                    activation.thread_id);

            if (session == nullptr) {
                continue;
            }

            if (session->loader.joinable()) {
                session->loader.join();
            }

            session->loading = false;

            if (!activation.result.success) {
                std::cerr
                    << "FAIL: thread/resume "
                    << activation.thread_id
                    << ": "
                    << activation.result.error
                    << '\n';

                if (
                    activation.thread_id ==
                        current_thread_id_ &&
                    !session->busy
                ) {
                    status_label_.set_text(
                        "Codex: thread resume failed");

                    if (
                        activation
                            .clear_saved_state_on_failure
                    ) {
                        current_thread_id_.clear();
                        last_active_thread_id_.clear();
                        last_active_thread_cwd_.clear();
                        clear_active_thread_surfaces();
                    }

                    session->transcript_buffer->set_text(
                        "The Codex thread could not be resumed.\n\n" +
                        activation.result.error);
                    update_send_button_state();
                }

                save_ui_state();
                schedule_sidebar_refresh();
                continue;
            }

            const auto assigned_project =
                thread_project_assignments_.find(
                    activation.thread_id);
            const std::string resumed_cwd =
                assigned_project !=
                        thread_project_assignments_.end()
                    ? project_cwd(
                        assigned_project->second)
                    : (
                        activation.result.cwd.empty()
                            ? activation.expected_cwd
                            : activation.result.cwd
                    );

            session->cwd = resumed_cwd;
            session->base_thread =
                activation.result.thread;
            session->mode =
                activation.result.collaboration_mode.empty()
                    ? "default"
                    : activation.result.collaboration_mode;
            session->options.model =
                activation.result.model;
            session->options.reasoning_effort =
                activation.result.reasoning_effort;

            const auto saved_reasoning =
                thread_reasoning_selections_.find(
                    session->thread_id);

            if (
                saved_reasoning !=
                    thread_reasoning_selections_.end() &&
                !saved_reasoning->second.empty()
            ) {
                session->options.reasoning_effort =
                    saved_reasoning->second;
            }
            session->options.approval_policy =
                activation.result.approval_policy;
            session->options.sandbox_policy =
                activation.result.sandbox_policy;
            session->options.cwd = resumed_cwd;

            if (
                !session->busy &&
                (
                    !session->client ||
                    !session->client->is_running()
                )
            ) {
                session->client =
                    std::move(activation.client);
                session->client_environment_generation =
                    activation.environment_generation;
            }

            if (
                activation.thread_id ==
                    current_thread_id_ &&
                !session->busy
            ) {
                last_active_thread_cwd_ = resumed_cwd;
                effective_mode_ = session->mode;

                apply_effective_thread_settings(
                    session->options.model,
                    session->options.reasoning_effort,
                    session->options.approval_policy,
                    session->options.sandbox_policy,
                    true);

                set_active_thread_surfaces(
                    display_thread_label(
                        session->base_thread),
                    resumed_cwd,
                    session->thread_id,
                    session->base_thread);

                attach_thread_transcript_buffer(
                    session->transcript_buffer);
                render_thread_transcript(
                    session->base_thread,
                    session->thread_id);

                status_label_.set_text(
                    "Codex: connected");
                prompt_.grab_focus();
                update_send_button_state();
            }

            save_ui_state();
            schedule_sidebar_refresh();

            std::cout
                << "PASS: GTK resumed Codex thread "
                << activation.thread_id
                << " without blocking the GTK thread\n";
        }
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
            true,
            selected_project_id_);
    }

    void initialize_app_server() {
        std::string start_error;
        AppServerClient::ProcessEnvironment environment =
            current_codex_process_environment();
        environment.tablet_accessible = true;

        if (
            !app_server_.start(
                start_error,
                environment)
        ) {
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

        load_model_catalog_and_usage();

        new_thread_button_.set_sensitive(
            !selected_folder_path_.empty());

        refresh_sidebar_threads();
        restore_last_active_thread();
    }

    bool transcript_is_at_bottom() const {
        const auto adjustment =
            transcript_scroll_.get_vadjustment();

        if (!adjustment) {
            return true;
        }

        constexpr double bottom_tolerance = 2.0;

        return
            adjustment->get_value() +
                adjustment->get_page_size() >=
            adjustment->get_upper() -
                bottom_tolerance;
    }

    void refresh_transcript_bottom_button() {
        if (transcript_is_at_bottom()) {
            transcript_bottom_button_.hide();
        } else {
            transcript_bottom_button_.show();
        }
    }

    void handle_transcript_scroll_changed() {
        const auto adjustment =
            transcript_scroll_.get_vadjustment();

        if (!adjustment) {
            transcript_follow_output_ = true;
            transcript_bottom_button_.hide();
            return;
        }

        const double value =
            adjustment->get_value();
        const double upper =
            adjustment->get_upper();
        const bool at_bottom =
            transcript_is_at_bottom();

        if (transcript_scroll_programmatic_) {
            transcript_last_scroll_value_ = value;
            transcript_last_scroll_upper_ = upper;
            refresh_transcript_bottom_button();
            return;
        }

        constexpr double change_tolerance = 0.5;

        const bool content_extent_unchanged =
            std::abs(
                upper -
                    transcript_last_scroll_upper_) <=
            change_tolerance;

        const bool viewport_moved =
            std::abs(
                value -
                    transcript_last_scroll_value_) >
            change_tolerance;

        if (at_bottom) {
            transcript_follow_output_ = true;
        } else if (
            content_extent_unchanged &&
            viewport_moved
        ) {
            transcript_follow_output_ = false;
        }

        transcript_last_scroll_value_ = value;
        transcript_last_scroll_upper_ = upper;

        refresh_transcript_bottom_button();

        if (
            transcript_follow_output_ &&
            !at_bottom
        ) {
            scroll_transcript_to_end();
        }
    }

    bool apply_pending_transcript_scroll() {
        transcript_scroll_pending_ = false;

        if (!transcript_follow_output_) {
            refresh_transcript_bottom_button();
            return false;
        }

        const auto buffer = transcript_.get_buffer();
        auto end = buffer->end();

        if (!transcript_end_mark_) {
            transcript_end_mark_ =
                buffer->create_mark(
                    end,
                    false);
        } else {
            buffer->move_mark(
                transcript_end_mark_,
                end);
        }

        transcript_scroll_programmatic_ = true;

        transcript_.scroll_to(
            transcript_end_mark_,
            0.0,
            0.0,
            1.0);

        const auto adjustment =
            transcript_scroll_.get_vadjustment();

        if (adjustment) {
            adjustment->set_value(
                std::max(
                    adjustment->get_lower(),
                    adjustment->get_upper() -
                        adjustment->get_page_size()));

            transcript_last_scroll_value_ =
                adjustment->get_value();
            transcript_last_scroll_upper_ =
                adjustment->get_upper();
        }

        transcript_scroll_programmatic_ = false;
        refresh_transcript_bottom_button();
        return false;
    }

    void scroll_transcript_to_end(
        bool force_follow = false
    ) {
        if (force_follow) {
            transcript_follow_output_ = true;
        }

        if (
            !transcript_follow_output_ ||
            transcript_scroll_pending_
        ) {
            return;
        }

        transcript_scroll_pending_ = true;

        Glib::signal_idle().connect(
            sigc::mem_fun(
                *this,
                &MainWindow::apply_pending_transcript_scroll));
    }

    void append_transcript(
        const std::string& message,
        bool force_follow = false
    ) {
        const auto buffer = transcript_.get_buffer();
        const int style_start =
            buffer->get_char_count();

        if (buffer->get_char_count() > 0) {
            auto end = buffer->end();
            buffer->insert(end, "\n\n");
        }

        auto end = buffer->end();
        buffer->insert(
            end,
            Glib::ustring(message));

        apply_transcript_tags(style_start);
        scroll_transcript_to_end(force_follow);
    }

    void append_user_content_to_transcript(
        const nlohmann::json& content
    ) {
        std::vector<TranscriptImage> images;
        const std::string rendered =
            "[[THREADDECK_USER_INPUT]]" +
            render_user_content(
                content,
                images) +
            "[[THREADDECK_USER_INPUT_END]]";

        const auto buffer =
            transcript_.get_buffer();

        const int base_character_offset =
            buffer->get_char_count() +
            (
                buffer->get_char_count() > 0
                    ? 2
                    : 0
            );

        append_transcript(
            rendered,
            true);

        materialize_transcript_images(
            buffer,
            rendered,
            images,
            base_character_offset);
        apply_transcript_tags(
            base_character_offset);
    }

    ThreadTurnSession* find_turn_session(
        const std::string& thread_id
    ) {
        const auto found =
            turn_sessions_.find(thread_id);

        return found == turn_sessions_.end()
            ? nullptr
            : found->second.get();
    }

    const ThreadTurnSession* find_turn_session(
        const std::string& thread_id
    ) const {
        const auto found =
            turn_sessions_.find(thread_id);

        return found == turn_sessions_.end()
            ? nullptr
            : found->second.get();
    }

    bool thread_is_busy(
        const std::string& thread_id
    ) const {
        const auto* session =
            find_turn_session(thread_id);

        return
            moving_thread_id_ == thread_id ||
            (
                session != nullptr &&
                (session->busy || session->loading)
            );
    }

    bool thread_has_pending_approval(
        const std::string& thread_id
    ) {
        std::lock_guard<std::mutex> lock(
            approval_mutex_);

        return std::any_of(
            pending_approvals_.begin(),
            pending_approvals_.end(),
            [&thread_id](
                const std::shared_ptr<
                    PendingApprovalState>& pending
            ) {
                return
                    pending != nullptr &&
                    !pending->resolved &&
                    pending->request.thread_id ==
                        thread_id;
            });
    }

    bool update_approval_blink() {
        bool has_pending = false;

        {
            std::lock_guard<std::mutex> lock(
                approval_mutex_);

            has_pending = std::any_of(
                pending_approvals_.begin(),
                pending_approvals_.end(),
                [](const std::shared_ptr<
                    PendingApprovalState>& pending) {
                    return
                        pending != nullptr &&
                        !pending->resolved;
                });
        }

        approval_blink_on_ =
            has_pending && !approval_blink_on_;

        for (Gtk::Widget* row : approval_question_rows_) {
            if (row == nullptr) {
                continue;
            }

            auto context = row->get_style_context();

            if (approval_blink_on_) {
                context->add_class("question-blink");
            } else {
                context->remove_class("question-blink");
            }
        }

        return true;
    }

    void prepare_session_process_environment(
        ThreadTurnSession& session
    ) {
        session.process_environment =
            current_codex_process_environment();
        session.process_environment.shield_enabled =
            shield_enabled_ &&
            thread_shield_selections_.find(
                session.thread_id) !=
                thread_shield_selections_.end();
        session.options.remote_shield_hosts =
            remote_shield_hosts_for_thread(
                session.thread_id);
        const nlohmann::json remote_hosts =
            remote_shield_host_map_for_thread(
                session.thread_id);
        session.process_environment
            .remote_shield_hosts_json =
                remote_hosts.empty()
                    ? std::string{}
                    : remote_hosts.dump();
        session.desired_environment_generation =
            codex_environment_generation_;
    }

    bool ensure_session_client_ready(
        ThreadTurnSession& session,
        const AppServerClient::SessionOptions& options,
        std::string& error
    ) {
        if (
            session.client->is_running() &&
            session.client_environment_generation ==
                session.desired_environment_generation
        ) {
            return true;
        }

        if (session.client->is_running()) {
            session.client->shutdown();
            session.client_environment_generation = 0;
        }

        if (
            !session.client->start(
                error,
                session.process_environment)
        ) {
            return false;
        }

        const auto initialized =
            session.client->initialize(
                "threaddeck",
                "ThreadDeck",
                "0.1.0");

        if (!initialized.success) {
            error =
                "Could not initialize the thread's "
                "Codex App Server: " +
                initialized.error;
            session.client->shutdown();
            session.client_environment_generation = 0;
            return false;
        }

        const auto resumed =
            session.client->resume_thread(
                session.thread_id,
                10000,
                options);

        if (!resumed.success) {
            error =
                "Could not resume the thread on its "
                "Codex App Server: " +
                resumed.error;
            session.client->shutdown();
            session.client_environment_generation = 0;
            return false;
        }

        session.client_environment_generation =
            session.desired_environment_generation;

        return true;
    }

    LiveTurnEntry& ensure_live_entry(
        ThreadTurnSession& session,
        LiveEntryKind kind,
        const std::string& item_id,
        const std::string& type = {}
    ) {
        const auto found = std::find_if(
            session.live_entries.begin(),
            session.live_entries.end(),
            [kind, &item_id](
                const LiveTurnEntry& entry
            ) {
                return
                    entry.kind == kind &&
                    !item_id.empty() &&
                    entry.item_id == item_id;
            });

        if (found != session.live_entries.end()) {
            return *found;
        }

        if (item_id.empty()) {
            const auto last = std::find_if(
                session.live_entries.rbegin(),
                session.live_entries.rend(),
                [kind](
                    const LiveTurnEntry& entry
                ) {
                    return entry.kind == kind;
                });

            if (last != session.live_entries.rend()) {
                return *last;
            }
        }

        LiveTurnEntry entry;
        entry.kind = kind;
        entry.item_id = item_id;
        entry.type = type;
        entry.state =
            kind == LiveEntryKind::Activity
                ? "running"
                : std::string{};

        session.live_entries.push_back(
            std::move(entry));

        return session.live_entries.back();
    }

    static nlohmann::json live_activity_item(
        const LiveTurnEntry& entry
    ) {
        nlohmann::json item = entry.item;

        if (!item.is_object()) {
            item = nlohmann::json::object();
        }

        if (!entry.type.empty()) {
            item["type"] = entry.type;
        }

        if (!entry.item_id.empty()) {
            item["id"] = entry.item_id;
        }

        if (!entry.state.empty()) {
            item["status"] =
                entry.state == "running"
                    ? "inProgress"
                    : entry.state;
        }

        return item;
    }

    static bool live_stream_text_entry(
        const LiveTurnEntry& entry
    ) {
        return
            entry.kind == LiveEntryKind::AgentMessage ||
            entry.kind == LiveEntryKind::Reasoning ||
            entry.kind == LiveEntryKind::Plan;
    }

    static bool stream_space(char character) {
        return
            character == ' ' ||
            character == '\t' ||
            character == '\r' ||
            character == '\n';
    }

    static std::string outer_text_fence_delimiter(
        const std::string& text
    ) {
        std::size_t opening = 0;

        while (
            opening < text.size() &&
            stream_space(text[opening])
        ) {
            ++opening;
        }

        if (text.compare(opening, 7, "```text") == 0) {
            return "```";
        }

        if (text.compare(opening, 7, "~~~text") == 0) {
            return "~~~";
        }

        return {};
    }

    static bool stream_inside_code_fence(
        const std::string& text,
        std::size_t position
    ) {
        const std::string outer_delimiter =
            outer_text_fence_delimiter(text);
        bool skipped_outer_opening = false;
        bool inside = false;
        std::size_t line_start = 0;

        while (line_start < position) {
            const std::size_t line_end =
                text.find('\n', line_start);
            const std::size_t content_end =
                line_end == std::string::npos
                    ? text.size()
                    : line_end;
            const std::string line = text.substr(
                line_start,
                content_end - line_start);
            const bool fence =
                line.rfind("```", 0) == 0 ||
                line.rfind("~~~", 0) == 0;

            if (fence) {
                if (
                    !skipped_outer_opening &&
                    !outer_delimiter.empty() &&
                    (
                        line.rfind("```text", 0) == 0 ||
                        line.rfind("~~~text", 0) == 0
                    )
                ) {
                    skipped_outer_opening = true;
                } else {
                    inside = !inside;
                }
            }

            if (
                line_end == std::string::npos ||
                line_end >= position
            ) {
                break;
            }

            line_start = line_end + 1;
        }

        return inside;
    }

    static std::size_t complete_stream_line_end(
        const std::string& text,
        std::size_t position,
        std::size_t limit
    ) {
        const std::size_t newline =
            text.find('\n', position);

        if (newline != std::string::npos && newline < limit) {
            return newline + 1;
        }

        return limit == text.size()
            ? limit
            : position;
    }

    static std::size_t matching_code_fence_end(
        const std::string& text,
        std::size_t opening,
        std::size_t limit
    ) {
        const std::string delimiter =
            text.substr(opening, 3);
        const std::size_t opening_line_end =
            complete_stream_line_end(
                text,
                opening,
                limit);

        if (opening_line_end == opening) {
            return opening;
        }

        std::size_t line_start = opening_line_end;

        while (line_start < limit) {
            const std::size_t line_end =
                complete_stream_line_end(
                    text,
                    line_start,
                    limit);

            if (line_end == line_start) {
                return opening;
            }

            if (text.compare(
                    line_start,
                    delimiter.size(),
                    delimiter) == 0
            ) {
                return line_end;
            }

            line_start = line_end;
        }

        return opening;
    }

    static std::size_t next_stream_reveal_boundary(
        const std::string& text,
        std::size_t position,
        std::size_t limit,
        bool stream_complete
    ) {
        if (position >= limit) {
            return position;
        }

        const bool line_start =
            position == 0 ||
            text[position - 1] == '\n';
        const std::string outer_delimiter =
            outer_text_fence_delimiter(text);

        if (
            line_start &&
            (
                text.compare(position, 7, "```text") == 0 ||
                text.compare(position, 7, "~~~text") == 0
            )
        ) {
            return complete_stream_line_end(
                text,
                position,
                limit);
        }

        if (
            line_start &&
            !outer_delimiter.empty() &&
            text.compare(
                position,
                outer_delimiter.size(),
                outer_delimiter) == 0 &&
            position + outer_delimiter.size() <= limit
        ) {
            std::size_t after =
                position + outer_delimiter.size();

            while (
                after < limit &&
                (
                    text[after] == ' ' ||
                    text[after] == '\t' ||
                    text[after] == '\r'
                )
            ) {
                ++after;
            }

            if (
                after == limit ||
                (after < limit && text[after] == '\n')
            ) {
                return complete_stream_line_end(
                    text,
                    position,
                    limit);
            }
        }

        const bool fence_line =
            line_start &&
            (
                text.compare(position, 3, "```") == 0 ||
                text.compare(position, 3, "~~~") == 0
            );

        if (fence_line) {
            const std::size_t fence_end =
                matching_code_fence_end(
                    text,
                    position,
                    limit);

            if (fence_end != position) {
                return fence_end;
            }

            return stream_complete
                ? complete_stream_line_end(
                    text,
                    position,
                    limit)
                : position;
        }

        if (stream_inside_code_fence(text, position)) {
            return complete_stream_line_end(
                text,
                position,
                limit);
        }

        const auto complete_delimited =
            [&text, position, limit](
                const std::string& opening,
                const std::string& closing
            ) {
                if (
                    text.compare(
                        position,
                        opening.size(),
                        opening) != 0
                ) {
                    return position;
                }

                const std::size_t close = text.find(
                    closing,
                    position + opening.size());

                return
                    close != std::string::npos &&
                    close + closing.size() <= limit
                        ? close + closing.size()
                        : position;
            };

        for (const auto& delimiters : {
                 std::pair<std::string, std::string>{"**", "**"},
                 std::pair<std::string, std::string>{"__", "__"},
                 std::pair<std::string, std::string>{"~~", "~~"},
                 std::pair<std::string, std::string>{"`", "`"},
             }) {
            const std::size_t complete =
                complete_delimited(
                    delimiters.first,
                    delimiters.second);

            if (complete != position) {
                return complete;
            }

            if (
                text.compare(
                    position,
                    delimiters.first.size(),
                    delimiters.first) == 0
            ) {
                return stream_complete ? limit : position;
            }
        }

        if (text[position] == '[') {
            const std::size_t label_end =
                text.find("](", position + 1);
            const std::size_t url_end =
                label_end == std::string::npos
                    ? std::string::npos
                    : text.find(')', label_end + 2);

            if (
                url_end != std::string::npos &&
                url_end + 1 <= limit
            ) {
                return url_end + 1;
            }

            return stream_complete ? limit : position;
        }

        const bool bare_url =
            text.compare(position, 7, "http://") == 0 ||
            text.compare(position, 8, "https://") == 0;

        if (bare_url) {
            std::size_t end = position;

            while (
                end < limit &&
                !stream_space(text[end])
            ) {
                ++end;
            }

            if (end < limit || limit == text.size()) {
                return end;
            }

            return position;
        }

        if (stream_space(text[position])) {
            std::size_t end = position + 1;

            while (
                end < limit &&
                stream_space(text[end])
            ) {
                ++end;
            }

            return end;
        }

        std::size_t end = position;

        while (
            end < limit &&
            !stream_space(text[end])
        ) {
            ++end;
        }

        if (end < limit || limit == text.size()) {
            while (
                end < limit &&
                stream_space(text[end])
            ) {
                ++end;
            }

            return end;
        }

        return position;
    }

    void schedule_live_text_reveal() {
        if (live_text_reveal_connection_.connected()) {
            return;
        }

        live_text_reveal_connection_ =
            Glib::signal_timeout().connect(
                sigc::mem_fun(
                    *this,
                    &MainWindow::reveal_live_text_tick),
                32);
    }

    void append_live_text_delta(
        LiveTurnEntry& entry,
        const std::string& delta
    ) {
        if (delta.empty()) {
            return;
        }

        const bool caught_up =
            entry.displayed_text.size() ==
                entry.text.size();
        const auto now =
            std::chrono::steady_clock::now();

        entry.text += delta;
        entry.stream_complete = false;
        entry.last_delta_at = now;

        if (caught_up) {
            entry.reveal_after =
                now + std::chrono::milliseconds(110);
        }

        schedule_live_text_reveal();
    }

    void complete_live_text_entry(
        LiveTurnEntry& entry
    ) {
        entry.stream_complete = true;
        entry.last_delta_at =
            std::chrono::steady_clock::now();
        schedule_live_text_reveal();
    }

    bool reveal_live_text_tick() {
        const auto now =
            std::chrono::steady_clock::now();
        constexpr auto quiet_delay =
            std::chrono::milliseconds(150);
        std::set<std::string> changed_threads;
        bool pending = false;

        for (auto& session_entry : turn_sessions_) {
            ThreadTurnSession& session =
                *session_entry.second;

            for (LiveTurnEntry& entry : session.live_entries) {
                if (!live_stream_text_entry(entry)) {
                    continue;
                }

                if (
                    entry.displayed_text.size() >
                        entry.text.size() ||
                    entry.text.compare(
                        0,
                        entry.displayed_text.size(),
                        entry.displayed_text) != 0
                ) {
                    entry.displayed_text.clear();
                }

                if (
                    entry.displayed_text.size() >=
                    entry.text.size()
                ) {
                    continue;
                }

                pending = true;

                if (now < entry.reveal_after) {
                    continue;
                }

                std::size_t reveal_limit =
                    entry.text.size();

                if (
                    !entry.stream_complete &&
                    now - entry.last_delta_at < quiet_delay
                ) {
                    const std::size_t newline =
                        entry.text.rfind('\n');

                    if (
                        newline == std::string::npos ||
                        newline + 1 <=
                            entry.displayed_text.size()
                    ) {
                        continue;
                    }

                    reveal_limit = newline + 1;
                }

                std::size_t reveal_end =
                    entry.displayed_text.size();
                const std::size_t reveal_start = reveal_end;
                constexpr std::size_t reveal_budget = 72;

                while (
                    reveal_end < reveal_limit &&
                    reveal_end - reveal_start < reveal_budget
                ) {
                    const std::size_t next =
                        next_stream_reveal_boundary(
                            entry.text,
                            reveal_end,
                            reveal_limit,
                            entry.stream_complete);

                    if (next <= reveal_end) {
                        break;
                    }

                    reveal_end = next;
                }

                if (reveal_end > reveal_start) {
                    entry.displayed_text.assign(
                        entry.text,
                        0,
                        reveal_end);
                    changed_threads.insert(
                        session.thread_id);
                }
            }
        }

        for (const std::string& thread_id : changed_threads) {
            if (auto* session = find_turn_session(thread_id)) {
                render_live_turn(*session);
            }
        }

        return pending;
    }

    struct TranscriptStyleState {
        bool inside_code_fence{false};
        bool inside_user_section{false};
        bool inside_diff_activity{false};
        std::string code_language;
    };

    static TranscriptStyleState transcript_style_state(
        const std::string& text
    ) {
        TranscriptStyleState state;
        std::size_t line_start = 0;

        while (line_start < text.size()) {
            const std::size_t line_end =
                text.find('\n', line_start);
            const std::string line =
                text.substr(
                    line_start,
                    line_end == std::string::npos
                        ? std::string::npos
                        : line_end - line_start);

            const bool fence =
                line.rfind("```", 0) == 0 ||
                line.rfind("~~~", 0) == 0;

            if (fence) {
                if (state.inside_code_fence) {
                    state.inside_code_fence = false;
                    state.code_language.clear();
                } else {
                    state.inside_code_fence = true;
                    state.code_language =
                        code_fence_language(line);
                }
            } else if (!state.inside_code_fence) {
                const bool internal_user_marker =
                    line.rfind(
                        "[[THREADDECK_USER_INPUT]]",
                        0) == 0;
                const bool user_heading =
                    internal_user_marker ||
                    line == "You:" ||
                    line.rfind("You · ", 0) == 0 ||
                    line == "Follow-up:";
                const bool section_heading =
                    user_heading ||
                    line == "Codex:" ||
                    line == "Codex commentary:" ||
                    line == "Codex reasoning:" ||
                    line == "Codex plan:" ||
                    line == "Activity:" ||
                    line == "Hook:" ||
                    line == "Follow-up rejected:" ||
                    line.rfind("Codex error", 0) == 0 ||
                    line.rfind("Codex transport error", 0) == 0 ||
                    line.rfind("Codex compaction error", 0) == 0 ||
                    line.rfind("Shell command error", 0) == 0 ||
                    (
                        line.rfind("Codex ", 0) == 0 &&
                        !line.empty() &&
                        line.back() == ':'
                    );

                if (section_heading) {
                    state.inside_user_section =
                        user_heading;
                    state.inside_diff_activity =
                        line == "Codex changed files:" ||
                        line == "Codex changing files:";
                }
            }

            if (
                state.inside_user_section &&
                line.find(
                    "[[THREADDECK_USER_INPUT_END]]") !=
                    std::string::npos
            ) {
                state.inside_user_section = false;
                state.inside_code_fence = false;
                state.code_language.clear();
            }

            if (line_end == std::string::npos) {
                break;
            }

            line_start = line_end + 1;
        }

        return state;
    }

    void set_transcript_pointer_cursor(bool clickable) {
        const auto window =
            transcript_.get_window(
                Gtk::TEXT_WINDOW_TEXT);

        if (!window) {
            return;
        }

        if (!transcript_arrow_cursor_) {
            transcript_arrow_cursor_ =
                Gdk::Cursor::create(
                    Gdk::LEFT_PTR);
        }

        if (!transcript_hand_cursor_) {
            transcript_hand_cursor_ =
                Gdk::Cursor::create(
                    Gdk::HAND2);
        }

        window->set_cursor(
            clickable
                ? transcript_hand_cursor_
                : transcript_arrow_cursor_);
    }

    void handle_transcript_realized() {
        set_transcript_pointer_cursor(false);
    }

    bool handle_transcript_pointer_motion(
        GdkEventMotion* event
    ) {
        if (event == nullptr) {
            return false;
        }

        int buffer_x = 0;
        int buffer_y = 0;
        transcript_.window_to_buffer_coords(
            Gtk::TEXT_WINDOW_TEXT,
            static_cast<int>(event->x),
            static_cast<int>(event->y),
            buffer_x,
            buffer_y);

        Gtk::TextBuffer::iterator hovered;
        transcript_.get_iter_at_location(
            hovered,
            buffer_x,
            buffer_y);

        const bool clickable =
            (
                transcript_expand_activity_tag_ &&
                hovered.has_tag(
                    transcript_expand_activity_tag_)
            ) ||
            (
                transcript_markdown_link_tag_ &&
                hovered.has_tag(
                    transcript_markdown_link_tag_)
            ) ||
            (
                transcript_code_copy_tag_ &&
                hovered.has_tag(
                    transcript_code_copy_tag_)
            );

        set_transcript_pointer_cursor(clickable);
        return false;
    }

    bool handle_transcript_pointer_leave(
        GdkEventCrossing*
    ) {
        set_transcript_pointer_cursor(false);
        return false;
    }

    bool handle_markdown_link_event(
        const Glib::RefPtr<Glib::Object>&,
        GdkEvent* event,
        const Gtk::TextIter& clicked
    ) {
        if (
            event == nullptr ||
            event->type != GDK_BUTTON_RELEASE ||
            event->button.button != 1
        ) {
            return false;
        }

        const auto buffer = transcript_.get_buffer();

        if (!buffer) {
            return false;
        }

        auto line_start = clicked;
        line_start.set_line_offset(0);
        auto line_end = line_start;
        line_end.forward_to_line_end();
        const std::string raw =
            buffer->get_slice(
                line_start,
                line_end,
                true).raw();
        const int clicked_column =
            clicked.get_line_offset();
        const auto character_offset =
            [&raw](std::size_t byte_offset) {
                return static_cast<int>(
                    Glib::ustring(
                        raw.substr(
                            0,
                            byte_offset))
                        .size());
            };
        std::string url;
        static const std::regex markdown_link(
            R"(\[([^\]]+)\]\((https?://[A-Za-z0-9][^\s\)]*)\))");

        for (
            std::sregex_iterator link(
                raw.begin(),
                raw.end(),
                markdown_link),
                end;
            link != end;
            ++link
        ) {
            const std::size_t label_byte_start =
                static_cast<std::size_t>(
                    (*link).position(1));
            const std::size_t label_byte_end =
                label_byte_start +
                static_cast<std::size_t>(
                    (*link).length(1));

            if (
                clicked_column >=
                    character_offset(label_byte_start) &&
                clicked_column <=
                    character_offset(label_byte_end)
            ) {
                url = (*link).str(2);
                break;
            }
        }

        if (url.empty()) {
            static const std::regex bare_link(
                R"URL((https?://[A-Za-z0-9][^\s<>()\[\]{}'"`]+))URL");

            for (
                std::sregex_iterator link(
                    raw.begin(),
                    raw.end(),
                    bare_link),
                    end;
                link != end;
                ++link
            ) {
                const std::size_t byte_start =
                    static_cast<std::size_t>(
                        link->position());
                std::size_t byte_end =
                    byte_start +
                    static_cast<std::size_t>(
                        link->length());

                while (
                    byte_end > byte_start &&
                    (
                        raw[byte_end - 1] == '.' ||
                        raw[byte_end - 1] == ',' ||
                        raw[byte_end - 1] == ';' ||
                        raw[byte_end - 1] == ':' ||
                        raw[byte_end - 1] == '!' ||
                        raw[byte_end - 1] == '?'
                    )
                ) {
                    --byte_end;
                }

                if (
                    clicked_column >=
                        character_offset(byte_start) &&
                    clicked_column <=
                        character_offset(byte_end)
                ) {
                    url = raw.substr(
                        byte_start,
                        byte_end - byte_start);
                    break;
                }
            }
        }

        if (url.empty()) {
            return false;
        }

        try {
            Gio::AppInfo::launch_default_for_uri(url);
            status_label_.set_text(
                "Codex: opened link in default browser");
        } catch (const Glib::Error& error) {
            status_label_.set_text(
                "Codex: could not open web link");
            std::cerr
                << "WARN: could not open transcript link: "
                << error.what()
                << '\n';
        }

        return true;
    }

    bool copy_text_to_clipboard(
        const std::string& text
    ) {
        if (text.empty()) {
            return false;
        }

        const auto clipboard =
            Gtk::Clipboard::get();

        if (!clipboard) {
            return false;
        }

        clipboard->set_text(text);
        clipboard->store();
        return true;
    }

    void prune_code_copy_buttons() {
        auto button = transcript_copy_buttons_.begin();

        while (button != transcript_copy_buttons_.end()) {
            if (
                button->anchor &&
                !button->anchor->get_deleted()
            ) {
                ++button;
                continue;
            }

            if (
                button->button &&
                button->button->get_parent() ==
                    &transcript_
            ) {
                transcript_.remove(
                    *button->button);
            }

            button = transcript_copy_buttons_.erase(
                button);
        }
    }

    void materialize_code_copy_buttons(
        const Glib::RefPtr<Gtk::TextBuffer>& buffer
    ) {
        if (!buffer) {
            return;
        }

        const auto visible_buffer =
            transcript_.get_buffer();

        if (
            !visible_buffer ||
            visible_buffer->gobj() != buffer->gobj()
        ) {
            return;
        }

        prune_code_copy_buttons();

        const Glib::ustring contents =
            buffer->get_slice(
                buffer->begin(),
                buffer->end(),
                true);
        const Glib::ustring marker_prefix =
            code_copy_button_marker_prefix();
        const Glib::ustring marker_suffix =
            code_copy_button_marker_suffix();
        std::vector<std::pair<int, std::string>> markers;
        Glib::ustring::size_type search_offset = 0;

        while (search_offset < contents.size()) {
            const auto marker_start =
                contents.find(marker_prefix, search_offset);

            if (marker_start == Glib::ustring::npos) {
                break;
            }

            const auto marker_suffix_start =
                contents.find(
                    marker_suffix,
                    marker_start + marker_prefix.size());

            if (marker_suffix_start == Glib::ustring::npos) {
                break;
            }

            const auto marker_end =
                marker_suffix_start + marker_suffix.size();
            markers.emplace_back(
                static_cast<int>(marker_start),
                contents.substr(
                    marker_start,
                    marker_end - marker_start).raw());
            search_offset = marker_end;
        }

        for (
            auto marker = markers.rbegin();
            marker != markers.rend();
            ++marker
        ) {
            const int offset = marker->first;
            const auto payload =
                code_copy_payloads_.find(marker->second);

            if (payload == code_copy_payloads_.end()) {
                continue;
            }

            auto marker_start =
                buffer->get_iter_at_offset(offset);
            auto marker_end = marker_start;
            marker_end.forward_chars(
                static_cast<int>(
                    Glib::ustring(marker->second).size()));
            buffer->erase(marker_start, marker_end);

            auto insertion =
                buffer->get_iter_at_offset(offset);
            TranscriptCopyButton embedded;
            embedded.buffer = buffer;
            embedded.marker = marker->second;
            embedded.anchor =
                buffer->create_child_anchor(insertion);
            embedded.image =
                std::make_unique<Gtk::Image>();
            embedded.button =
                std::make_unique<Gtk::Button>();

            embedded.image->set_from_icon_name(
                "edit-copy-symbolic",
                Gtk::ICON_SIZE_BUTTON);
            embedded.button->set_image(
                *embedded.image);
            embedded.button->set_always_show_image(true);
            embedded.button->set_relief(
                Gtk::RELIEF_NONE);
            embedded.button->set_focus_on_click(false);
            embedded.button->set_size_request(36, 36);
            embedded.button->set_tooltip_text(
                "Copy code block");
            embedded.button->get_style_context()
                ->add_class("compact-header-button");
            embedded.button->get_style_context()
                ->add_class("code-copy-button");

            Gtk::Button* const copy_button =
                embedded.button.get();
            copy_button->add_events(
                Gdk::ENTER_NOTIFY_MASK |
                Gdk::LEAVE_NOTIFY_MASK);
            copy_button->signal_enter_notify_event()
                .connect(
                    [this](GdkEventCrossing*) {
                        set_transcript_pointer_cursor(true);
                        return false;
                    });
            copy_button->signal_leave_notify_event()
                .connect(
                    [this](GdkEventCrossing*) {
                        set_transcript_pointer_cursor(false);
                        return false;
                    });

            embedded.button->signal_clicked().connect(
                [
                    this,
                    copied_text = payload->second.text,
                    language = payload->second.language
                ]() {
                    if (!copy_text_to_clipboard(copied_text)) {
                        status_label_.set_text(
                            "Codex: could not copy code block");
                        return;
                    }

                    status_label_.set_text(
                        "Codex: " + language +
                        " block copied");
                });

            transcript_.add_child_at_anchor(
                *embedded.button,
                embedded.anchor);
            embedded.button->show_all();
            transcript_copy_buttons_.push_back(
                std::move(embedded));
        }
    }

    void dematerialize_code_copy_buttons() {
        for (auto& embedded : transcript_copy_buttons_) {
            if (
                !embedded.buffer ||
                !embedded.anchor ||
                embedded.anchor->get_deleted()
            ) {
                continue;
            }

            const int offset =
                embedded.buffer
                    ->get_iter_at_child_anchor(
                        embedded.anchor)
                    .get_offset();

            if (
                embedded.button &&
                embedded.button->get_parent() ==
                    &transcript_
            ) {
                transcript_.remove(
                    *embedded.button);
            }

            auto anchor_start =
                embedded.buffer->get_iter_at_offset(
                    offset);
            auto anchor_end = anchor_start;
            anchor_end.forward_char();
            embedded.buffer->erase(
                anchor_start,
                anchor_end);
            auto insertion =
                embedded.buffer->get_iter_at_offset(
                    offset);
            embedded.buffer->insert(
                insertion,
                embedded.marker);
        }

        transcript_copy_buttons_.clear();
    }

    bool copy_code_block_at(
        const Glib::RefPtr<Gtk::TextBuffer>& buffer,
        const Gtk::TextIter& clicked
    ) {
        if (!buffer) {
            return false;
        }

        auto opening_start = clicked;
        opening_start.set_line_offset(0);
        auto opening_end = opening_start;
        opening_end.forward_to_line_end();

        const std::string opening =
            buffer->get_text(
                opening_start,
                opening_end,
                true).raw();

        if (
            opening.rfind("```", 0) != 0 &&
            opening.rfind("~~~", 0) != 0
        ) {
            return false;
        }

        const std::string delimiter =
            opening.substr(0, 3);
        const std::string language =
            code_fence_language(opening);
        std::string copied_text;
        auto scan = opening_start;

        if (!scan.forward_line()) {
            return false;
        }

        while (scan != buffer->end()) {
            auto line_end = scan;
            line_end.forward_to_line_end();
            const std::string line =
                buffer->get_text(
                    scan,
                    line_end,
                    true).raw();

            if (line.rfind(delimiter, 0) == 0) {
                break;
            }

            if (!copied_text.empty()) {
                copied_text += '\n';
            }

            copied_text += line;

            if (!scan.forward_line()) {
                break;
            }
        }

        if (!copy_text_to_clipboard(copied_text)) {
            status_label_.set_text(
                "Codex: could not copy code block");
            return true;
        }

        status_label_.set_text(
            language.empty()
                ? "Codex: code block copied"
                : "Codex: " + language +
                    " block copied");
        return true;
    }

    bool handle_code_copy_event(
        const Glib::RefPtr<Glib::Object>&,
        GdkEvent* event,
        const Gtk::TextIter& clicked
    ) {
        if (
            event == nullptr ||
            event->type != GDK_BUTTON_RELEASE ||
            event->button.button != 1
        ) {
            return false;
        }

        return copy_code_block_at(
            transcript_.get_buffer(),
            clicked);
    }

    bool handle_activity_expand_event(
        const Glib::RefPtr<Glib::Object>&,
        GdkEvent* event,
        const Gtk::TextIter& clicked
    ) {
        if (
            event == nullptr ||
            event->type != GDK_BUTTON_RELEASE ||
            event->button.button != 1
        ) {
            return false;
        }

        const auto buffer = transcript_.get_buffer();

        if (!buffer) {
            return false;
        }

        auto marker_start = clicked;
        marker_start.set_line_offset(0);
        auto marker_end = marker_start;
        marker_end.forward_to_line_end();

        const std::string marker_line =
            buffer->get_text(
                marker_start,
                marker_end,
                true).raw();
        constexpr const char* token_prefix =
            "[[THREADDECK_EXPAND_";
        const std::size_t token_start =
            marker_line.find(token_prefix);

        if (token_start == std::string::npos) {
            return false;
        }

        const std::size_t token_end =
            marker_line.find("]]", token_start);

        if (token_end == std::string::npos) {
            return false;
        }

        const std::string token =
            marker_line.substr(
                token_start,
                token_end + 2 - token_start);

        Glib::signal_idle().connect(
            [this, buffer, token]() {
                toggle_activity_expansion(
                    buffer,
                    token);
                return false;
            });

        return true;
    }

    void toggle_activity_expansion(
        const Glib::RefPtr<Gtk::TextBuffer>& buffer,
        const std::string& token
    ) {
        if (!buffer) {
            return;
        }

        const auto payload =
            activity_expansion_payloads_.find(token);

        if (payload == activity_expansion_payloads_.end()) {
            return;
        }

        const Glib::ustring buffer_text =
            buffer->get_text();
        const auto token_position =
            buffer_text.find(
                Glib::ustring(token));

        if (token_position == Glib::ustring::npos) {
            return;
        }

        auto marker_start =
            buffer->get_iter_at_offset(
                static_cast<int>(token_position));
        marker_start.set_line_offset(0);
        auto marker_end = marker_start;
        marker_end.forward_to_line_end();

        auto body_start = marker_start;
        auto scan = marker_start;
        bool found_heading = false;

        while (scan.get_line() > 0) {
            if (!scan.backward_line()) {
                break;
            }

            if (!scan.has_tag(transcript_activity_tag_)) {
                continue;
            }

            auto heading_end = scan;
            heading_end.forward_to_line_end();

            if (heading_end != buffer->end()) {
                heading_end.forward_char();
            }

            body_start = heading_end;
            found_heading = true;
            break;
        }

        if (!found_heading) {
            return;
        }

        const int insertion_offset =
            body_start.get_offset();
        const bool visible_buffer =
            transcript_.get_buffer() == buffer;
        const auto adjustment =
            visible_buffer
                ? transcript_scroll_.get_vadjustment()
                : Glib::RefPtr<Gtk::Adjustment>{};
        const double previous_scroll =
            adjustment
                ? adjustment->get_value()
                : 0.0;
        const std::string activity_identity =
            payload->second.activity_identity;
        const std::string full_text =
            payload->second.full_text;
        const std::string displayed_text =
            buffer->get_text(
                body_start,
                marker_end,
                true).raw();
        const bool is_expanded =
            expanded_activity_ids_.find(
                activity_identity) !=
            expanded_activity_ids_.end();

        if (visible_buffer) {
            transcript_follow_output_ = false;
        }

        if (is_expanded) {
            expanded_activity_ids_.erase(
                activity_identity);
        } else {
            expanded_activity_ids_.insert(
                activity_identity);
        }

        const std::string replacement_text =
            expandable_activity_text(
                full_text,
                activity_identity);

        buffer->erase(body_start, marker_end);

        auto insertion =
            buffer->get_iter_at_offset(
                insertion_offset);
        buffer->insert(
            insertion,
            Glib::ustring(replacement_text));

        const std::string transcript_prefix =
            buffer->get_text(
                buffer->begin(),
                buffer->get_iter_at_offset(
                    insertion_offset),
                true).raw();
        const TranscriptStyleState style_state =
            transcript_style_state(
                transcript_prefix);
        apply_transcript_tags_to_buffer(
            buffer,
            insertion_offset,
            style_state.inside_code_fence,
            style_state.inside_user_section,
            style_state.inside_diff_activity,
            style_state.code_language);

        ThreadTurnSession* session = nullptr;

        for (auto& entry : turn_sessions_) {
            if (
                entry.second != nullptr &&
                entry.second->transcript_buffer == buffer
            ) {
                session = entry.second.get();
                break;
            }
        }

        if (
            session != nullptr &&
            session->transcript_buffer == buffer
        ) {
            const std::size_t rendered_position =
                session->rendered_tail.find(
                    displayed_text);

            if (rendered_position != std::string::npos) {
                session->rendered_tail.replace(
                    rendered_position,
                    displayed_text.size(),
                    replacement_text);
            }
        }

        if (visible_buffer) {
            refresh_transcript_bottom_button();
        }

        if (adjustment) {
            Glib::signal_idle().connect(
                [adjustment, previous_scroll]() {
                    adjustment->set_value(
                        std::clamp(
                            previous_scroll,
                            adjustment->get_lower(),
                            std::max(
                                adjustment->get_lower(),
                                adjustment->get_upper() -
                                    adjustment->get_page_size())));
                    return false;
                });
        }

        return;
    }

    void render_live_turn(
        ThreadTurnSession& session
    ) {
        if (!session.transcript_buffer) {
            return;
        }

        std::string rendered;
        std::size_t activity_index = 0;

        for (
            const LiveTurnEntry& entry :
            session.live_entries
        ) {
            if (
                live_stream_text_entry(entry) &&
                !entry.text.empty() &&
                entry.displayed_text.empty()
            ) {
                break;
            }

            if (entry.kind == LiveEntryKind::Reasoning) {
                append_rendered_block(
                    rendered,
                    "Codex reasoning",
                    entry.displayed_text,
                    !entry.stream_complete);
                continue;
            }

            if (entry.kind == LiveEntryKind::AgentMessage) {
                append_rendered_block(
                    rendered,
                    entry.phase == "commentary"
                        ? "Codex commentary"
                        : "Codex",
                    entry.displayed_text,
                    !entry.stream_complete);
                continue;
            }

            if (entry.kind == LiveEntryKind::Plan) {
                append_rendered_block(
                    rendered,
                    "Codex plan",
                    entry.displayed_text,
                    !entry.stream_complete);
                continue;
            }


            if (entry.kind == LiveEntryKind::FollowUp) {
                std::string heading =
                    "Follow-up";

                if (entry.state == "rejected") {
                    heading = "Follow-up rejected";
                }

                std::string body = entry.text;

                if (!entry.output.empty()) {
                    if (!body.empty()) {
                        body += '\n';
                    }

                    body += entry.output;
                }

                append_rendered_block(
                    rendered,
                    heading,
                    body);
                continue;
            }

            const nlohmann::json item =
                live_activity_item(entry);
            const std::string activity_identity =
                session.thread_id + ":" +
                (
                    entry.item_id.empty()
                        ? "live-" +
                            std::to_string(
                                activity_index)
                        : entry.item_id
                );
            ++activity_index;

            const std::string full_activity =
                activity_body(
                    item,
                    entry.state,
                    entry.output,
                    true);

            append_rendered_block(
                rendered,
                activity_heading(
                    item,
                    entry.state),
                expandable_activity_text(
                    full_activity,
                    activity_identity));
        }

        if (
            session.live_turn_has_base_transcript &&
            !rendered.empty()
        ) {
            rendered = "\n\n" + rendered;
        }

        const auto buffer =
            session.transcript_buffer;
        const bool preserve_visible_scroll =
            session.thread_id == current_thread_id_ &&
            !transcript_follow_output_;
        const auto visible_adjustment =
            preserve_visible_scroll
                ? transcript_scroll_.get_vadjustment()
                : Glib::RefPtr<Gtk::Adjustment>{};
        const double previous_scroll =
            visible_adjustment
                ? visible_adjustment->get_value()
                : 0.0;

        const bool append_only =
            rendered.size() >=
                session.rendered_tail.size() &&
            rendered.compare(
                0,
                session.rendered_tail.size(),
                session.rendered_tail) == 0;

        int style_start =
            session.live_turn_start_mark
                ? buffer->get_iter_at_mark(
                    session.live_turn_start_mark)
                    .get_offset()
                : 0;
        TranscriptStyleState style_state;
        bool transcript_modified = false;

        if (append_only) {
            const std::string suffix =
                rendered.substr(
                    session.rendered_tail.size());

            if (!suffix.empty()) {
                std::size_t restyle_start =
                    session.rendered_tail.rfind('\n');

                restyle_start =
                    restyle_start == std::string::npos
                        ? 0
                        : restyle_start + 1;

                const std::string rendered_prefix =
                    rendered.substr(0, restyle_start);
                style_state =
                    transcript_style_state(
                        rendered_prefix);
                style_start +=
                    static_cast<int>(
                        Glib::ustring(
                            rendered_prefix)
                            .size());

                auto end = buffer->end();
                buffer->insert(
                    end,
                    Glib::ustring(suffix));
                transcript_modified = true;
            }
        } else {
            auto start =
                session.live_turn_start_mark
                    ? buffer->get_iter_at_mark(
                        session.live_turn_start_mark)
                    : buffer->end();
            auto end = buffer->end();

            buffer->erase(start, end);

            end = buffer->end();
            buffer->insert(
                end,
                Glib::ustring(rendered));
            transcript_modified = true;
        }

        session.rendered_tail = rendered;

        if (transcript_modified) {
            apply_transcript_tags_to_buffer(
                buffer,
                style_start,
                style_state.inside_code_fence,
                style_state.inside_user_section,
                style_state.inside_diff_activity,
                style_state.code_language);
        }

        if (session.thread_id == current_thread_id_) {
            scroll_transcript_to_end();

            if (visible_adjustment && !append_only) {
                Glib::signal_idle().connect(
                    [visible_adjustment, previous_scroll]() {
                        visible_adjustment->set_value(
                            std::clamp(
                                previous_scroll,
                                visible_adjustment->get_lower(),
                                std::max(
                                    visible_adjustment->get_lower(),
                                    visible_adjustment->get_upper() -
                                        visible_adjustment->get_page_size())));
                        return false;
                    });
            }
        }
    }

    void begin_live_turn(
        ThreadTurnSession& session
    ) {
        session.active_turn_id.clear();
        session.stop_requested = false;
        session.interrupt_sent = false;
        session.reasoning_summary.clear();
        session.agent_text.clear();
        session.live_entries.clear();
        session.rendered_tail.clear();
        session.pending_follow_ups.clear();
        session.follow_up_request_entries.clear();

        if (!session.transcript_buffer) {
            session.transcript_buffer =
                create_thread_transcript_buffer();
        }

        const auto buffer =
            session.transcript_buffer;
        auto end = buffer->end();

        if (session.live_turn_start_mark) {
            buffer->delete_mark(
                session.live_turn_start_mark);
        }

        session.live_turn_has_base_transcript =
            buffer->get_char_count() > 0;
        session.live_turn_start_mark =
            buffer->create_mark(
                end,
                true);
    }

    void finalize_live_activities(
        ThreadTurnSession& session,
        const std::string& state
    ) {
        bool changed = false;

        for (
            LiveTurnEntry& entry :
            session.live_entries
        ) {
            if (
                entry.kind == LiveEntryKind::Activity &&
                entry.state == "running"
            ) {
                entry.state = state;
                changed = true;
            }
        }

        if (changed) {
            render_live_turn(session);
        }
    }

    std::string request_approval(
        const AppServerClient::ApprovalRequest& request
    ) {
        auto pending =
            std::make_shared<PendingApprovalState>();
        pending->request = request;

        std::unique_lock<std::mutex> lock(
            approval_mutex_);

        if (shutting_down_) {
            return "cancel";
        }

        pending_approvals_.push_back(pending);

        lock.unlock();

        approval_dispatcher_.emit();

        lock.lock();

        pending->condition.wait(
            lock,
            [this, &pending]() {
                return
                    pending->resolved ||
                    shutting_down_;
            });

        const std::string decision =
            pending->decision;

        pending_approvals_.erase(
            std::remove(
                pending_approvals_.begin(),
                pending_approvals_.end(),
                pending),
            pending_approvals_.end());

        return decision.empty()
            ? "cancel"
            : decision;
    }

    void handle_approval_request() {
        std::shared_ptr<PendingApprovalState> pending;
        AppServerClient::ApprovalRequest request;

        {
            std::lock_guard<std::mutex> lock(
                approval_mutex_);

            if (approval_dialog_open_) {
                return;
            }

            const auto matching =
                std::find_if(
                    pending_approvals_.begin(),
                    pending_approvals_.end(),
                    [this](
                        const std::shared_ptr<
                            PendingApprovalState>& candidate
                    ) {
                        return
                            candidate != nullptr &&
                            !candidate->resolved &&
                            candidate->request.thread_id ==
                                current_thread_id_;
                    });

            if (matching == pending_approvals_.end()) {
                schedule_sidebar_refresh();
                return;
            }

            pending = *matching;
            request = pending->request;
            approval_dialog_open_ = true;
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
        const bool permissions_approval =
            request.method ==
            "item/permissions/requestApproval";

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
        } else if (permissions_approval) {
            append_permission_detail(
                "Requested permissions",
                json_string("permissions"));
            append_permission_detail(
                "Environment",
                json_string("environmentId"));
        }

        const std::string approval_title =
            command_approval
                ? "Run this command?"
                : (
                    permissions_approval
                        ? "Grant these permissions?"
                        : "Allow these file changes?"
                );

        const std::string approval_summary =
            command_approval
                ? "Codex needs your approval before it can run this command."
                : (
                    permissions_approval
                        ? "Codex needs additional access before it can continue."
                        : "Codex needs your approval before it can write these changes."
                );

        const std::string action_heading =
            command_approval
                ? "COMMAND"
                : (
                    permissions_approval
                        ? "REQUESTED ACCESS"
                        : "WRITE ACCESS"
                );

        const std::string action_text =
            command_approval
                ? (
                    command.empty()
                        ? "Command details unavailable"
                        : command
                )
                : (
                    permissions_approval
                        ? (
                            json_string("permissions").empty()
                                ? "Permission details unavailable"
                                : json_string("permissions")
                        )
                        : (
                            grant_root.empty()
                                ? "Requested files were not specified"
                                : grant_root
                        )
                );

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

        Gtk::Label title(approval_title);

        title.set_xalign(0.0F);
        title.get_style_context()->add_class(
            "approval-title");
        shell.pack_start(
            title,
            Gtk::PACK_SHRINK);

        Gtk::Label summary(approval_summary);

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

        Gtk::Label action_label(action_heading);

        action_label.set_xalign(0.0F);
        action_label.get_style_context()->add_class(
            "approval-card-label");
        action_card.pack_start(
            action_label,
            Gtk::PACK_SHRINK);

        Gtk::Label action_value(action_text);

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
            command_approval || permissions_approval
                ? "WORKING DIRECTORY"
                : "PROJECT");
        Gtk::Label location_value(
            command_approval || permissions_approval
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
                ? "Approve only if you recognize the command and the access it requests."
                : (
                    permissions_approval
                        ? "Approve only if this access matches the work you asked Codex to perform."
                        : "Approve only if this write access matches the change you asked Codex to make."
                ));

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
                pending != nullptr &&
                !pending->resolved
            ) {
                pending->decision =
                    decision;
                pending->resolved = true;
            }

            approval_dialog_open_ = false;
        }

        if (pending != nullptr) {
            pending->condition.notify_all();
        }

        schedule_sidebar_refresh();
        approval_dispatcher_.emit();

        if (
            turn_in_progress_ &&
            request.thread_id ==
                current_thread_id_
        ) {
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

        std::set<std::string>
            transcript_changed_threads;

        for (
            const AppServerClient::TurnEvent& event :
            events
        ) {
            ThreadTurnSession* session =
                find_turn_session(event.thread_id);

            if (session == nullptr) {
                continue;
            }

            switch (event.type) {
            case AppServerClient::TurnEvent::Type::
                TurnStarted:
                session->active_turn_id =
                    event.turn_id;

                if (!session->pending_follow_ups.empty()) {
                    flush_pending_follow_ups(*session);
                    transcript_changed_threads.insert(
                        session->thread_id);
                }

                if (
                    session->stop_requested &&
                    !session->interrupt_sent &&
                    session->client
                ) {
                    const auto interrupted =
                        session->client->interrupt_turn(
                            session->thread_id,
                            event.turn_id);

                    if (interrupted.success) {
                        session->interrupt_sent = true;
                    } else {
                        session->stop_requested = false;

                        if (
                            session->thread_id ==
                            current_thread_id_
                        ) {
                            stop_requested_ = false;
                            status_label_.set_text(
                                "Codex: stop request failed");
                        }
                    }
                }

                if (
                    session->thread_id ==
                    current_thread_id_
                ) {
                    active_turn_id_ = event.turn_id;
                    update_send_button_state();
                }
                break;

            case AppServerClient::TurnEvent::Type::
                SteerAccepted:
            case AppServerClient::TurnEvent::Type::
                SteerRejected: {
                int request_id = 0;

                if (
                    event.message.contains("id") &&
                    event.message["id"].is_number_integer()
                ) {
                    request_id =
                        event.message["id"].get<int>();
                }

                const auto pending =
                    session->follow_up_request_entries.find(
                        request_id);

                if (
                    pending !=
                    session->follow_up_request_entries.end()
                ) {
                    const bool pause_request =
                        pending->second.rfind(
                            "threaddeck-pause-",
                            0) == 0;
                    LiveTurnEntry* entry =
                        find_live_entry(
                            *session,
                            pending->second);

                    if (
                        entry != nullptr &&
                        event.type ==
                            AppServerClient::TurnEvent::Type::
                                SteerRejected
                    ) {
                        entry->state = "rejected";
                        entry->output =
                            app_server_error_message(
                                event.message);

                        if (pause_request) {
                            pause_requested_threads_.erase(
                                session->thread_id);
                        }
                    }

                    session->follow_up_request_entries.erase(
                        pending);
                    transcript_changed_threads.insert(
                        session->thread_id);
                }

                if (
                    session->thread_id ==
                    current_thread_id_
                ) {
                    status_label_.set_text(
                        event.type ==
                            AppServerClient::TurnEvent::Type::
                                SteerAccepted
                            ? "Codex: follow-up accepted"
                            : "Codex: follow-up rejected");
                    update_pause_button();
                }

                break;
            }

            case AppServerClient::TurnEvent::Type::
                AgentMessageDelta:
                session->agent_text += event.delta;
                append_live_text_delta(
                    ensure_live_entry(
                        *session,
                        LiveEntryKind::AgentMessage,
                        event.item_id),
                    event.delta);
                break;

            case AppServerClient::TurnEvent::Type::
                ReasoningSummaryDelta:
                session->reasoning_summary +=
                    event.delta;
                append_live_text_delta(
                    ensure_live_entry(
                        *session,
                        LiveEntryKind::Reasoning,
                        event.item_id),
                    event.delta);
                break;

            case AppServerClient::TurnEvent::Type::
                ReasoningTextDelta:
                break;

            case AppServerClient::TurnEvent::Type::
                PlanDelta:
                append_live_text_delta(
                    ensure_live_entry(
                        *session,
                        LiveEntryKind::Plan,
                        event.item_id,
                        "plan"),
                    event.delta);
                break;

            case AppServerClient::TurnEvent::Type::
                CommandExecutionOutputDelta: {
                LiveTurnEntry& entry =
                    ensure_live_entry(
                        *session,
                        LiveEntryKind::Activity,
                        event.item_id,
                        "commandExecution");

                entry.output += event.delta;
                transcript_changed_threads.insert(
                    session->thread_id);
                break;
            }

            case AppServerClient::TurnEvent::Type::
                TokenUsageUpdated: {
                if (
                    event.message.contains("params") &&
                    event.message["params"].is_object() &&
                    event.message["params"].contains(
                        "tokenUsage") &&
                    event.message["params"]["tokenUsage"]
                        .is_object()
                ) {
                    if (
                        session->thread_id ==
                        current_thread_id_
                    ) {
                        thread_token_usage_ =
                            event.message["params"][
                                "tokenUsage"];

                        refresh_usage_label();
                    }
                }

                break;
            }

            case AppServerClient::TurnEvent::Type::
                ThreadSettingsUpdated: {
                if (
                    event.message.contains("params") &&
                    event.message["params"].is_object() &&
                    event.message["params"].contains(
                        "threadSettings") &&
                    event.message["params"][
                        "threadSettings"].is_object()
                ) {
                    const auto& settings =
                        event.message["params"][
                            "threadSettings"];

                    if (
                        settings.contains("model") &&
                        settings["model"].is_string()
                    ) {
                        session->options.model =
                            settings["model"]
                                .get<std::string>();
                    }

                    if (
                        settings.contains("effort") &&
                        settings["effort"].is_string()
                    ) {
                        session->options.reasoning_effort =
                            settings["effort"]
                                .get<std::string>();
                    }

                    if (
                        settings.contains("cwd") &&
                        settings["cwd"].is_string() &&
                        thread_project_assignments_.find(
                            session->thread_id) ==
                            thread_project_assignments_.end()
                    ) {
                        session->cwd =
                            settings["cwd"]
                                .get<std::string>();
                        session->options.cwd =
                            session->cwd;
                    }

                    if (
                        settings.contains(
                            "approvalPolicy")
                    ) {
                        session->options.approval_policy =
                            settings[
                                "approvalPolicy"];
                    }

                    if (
                        settings.contains(
                            "sandboxPolicy")
                    ) {
                        session->options.sandbox_policy =
                            settings[
                                "sandboxPolicy"];
                    }

                    if (
                        session->thread_id ==
                            current_thread_id_
                    ) {
                        effective_model_ =
                            session->options.model;
                        effective_reasoning_effort_ =
                            session->options
                                .reasoning_effort;
                        effective_approval_policy_ =
                            session->options
                                .approval_policy;
                        effective_sandbox_policy_ =
                            session->options
                                .sandbox_policy;
                    }

                    if (
                        settings.contains(
                            "collaborationMode") &&
                        settings["collaborationMode"]
                            .is_object()
                    ) {
                        session->mode =
                            settings[
                                "collaborationMode"]
                                .value(
                                    "mode",
                                    session->mode);

                        if (
                            session->thread_id ==
                            current_thread_id_
                        ) {
                            effective_mode_ =
                                session->mode;
                        }
                    }

                    if (
                        session->thread_id ==
                        current_thread_id_
                    ) {
                        refresh_session_controls();
                    }
                }

                break;
            }

            case AppServerClient::TurnEvent::Type::
                AccountRateLimitsUpdated: {
                if (
                    event.message.contains("params") &&
                    event.message["params"].is_object() &&
                    event.message["params"].contains(
                        "rateLimits") &&
                    event.message["params"]["rateLimits"]
                        .is_object()
                ) {
                    const auto& update =
                        event.message["params"][
                            "rateLimits"];

                    const std::string update_limit_id =
                        json_string_field(
                            update,
                            "limitId");

                    auto& snapshot =
                        account_rate_limits_[
                            "rateLimits"];

                    const std::string snapshot_limit_id =
                        json_string_field(
                            snapshot,
                            "limitId");

                    if (
                        update_limit_id.empty() ||
                        snapshot_limit_id.empty() ||
                        update_limit_id ==
                            snapshot_limit_id
                    ) {
                        merge_sparse_json_object(
                            snapshot,
                            update);
                    }

                    if (!update_limit_id.empty()) {
                        auto& snapshots_by_id =
                            account_rate_limits_[
                                "rateLimitsByLimitId"];

                        if (!snapshots_by_id.is_object()) {
                            snapshots_by_id =
                                nlohmann::json::object();
                        }

                        merge_sparse_json_object(
                            snapshots_by_id[
                                update_limit_id],
                            update);
                    }

                    refresh_usage_label();
                }

                break;
            }

            case AppServerClient::TurnEvent::Type::
                ItemStarted:
            case AppServerClient::TurnEvent::Type::
                ItemCompleted: {
                const std::string type =
                    json_string_field(
                        event.item,
                        "type");

                if (
                    type == "commandExecution" &&
                    event.item.contains("command") &&
                    event.item["command"].is_string()
                ) {
                    observe_remote_host_from_command(
                        session->thread_id,
                        event.item["command"]
                            .get<std::string>());
                }

                if (type == "userMessage") {
                    break;
                }

                if (type == "agentMessage") {
                    LiveTurnEntry& entry =
                        ensure_live_entry(
                            *session,
                            LiveEntryKind::AgentMessage,
                            event.item_id,
                            type);

                    entry.phase = json_string_field(
                        event.item,
                        "phase",
                        entry.phase);

                    if (
                        event.type ==
                            AppServerClient::TurnEvent::Type::
                                ItemCompleted &&
                        event.item.contains("text") &&
                        event.item["text"].is_string()
                    ) {
                        entry.text = event.item["text"]
                            .get<std::string>();
                    }

                    if (
                        event.type ==
                            AppServerClient::TurnEvent::Type::
                                ItemCompleted
                    ) {
                        complete_live_text_entry(entry);
                    }
                    break;
                }

                if (type == "reasoning") {
                    LiveTurnEntry& entry =
                        ensure_live_entry(
                            *session,
                            LiveEntryKind::Reasoning,
                            event.item_id,
                            type);

                    if (
                        event.type ==
                            AppServerClient::TurnEvent::Type::
                                ItemCompleted &&
                        event.item.contains("summary") &&
                        event.item["summary"].is_array()
                    ) {
                        std::string summary;

                        for (
                            const auto& part :
                            event.item["summary"]
                        ) {
                            if (!part.is_string()) {
                                continue;
                            }

                            if (!summary.empty()) {
                                summary += '\n';
                            }

                            summary += part.get<std::string>();
                        }

                        if (!summary.empty()) {
                            entry.text =
                                std::move(summary);
                        }
                    }

                    if (
                        event.type ==
                            AppServerClient::TurnEvent::Type::
                                ItemCompleted
                    ) {
                        complete_live_text_entry(entry);
                    }
                    break;
                }

                if (type == "plan") {
                    LiveTurnEntry& entry =
                        ensure_live_entry(
                            *session,
                            LiveEntryKind::Plan,
                            event.item_id,
                            type);

                    if (
                        event.type ==
                            AppServerClient::TurnEvent::Type::
                                ItemCompleted
                    ) {
                        entry.text = json_string_field(
                            event.item,
                            "text",
                            entry.text);
                        complete_live_text_entry(entry);
                    }

                    break;
                }

                LiveTurnEntry& entry =
                    ensure_live_entry(
                        *session,
                        LiveEntryKind::Activity,
                        event.item_id,
                        type);

                entry.type = type;
                entry.label =
                    activity_label(type);
                if (!entry.item.is_object()) {
                    entry.item =
                        nlohmann::json::object();
                }

                for (
                    auto field = event.item.begin();
                    field != event.item.end();
                    ++field
                ) {
                    entry.item[field.key()] =
                        field.value();
                }
                entry.state =
                    event.type ==
                        AppServerClient::TurnEvent::Type::
                            ItemStarted
                        ? "running"
                        : activity_state(
                            event.item,
                            {},
                            "completed");

                if (
                    type == "commandExecution" &&
                    event.item.contains(
                        "aggregatedOutput") &&
                    event.item["aggregatedOutput"]
                        .is_string()
                ) {
                    entry.output =
                        event.item["aggregatedOutput"]
                            .get<std::string>();
                }

                transcript_changed_threads.insert(
                    session->thread_id);
                break;
            }
            }
        }

        for (const auto& thread_id :
             transcript_changed_threads) {
            if (auto* session =
                find_turn_session(thread_id)) {
                render_live_turn(*session);
            }
        }
    }

    LiveTurnEntry* find_live_entry(
        ThreadTurnSession& session,
        const std::string& entry_id
    ) {
        const auto found = std::find_if(
            session.live_entries.begin(),
            session.live_entries.end(),
            [&entry_id](const LiveTurnEntry& entry) {
                return entry.item_id == entry_id;
            });

        return found == session.live_entries.end()
            ? nullptr
            : &*found;
    }

    static std::string app_server_error_message(
        const nlohmann::json& message
    ) {
        if (!message.contains("error")) {
            return "Codex did not accept the follow-up.";
        }

        const auto& error = message["error"];

        if (
            error.is_object() &&
            error.contains("message") &&
            error["message"].is_string()
        ) {
            return error["message"].get<std::string>();
        }

        return error.is_string()
            ? error.get<std::string>()
            : error.dump();
    }

    bool send_follow_up(
        ThreadTurnSession& session,
        const PendingFollowUp& follow_up
    ) {
        LiveTurnEntry* entry =
            find_live_entry(
                session,
                follow_up.entry_id);

        if (
            !session.busy ||
            !session.client ||
            session.active_turn_id.empty()
        ) {
            if (entry != nullptr) {
                entry->state = "rejected";
                entry->output =
                    "The active turn ended before this follow-up could be sent.";
            }

            return false;
        }

        const auto result =
            session.client->steer_turn(
                session.thread_id,
                session.active_turn_id,
                follow_up.input);

        if (!result.success) {
            if (entry != nullptr) {
                entry->state = "rejected";
                entry->output = result.error;
            }

            return false;
        }

        if (entry != nullptr) {
            entry->state = "sent";
            entry->output.clear();
        }

        session.follow_up_request_entries[
            result.request_id] =
            follow_up.entry_id;

        return true;
    }

    void flush_pending_follow_ups(
        ThreadTurnSession& session
    ) {
        while (!session.pending_follow_ups.empty()) {
            PendingFollowUp follow_up =
                std::move(
                    session.pending_follow_ups.front());
            session.pending_follow_ups.pop_front();

            if (!send_follow_up(session, follow_up)) {
                if (
                    follow_up.entry_id.rfind(
                        "threaddeck-pause-",
                        0) == 0
                ) {
                    pause_requested_threads_.erase(
                        session.thread_id);

                    if (
                        session.thread_id ==
                        current_thread_id_
                    ) {
                        update_pause_button();
                    }
                }
            }
        }
    }

    void continue_current_thread() {
        if (current_thread_id_.empty()) {
            status_label_.set_text(
                "Codex: no active thread to continue");
            return;
        }

        ThreadTurnSession* session =
            find_turn_session(current_thread_id_);

        if (
            session != nullptr &&
            session->busy
        ) {
            if (
                session->work_kind !=
                    SessionWorkKind::Turn ||
                session->stop_requested
            ) {
                status_label_.set_text(
                    "Codex: this thread cannot continue yet");
                return;
            }

            PendingFollowUp follow_up;
            follow_up.entry_id =
                "threaddeck-follow-up-" +
                std::to_string(
                    ++follow_up_sequence_);
            follow_up.input.push_back(
                {
                    {"type", "text"},
                    {"text", "continue"},
                    {"text_elements",
                     nlohmann::json::array()},
                });

            LiveTurnEntry entry;
            entry.kind = LiveEntryKind::FollowUp;
            entry.item_id = follow_up.entry_id;
            entry.state = "queued";
            entry.text = "continue";
            session->live_entries.push_back(
                std::move(entry));

            if (session->active_turn_id.empty()) {
                session->pending_follow_ups.push_back(
                    std::move(follow_up));
                status_label_.set_text(
                    "Codex: continue queued for this thread");
            } else if (
                send_follow_up(
                    *session,
                    follow_up)
            ) {
                status_label_.set_text(
                    "Codex: continue sent to this thread");
            } else {
                status_label_.set_text(
                    "Codex: continue could not be sent");
            }

            render_live_turn(*session);
            update_send_button_state();
            return;
        }

        nlohmann::json turn_input =
            nlohmann::json::array();
        turn_input.push_back(
            {
                {"type", "text"},
                {"text", "continue"},
                {"text_elements",
                 nlohmann::json::array()},
            });

        append_user_content_to_transcript(
            turn_input);

        start_structured_turn(
            current_thread_id_,
            turn_input,
            turn_input,
            current_session_options());
    }

    void submit_follow_up() {
        ThreadTurnSession* session =
            find_turn_session(current_thread_id_);

        const std::string text = trim(
            prompt_.get_buffer()->get_text().raw());
        const bool has_attachments =
            !attached_image_paths_.empty() ||
            !attached_audio_paths_.empty();

        if (
            session == nullptr ||
            !session->busy ||
            session->work_kind !=
                SessionWorkKind::Turn ||
            (text.empty() && !has_attachments)
        ) {
            return;
        }

        PendingFollowUp follow_up;
        follow_up.entry_id =
            "threaddeck-follow-up-" +
            std::to_string(
                ++follow_up_sequence_);
        if (!text.empty()) {
            follow_up.input.push_back(
                {
                    {"type", "text"},
                    {"text", text},
                    {"text_elements",
                     nlohmann::json::array()},
                });
        }

        for (const auto& image_path :
             attached_image_paths_) {
            follow_up.input.push_back(
                {
                    {"type", "localImage"},
                    {"path", image_path},
                });
        }

        for (const auto& audio_path :
             attached_audio_paths_) {
            follow_up.input.push_back(
                {
                    {"type", "localAudio"},
                    {"path", audio_path},
                });
        }

        LiveTurnEntry entry;
        entry.kind = LiveEntryKind::FollowUp;
        entry.item_id = follow_up.entry_id;
        entry.state = "queued";
        entry.text = text;

        for (const auto& image_path :
             attached_image_paths_) {
            if (!entry.text.empty()) {
                entry.text += '\n';
            }

            entry.text +=
                "Attached image: " +
                std::filesystem::path(image_path)
                    .filename().string();
        }

        for (const auto& audio_path :
             attached_audio_paths_) {
            if (!entry.text.empty()) {
                entry.text += '\n';
            }

            entry.text +=
                "Attached audio: " +
                std::filesystem::path(audio_path)
                    .filename().string();
        }

        session->live_entries.push_back(
            std::move(entry));

        queued_temporary_attachment_paths_.clear();
        clear_prompt_after_submission();
        attached_image_paths_.clear();
        attached_audio_paths_.clear();
        refresh_attachment_row();
        skill_popover_.popdown();

        if (session->active_turn_id.empty()) {
            session->pending_follow_ups.push_back(
                std::move(follow_up));
            status_label_.set_text(
                "Codex: follow-up queued until the turn starts");
        } else if (send_follow_up(*session, follow_up)) {
            status_label_.set_text(
                "Codex: follow-up sent to the active turn");
        } else {
            status_label_.set_text(
                "Codex: follow-up could not be sent");
        }

        render_live_turn(*session);
        update_send_button_state();
    }

    void request_turn_stop() {
        ThreadTurnSession* session =
            find_turn_session(current_thread_id_);

        if (
            session == nullptr ||
            !session->client ||
            !session->busy ||
            (
                session->work_kind !=
                    SessionWorkKind::Turn &&
                session->work_kind !=
                    SessionWorkKind::Compaction &&
                session->work_kind !=
                    SessionWorkKind::SummarizeAndTitle
            ) ||
            !turn_in_progress_ ||
            stop_requested_
        ) {
            return;
        }

        stop_requested_ = true;
        session->stop_requested = true;
        status_label_.set_text("Codex: stopping");
        send_button_.set_tooltip_text("Stop requested");
        update_send_button_state();

        if (session->active_turn_id.empty()) {
            std::cout
                << "PASS: GTK queued interruption until the turn starts\n";
            return;
        }

        const auto result =
            session->client->interrupt_turn(
                session->thread_id,
                session->active_turn_id);

        if (!result.success) {
            stop_requested_ = false;
            session->stop_requested = false;
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

        session->interrupt_sent = true;

        std::cout
            << "PASS: GTK requested interruption for turn "
            << session->active_turn_id
            << " in thread "
            << session->thread_id
            << '\n';
    }

    void handle_send_or_stop() {
        ThreadTurnSession* session =
            find_turn_session(current_thread_id_);

        if (
            session != nullptr &&
            session->busy &&
            session->work_kind ==
                SessionWorkKind::ShellCommand
        ) {
            return;
        }

        if (session != nullptr && session->busy) {
            const std::string follow_up_text = trim(
                prompt_.get_buffer()->get_text().raw());
            const bool has_follow_up =
                !follow_up_text.empty() ||
                !attached_image_paths_.empty() ||
                !attached_audio_paths_.empty();

            if (
                session != nullptr &&
                session->work_kind ==
                    SessionWorkKind::Turn &&
                has_follow_up
            ) {
                submit_follow_up();
            } else {
                request_turn_stop();
            }
            return;
        }

        submit_prompt();
    }

    void start_structured_turn(
        const std::string& thread_id,
        const nlohmann::json& turn_input,
        const nlohmann::json& transcript_input,
        const AppServerClient::SessionOptions&
            session_options
    ) {
        auto& session_pointer =
            turn_sessions_[thread_id];

        if (!session_pointer) {
            session_pointer =
                std::make_unique<
                    ThreadTurnSession>();
            session_pointer->thread_id =
                thread_id;
        }

        ThreadTurnSession& session =
            *session_pointer;

        const bool was_paused =
            paused_threads_.erase(thread_id) > 0;
        pause_requested_threads_.erase(thread_id);

        if (was_paused) {
            save_ui_state();
        }

        if (thread_id == current_thread_id_) {
            update_pause_button();
        }

        if (session.worker.joinable()) {
            session.worker.join();
        }

        session.cwd = selected_folder_path_;
        session.busy = true;
        session.failed = false;
        session.work_kind =
            SessionWorkKind::Turn;
        session.base_thread = current_thread_data_;
        session.transcript_input =
            transcript_input;
        session.pending_display.clear();
        session.options = session_options;
        session.options.cwd = session.cwd;
        session.mode = effective_mode_;
        prepare_session_process_environment(session);

        if (!session.client) {
            session.client =
                std::make_unique<
                    AppServerClient>();
        }

        begin_live_turn(session);

        status_label_.set_text("Codex: working");
        current_thread_turn_failed_ = false;
        set_turn_busy(true);
        refresh_sidebar_threads();

        std::cout
            << "PASS: GTK submitted a turn for thread "
            << thread_id
            << '\n';

        session.worker = std::thread(
            [
                this,
                session = &session,
                thread_id,
                turn_input,
                session_options
            ]() {
                AppServerClient::TurnResult result;
                std::string error;

                if (
                    ensure_session_client_ready(
                        *session,
                        session_options,
                        error)
                ) {
                    result =
                        session->client
                            ->start_turn_with_input(
                                thread_id,
                                turn_input,
                                60000,
                                [this](
                                    const AppServerClient::TurnEvent& event
                                ) {
                                    {
                                        std::lock_guard<
                                            std::mutex
                                        > lock(
                                            turn_event_mutex_);
                                        pending_turn_events_
                                            .push_back(event);
                                    }

                                    turn_event_dispatcher_.emit();
                                },
                                [this](
                                    const AppServerClient::ApprovalRequest&
                                        request
                                ) {
                                    return request_approval(
                                        request);
                                },
                                session_options);
                } else {
                    result.error =
                        "Could not prepare the thread's "
                        "Codex App Server: " +
                        error;
                }

                {
                    std::lock_guard<std::mutex> lock(
                        turn_result_mutex_);

                    pending_turn_results_.push_back(
                        {
                            thread_id,
                            std::move(result),
                        });
                }

                turn_dispatcher_.emit();
            });
    }

    void set_shell_command_busy(bool busy) {
        shell_command_in_progress_ = busy;

        new_thread_button_.set_sensitive(
            app_server_.is_running() &&
            !selected_folder_path_.empty());

        prompt_.set_editable(!busy);

        send_button_.set_tooltip_text(
            busy
                ? "Shell command is running"
                : "Send message (Enter)");

        update_send_button_state();
    }

    void set_turn_busy(bool busy) {
        turn_in_progress_ = busy;

        new_thread_button_.set_sensitive(
            app_server_.is_running() &&
            !selected_folder_path_.empty());

        prompt_.set_editable(true);

        if (busy) {
            send_button_.set_tooltip_text(
                "Type a follow-up, or press Esc to stop");
        } else {
            active_turn_id_.clear();
            stop_requested_ = false;
            send_image_.set_from_icon_name(
                "mail-send-symbolic",
                Gtk::ICON_SIZE_BUTTON);
            send_button_.set_tooltip_text(
                "Send message (Enter)");
        }

        update_send_button_state();
        refresh_session_control_sensitivity();
        update_pause_button();
    }

    void create_thread_for_selected_folder() {
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

        const AppServerClient::SessionOptions options =
            current_session_options();
        const AppServerClient::ProcessEnvironment
            process_environment =
                current_codex_process_environment();

        new_thread_button_.set_sensitive(false);
        send_button_.set_sensitive(false);

        auto thread_client =
            std::make_unique<AppServerClient>();
        std::string client_error;

        if (
            !thread_client->start(
                client_error,
                process_environment)
        ) {
            status_label_.set_text(
                "Codex: thread creation failed");
            new_thread_button_.set_sensitive(true);
            update_send_button_state();

            std::cerr
                << "FAIL: could not start the new thread's "
                   "Codex App Server: "
                << client_error
                << '\n';
            return;
        }

        const auto initialized =
            thread_client->initialize(
                "threaddeck",
                "ThreadDeck",
                "0.1.0");

        if (!initialized.success) {
            status_label_.set_text(
                "Codex: thread creation failed");
            thread_client->shutdown();
            new_thread_button_.set_sensitive(true);
            update_send_button_state();

            std::cerr
                << "FAIL: could not initialize the new thread's "
                   "Codex App Server: "
                << initialized.error
                << '\n';
            return;
        }

        const auto result = thread_client->start_thread(
            selected_folder_path_,
            false,
            10000,
            options);

        new_thread_button_.set_sensitive(
            app_server_.is_running() &&
            !selected_folder_path_.empty());

        if (!result.success) {
            status_label_.set_text(
                "Codex: thread creation failed");
            thread_client->shutdown();

            update_send_button_state();

            std::cerr
                << "FAIL: thread/start: "
                << result.error
                << '\n';

            return;
        }

        current_thread_id_ = result.thread_id;
        if (!selected_project_id_.empty()) {
            thread_project_assignments_[
                result.thread_id] =
                selected_project_id_;
        }
        last_active_thread_id_ = result.thread_id;
        last_active_thread_cwd_ =
            selected_folder_path_;
        current_thread_turn_failed_ = false;
        thread_token_usage_ =
            nlohmann::json::object();

        auto session =
            std::make_unique<ThreadTurnSession>();
        session->thread_id = result.thread_id;
        session->cwd = selected_folder_path_;
        session->options = options;
        session->process_environment =
            process_environment;
        session->desired_environment_generation =
            codex_environment_generation_;
        session->client_environment_generation =
            codex_environment_generation_;
        session->transcript_buffer =
            create_thread_transcript_buffer();
        session->client =
            std::move(thread_client);

        turn_sessions_[result.thread_id] =
            std::move(session);

        attach_thread_transcript_buffer(
            turn_sessions_[result.thread_id]
                ->transcript_buffer);

        set_turn_busy(false);
        set_shell_command_busy(false);

        apply_effective_thread_settings(
            result.model,
            result.reasoning_effort,
            result.approval_policy,
            result.sandbox_policy,
            true);

        set_active_thread_surfaces(
            "New Thread",
            selected_folder_path_,
            current_thread_id_);

        status_label_.set_text("Codex: connected");

        prompt_.set_sensitive(true);
        prompt_.set_editable(true);
        prompt_.set_cursor_visible(true);
        prompt_.set_can_focus(true);
        clear_prompt_after_submission();
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

    void start_thread_compaction() {
        if (current_thread_id_.empty()) {
            status_label_.set_text(
                "Codex: no active thread");
            return;
        }

        const std::string thread_id =
            current_thread_id_;

        const AppServerClient::SessionOptions
            session_options =
                current_session_options();

        auto& session_pointer =
            turn_sessions_[thread_id];

        if (!session_pointer) {
            session_pointer =
                std::make_unique<
                    ThreadTurnSession>();
            session_pointer->thread_id =
                thread_id;
        }

        ThreadTurnSession& session =
            *session_pointer;

        if (session.worker.joinable()) {
            session.worker.join();
        }

        session.cwd = selected_folder_path_;
        session.busy = true;
        session.failed = false;
        session.work_kind =
            SessionWorkKind::Compaction;
        session.base_thread = current_thread_data_;
        session.transcript_input =
            nlohmann::json::array();
        session.pending_display.clear();
        session.options = session_options;
        session.options.cwd = session.cwd;
        session.mode = effective_mode_;
        prepare_session_process_environment(session);

        if (!session.client) {
            session.client =
                std::make_unique<
                    AppServerClient>();
        }

        clear_prompt_after_submission();
        skill_popover_.popdown();

        begin_live_turn(session);

        status_label_.set_text(
            "Codex: compacting thread context");
        current_thread_turn_failed_ = false;
        set_turn_busy(true);
        prompt_.set_editable(false);
        send_button_.set_tooltip_text(
            "Stop context compaction (Esc)");
        refresh_sidebar_threads();

        std::cout
            << "PASS: GTK requested native context compaction for thread "
            << thread_id
            << '\n';

        session.worker = std::thread(
            [
                this,
                session = &session,
                thread_id,
                session_options
            ]() {
                AppServerClient::TurnResult result;
                std::string error;

                if (
                    ensure_session_client_ready(
                        *session,
                        session_options,
                        error)
                ) {
                    result =
                        session->client
                            ->compact_thread(
                                thread_id,
                                60000,
                                [this](
                                    const AppServerClient::TurnEvent& event
                                ) {
                                    {
                                        std::lock_guard<
                                            std::mutex
                                        > lock(
                                            turn_event_mutex_);
                                        pending_turn_events_
                                            .push_back(event);
                                    }

                                    turn_event_dispatcher_.emit();
                                });
                } else {
                    result.error =
                        "Could not prepare the thread's "
                        "Codex App Server: " +
                        error;
                }

                {
                    std::lock_guard<std::mutex> lock(
                        turn_result_mutex_);

                    pending_turn_results_.push_back(
                        {
                            thread_id,
                            std::move(result),
                        });
                }

                turn_dispatcher_.emit();
            });
    }

    void submit_prompt() {
        const ThreadTurnSession* active_session =
            find_turn_session(current_thread_id_);

        if (
            active_session != nullptr &&
            active_session->busy
        ) {
            return;
        }

        if (current_thread_id_.empty()) {
            status_label_.set_text(
                "Codex: no active thread");
            return;
        }

        const std::string prompt_text = trim(
            prompt_.get_buffer()->get_text().raw());

        if (
            prompt_text.empty() &&
            attached_image_paths_.empty() &&
            attached_audio_paths_.empty()
        ) {
            status_label_.set_text(
                "Codex: enter a message");
            return;
        }

        if (prompt_text == "/compact") {
            if (
                !attached_image_paths_.empty() ||
                !attached_audio_paths_.empty()
            ) {
                status_label_.set_text(
                    "Codex: /compact cannot include attachments");
                return;
            }

            start_thread_compaction();
            return;
        }

        if (
            !prompt_text.empty() &&
            prompt_text.front() == '!'
        ) {
            if (
                !attached_image_paths_.empty() ||
                !attached_audio_paths_.empty()
            ) {
                status_label_.set_text(
                    "Codex: !command cannot include attachments");
                return;
            }
            const std::string command =
                trim(prompt_text.substr(1));

            if (command.empty()) {
                status_label_.set_text(
                    "Codex: enter a shell command");
                return;
            }

            const std::string thread_id =
                current_thread_id_;

            const AppServerClient::SessionOptions
                session_options =
                    current_session_options();

            auto& session_pointer =
                turn_sessions_[thread_id];

            if (!session_pointer) {
                session_pointer =
                    std::make_unique<
                        ThreadTurnSession>();
                session_pointer->thread_id =
                    thread_id;
            }

            ThreadTurnSession& session =
                *session_pointer;

            if (session.worker.joinable()) {
                session.worker.join();
            }

            session.cwd = selected_folder_path_;
            session.busy = true;
            session.failed = false;
            session.work_kind =
                SessionWorkKind::ShellCommand;
            session.base_thread =
                current_thread_data_;
            session.transcript_input =
                nlohmann::json::array();
            session.pending_display =
                "You (!command):\n$ " +
                command;
            session.options = session_options;
            session.options.cwd = session.cwd;
            session.mode = effective_mode_;
            prepare_session_process_environment(session);

            if (!session.client) {
                session.client =
                    std::make_unique<
                        AppServerClient>();
            }

            clear_prompt_after_submission();

            append_transcript(
                session.pending_display,
                true);

            status_label_.set_text(
                "Codex: running shell command");

            set_shell_command_busy(true);
            refresh_sidebar_threads();

            std::cout
                << "PASS: GTK submitted !command for thread "
                << thread_id
                << '\n';

            session.worker =
                std::thread(
                    [
                        this,
                        session = &session,
                        thread_id,
                        command,
                        session_options
                    ]() {
                        AppServerClient::JsonResult
                            result;
                        std::string error;

                        if (
                            ensure_session_client_ready(
                                *session,
                                session_options,
                                error)
                        ) {
                            result =
                                session->client
                                    ->run_thread_shell_command(
                                        thread_id,
                                        command,
                                        60000);
                        } else {
                            result.error =
                                "Could not prepare the thread's "
                                "Codex App Server: " +
                                error;
                        }

                        {
                            std::lock_guard<
                                std::mutex
                            > lock(
                                shell_command_result_mutex_);

                            pending_shell_command_results_
                                .push_back(
                                    {
                                        thread_id,
                                        std::move(result),
                                    });
                        }

                        shell_command_dispatcher_.emit();
                    });

            return;
        }

        const std::string thread_id =
            current_thread_id_;

        const AppServerClient::SessionOptions
            session_options =
                current_session_options();

        nlohmann::json turn_input =
            nlohmann::json::array();

        if (!prompt_text.empty()) {
            turn_input.push_back(
                {
                    {"type", "text"},
                    {"text", prompt_text},
                    {"text_elements",
                     nlohmann::json::array()},
                });
        }

        nlohmann::json transcript_input =
            turn_input;

        if (
            !prompt_text.empty() &&
            prompt_text.front() == '$'
        ) {
            const std::size_t token_end =
                prompt_text.find_first_of(
                    " \t\r\n");

            const std::string skill_name =
                prompt_text.substr(
                    1,
                    token_end == std::string::npos
                        ? std::string::npos
                        : token_end - 1);

            const auto* skill =
                find_skill(skill_name);

            if (skill != nullptr) {
                turn_input =
                    nlohmann::json::array();

                turn_input.push_back(
                    {
                        {"type", "skill"},
                        {"name", skill_name},
                        {"path",
                         skill->value(
                             "path",
                             std::string{})},
                    });

                const std::string remainder =
                    token_end == std::string::npos
                        ? std::string{}
                        : trim(
                            prompt_text.substr(
                                token_end));

                if (!remainder.empty()) {
                    turn_input.push_back(
                        {
                            {"type", "text"},
                            {"text", remainder},
                            {"text_elements",
                             nlohmann::json::array()},
                        });
                }

                std::cout
                    << "PASS: resolved native $skill "
                    << skill_name
                    << '\n';
            }
        }

        for (
            const auto& image_path :
            attached_image_paths_
        ) {
            const nlohmann::json input = {
                {"type", "localImage"},
                {"path", image_path},
            };

            turn_input.push_back(input);
            transcript_input.push_back(input);
        }

        for (
            const auto& audio_path :
            attached_audio_paths_
        ) {
            const nlohmann::json input = {
                {"type", "localAudio"},
                {"path", audio_path},
            };

            turn_input.push_back(input);
            transcript_input.push_back(input);
        }

        queued_temporary_attachment_paths_.clear();

        clear_prompt_after_submission();
        skill_popover_.popdown();

        append_user_content_to_transcript(
            transcript_input);

        attached_image_paths_.clear();
        attached_audio_paths_.clear();
        refresh_attachment_row();
        update_send_button_state();

        auto& session_pointer =
            turn_sessions_[thread_id];

        if (!session_pointer) {
            session_pointer =
                std::make_unique<
                    ThreadTurnSession>();
            session_pointer->thread_id =
                thread_id;
        }

        ThreadTurnSession& session =
            *session_pointer;

        if (session.worker.joinable()) {
            session.worker.join();
        }

        session.cwd = selected_folder_path_;
        session.busy = true;
        session.failed = false;
        session.work_kind =
            SessionWorkKind::Turn;
        session.base_thread = current_thread_data_;
        session.transcript_input =
            transcript_input;
        session.pending_display.clear();
        session.options = session_options;
        session.options.cwd = session.cwd;
        session.mode = effective_mode_;
        prepare_session_process_environment(session);

        if (!session.client) {
            session.client =
                std::make_unique<
                    AppServerClient>();
        }

        begin_live_turn(session);

        status_label_.set_text("Codex: working");
        current_thread_turn_failed_ = false;
        set_turn_busy(true);
        refresh_sidebar_threads();

        std::cout
            << "PASS: GTK submitted a turn for thread "
            << thread_id
            << '\n';

        session.worker = std::thread(
            [
                this,
                session = &session,
                thread_id,
                turn_input,
                session_options
            ]() {
                AppServerClient::TurnResult result;
                std::string error;

                if (
                    ensure_session_client_ready(
                        *session,
                        session_options,
                        error)
                ) {
                    result =
                        session->client
                            ->start_turn_with_input(
                                thread_id,
                                turn_input,
                                60000,
                                [this](
                                    const AppServerClient::TurnEvent& event
                                ) {
                                    {
                                        std::lock_guard<
                                            std::mutex
                                        > lock(
                                            turn_event_mutex_);
                                        pending_turn_events_
                                            .push_back(event);
                                    }

                                    turn_event_dispatcher_.emit();
                                },
                                [this](
                                    const AppServerClient::ApprovalRequest&
                                        request
                                ) {
                                    return request_approval(
                                        request);
                                },
                                session_options);
                } else {
                    result.error =
                        "Could not prepare the thread's "
                        "Codex App Server: " +
                        error;
                }

                {
                    std::lock_guard<std::mutex> lock(
                        turn_result_mutex_);

                    pending_turn_results_.push_back(
                        {
                            thread_id,
                            std::move(result),
                        });
                }

                turn_dispatcher_.emit();
            });
    }

    void handle_shell_command_finished() {
        std::deque<CompletedShellCommand>
            completed_commands;

        {
            std::lock_guard<std::mutex> lock(
                shell_command_result_mutex_);

            completed_commands.swap(
                pending_shell_command_results_);
        }

        for (
            CompletedShellCommand& completed :
            completed_commands
        ) {
            ThreadTurnSession* session =
                find_turn_session(
                    completed.thread_id);

            if (session == nullptr) {
                continue;
            }

            if (session->worker.joinable()) {
                session->worker.join();
            }

            AppServerClient::JsonResult& result =
                completed.result;

            const bool is_current =
                completed.thread_id ==
                current_thread_id_;

            completed_unseen_threads_.erase(
                completed.thread_id);

            session->busy = false;
            session->work_kind =
                SessionWorkKind::None;

            if (is_current) {
                set_shell_command_busy(false);
            }

            if (!result.success) {
                session->failed = true;

                if (is_current) {
                    status_label_.set_text(
                        "Codex: shell command failed");

                    append_transcript(
                        "Shell command error:\n" +
                        result.error);
                }

                std::cerr
                    << "FAIL: GTK thread/shellCommand for "
                    << completed.thread_id
                    << ": "
                    << result.error
                    << '\n';

                continue;
            }

            std::string streamed_output;
            std::string aggregated_output;
            int exit_code = 0;
            bool has_exit_code = false;

            for (
                const auto& message :
                result.preceding_messages
            ) {
                if (!message.is_object()) {
                    continue;
                }

                const std::string method =
                    json_string_field(
                        message,
                        "method");

                if (
                    method ==
                    "item/commandExecution/outputDelta"
                ) {
                    if (
                        message.contains("params") &&
                        message["params"].is_object()
                    ) {
                        streamed_output +=
                            json_string_field(
                                message["params"],
                                "delta");
                    }

                    continue;
                }

                if (
                    method != "item/completed" ||
                    !message.contains("params") ||
                    !message["params"].is_object()
                ) {
                    continue;
                }

                const auto& params =
                    message["params"];

                if (
                    !params.contains("item") ||
                    !params["item"].is_object()
                ) {
                    continue;
                }

                const auto& item =
                    params["item"];

                if (
                    json_string_field(
                        item,
                        "type") !=
                    "commandExecution"
                ) {
                    continue;
                }

                if (
                    item.contains("aggregatedOutput") &&
                    item["aggregatedOutput"].is_string()
                ) {
                    aggregated_output =
                        item["aggregatedOutput"]
                            .get<std::string>();
                }

                if (
                    item.contains("exitCode") &&
                    item["exitCode"]
                        .is_number_integer()
                ) {
                    exit_code =
                        item["exitCode"].get<int>();

                    has_exit_code = true;
                }
            }

            const std::string output =
                !aggregated_output.empty()
                    ? aggregated_output
                    : streamed_output;

            std::string label =
                "Shell command";

            if (has_exit_code) {
                label +=
                    " (exit " +
                    std::to_string(exit_code) +
                    ")";
            }

            if (
                !is_current &&
                (!has_exit_code || exit_code == 0)
            ) {
                completed_unseen_threads_.insert(
                    completed.thread_id);
            }

            if (!has_exit_code || exit_code == 0) {
                play_task_completion_sound();
            }

            if (is_current) {
                append_transcript(
                    label +
                    ":\n" +
                    (
                        output.empty()
                            ? "(No output.)"
                            : compact_activity_text(
                                output)
                    ));

                if (
                    has_exit_code &&
                    exit_code != 0
                ) {
                    status_label_.set_text(
                        "Codex: shell command exited " +
                        std::to_string(exit_code));
                } else {
                    status_label_.set_text(
                        "Codex: connected");
                }

                prompt_.grab_focus();
            }

            if (is_current) {
                session->transcript_buffer =
                    transcript_.get_buffer();
            }

            session->pending_display.clear();
            upsert_session_in_thread_catalog(
                session->thread_id,
                session->cwd,
                session->transcript_input);

            std::cout
                << "PASS: GTK completed thread/shellCommand for "
                << completed.thread_id;

            if (has_exit_code) {
                std::cout
                    << " with exit code "
                    << exit_code;
            }

            std::cout << '\n';
        }

        save_ui_state();
        refresh_sidebar_threads();
    }

    void handle_turn_finished() {
        std::deque<CompletedTurn> completed_turns;

        {
            std::lock_guard<std::mutex> lock(
                turn_result_mutex_);

            completed_turns.swap(
                pending_turn_results_);
        }

        handle_turn_events();

        for (CompletedTurn& completed : completed_turns) {
            ThreadTurnSession* session =
                find_turn_session(
                    completed.thread_id);

            if (session == nullptr) {
                continue;
            }

            if (session->worker.joinable()) {
                session->worker.join();
            }

            AppServerClient::TurnResult& result =
                completed.result;

            const bool safe_pause_requested =
                pause_requested_threads_.erase(
                    completed.thread_id) > 0;

            const bool is_current =
                completed.thread_id ==
                current_thread_id_;

            completed_unseen_threads_.erase(
                completed.thread_id);

            if (
                !result.streamed_text.empty() &&
                session->agent_text !=
                    result.streamed_text
            ) {
                session->agent_text =
                    result.streamed_text;

                const auto agent_entry =
                    std::find_if(
                        session->live_entries.begin(),
                        session->live_entries.end(),
                        [](const LiveTurnEntry& entry) {
                            return entry.kind ==
                                LiveEntryKind::AgentMessage;
                        });

                if (
                    agent_entry ==
                    session->live_entries.end()
                ) {
                    ensure_live_entry(
                        *session,
                        LiveEntryKind::AgentMessage,
                        {})
                        .text = result.streamed_text;
                } else {
                    const auto another_agent =
                        std::find_if(
                            std::next(agent_entry),
                            session->live_entries.end(),
                            [](const LiveTurnEntry& entry) {
                                return entry.kind ==
                                    LiveEntryKind::AgentMessage;
                            });

                    if (
                        another_agent ==
                        session->live_entries.end()
                    ) {
                        agent_entry->text =
                            result.streamed_text;
                    }
                }

            }

            bool flushed_live_text = false;

            for (LiveTurnEntry& entry : session->live_entries) {
                if (!live_stream_text_entry(entry)) {
                    continue;
                }

                entry.stream_complete = true;

                if (entry.displayed_text != entry.text) {
                    entry.displayed_text = entry.text;
                    flushed_live_text = true;
                }
            }

            if (flushed_live_text) {
                render_live_turn(*session);
            }

            const SessionWorkKind completed_work_kind =
                session->work_kind;

            session->busy = false;
            session->work_kind =
                SessionWorkKind::None;
            session->active_turn_id.clear();
            session->stop_requested = false;
            session->interrupt_sent = false;

            for (
                LiveTurnEntry& entry :
                session->live_entries
            ) {
                if (
                    entry.kind ==
                        LiveEntryKind::FollowUp &&
                    entry.state == "queued"
                ) {
                    entry.state = "rejected";
                    entry.output =
                        "The turn ended before this follow-up could be sent.";
                }
            }

            session->pending_follow_ups.clear();
            session->follow_up_request_entries.clear();

            if (is_current) {
                set_turn_busy(false);
            }

            if (!result.success) {
                session->failed = true;
                finalize_live_activities(
                    *session,
                    "failed");

                if (is_current) {
                    status_label_.set_text(
                        completed_work_kind ==
                            SessionWorkKind::Compaction
                            ? "Codex: compaction failed"
                            : "Codex: turn transport failed");

                    append_transcript(
                        (
                            completed_work_kind ==
                                SessionWorkKind::Compaction
                                ? std::string{
                                    "Codex compaction error:\n"}
                                : std::string{
                                    "Codex transport error:\n"}
                        ) +
                        result.error);

                    current_thread_turn_failed_ =
                        true;
                }

                std::cerr
                    << "FAIL: GTK turn transport for "
                    << completed.thread_id
                    << ": "
                    << result.error
                    << '\n';

                if (is_current) {
                    update_pause_button();
                }

                continue;
            }

            upsert_session_in_thread_catalog(
                session->thread_id,
                session->cwd,
                session->transcript_input);

            if (result.status == "completed") {
                if (safe_pause_requested) {
                    paused_threads_.insert(
                        completed.thread_id);
                }

                play_task_completion_sound();

                if (!is_current) {
                    completed_unseen_threads_.insert(
                        completed.thread_id);
                }

                bool command_auto_copied = false;

                if (
                    is_current &&
                    thread_auto_copy_selections_.find(
                        completed.thread_id) !=
                        thread_auto_copy_selections_.end()
                ) {
                    const std::string command =
                        last_shell_code_block(
                            !result.streamed_text.empty()
                                ? result.streamed_text
                                : session->agent_text);

                    command_auto_copied =
                        copy_text_to_clipboard(command);
                }

                finalize_live_activities(
                    *session,
                    "completed");

                bool generated_title_saved = true;

                if (
                    completed_work_kind ==
                    SessionWorkKind::SummarizeAndTitle
                ) {
                    const std::string title =
                        generated_thread_title(
                            !result.streamed_text.empty()
                                ? result.streamed_text
                                : session->agent_text);

                    generated_title_saved =
                        !title.empty();

                    if (generated_title_saved) {
                        thread_labels_[
                            completed.thread_id] = title;

                        std::cout
                            << "PASS: saved generated display title for thread "
                            << completed.thread_id
                            << '\n';
                    }
                }

                if (is_current) {
                    status_label_.set_text(
                        safe_pause_requested
                            ? "Codex: paused at a safe checkpoint"
                            : generated_title_saved
                            ? (
                                command_auto_copied
                                    ? "Codex: connected · command copied"
                                    : "Codex: connected"
                            )
                            : "Codex: summary completed without a title");

                    if (!generated_title_saved) {
                        append_transcript(
                            "ThreadDeck could not find the required "
                            "THREADDECK_TITLE line, so the display "
                            "label was not changed.");
                    }

                    const bool has_agent_text =
                        !session->agent_text.empty() ||
                        std::any_of(
                            session->live_entries.begin(),
                            session->live_entries.end(),
                            [](const LiveTurnEntry& entry) {
                                return
                                    entry.kind ==
                                        LiveEntryKind::AgentMessage &&
                                    !entry.text.empty();
                            });

                    if (
                        completed_work_kind !=
                            SessionWorkKind::Compaction &&
                        !has_agent_text
                    ) {
                        append_transcript(
                            "Codex:\n"
                            "(Turn completed without text.)");
                    }
                }

            } else if (result.status == "failed") {
                session->failed = true;
                finalize_live_activities(
                    *session,
                    "failed");

                if (is_current) {
                    current_thread_turn_failed_ =
                        true;
                    status_label_.set_text(
                        completed_work_kind ==
                            SessionWorkKind::Compaction
                            ? "Codex: compaction failed"
                            : "Codex: turn failed");

                    std::string turn_error =
                        result.turn_error.empty()
                            ? "The turn failed without an error message."
                            : result.turn_error;

                    const bool context_limit_error =
                        turn_error.find(
                            "context window") !=
                            std::string::npos ||
                        turn_error.find(
                            "context_length_exceeded") !=
                            std::string::npos;

                    if (context_limit_error) {
                        if (
                            completed_work_kind ==
                            SessionWorkKind::Compaction
                        ) {
                            turn_error +=
                                "\n\nCodex could not compact this "
                                "thread because it is already too "
                                "large. Start a new thread to "
                                "continue; retrying /compact on "
                                "this thread will fail again.";
                        } else {
                            turn_error +=
                                "\n\nType /compact to summarize "
                                "earlier history, then retry your "
                                "message.";
                        }
                    }

                    append_transcript(
                        completed_work_kind ==
                            SessionWorkKind::Compaction
                            ? "Codex compaction error:\n" +
                                turn_error
                            : "Codex error:\n" +
                                turn_error);
                }

            } else if (
                result.status == "interrupted"
            ) {
                finalize_live_activities(
                    *session,
                    "interrupted");

                if (is_current) {
                    status_label_.set_text(
                        completed_work_kind ==
                            SessionWorkKind::Compaction
                            ? "Codex: compaction interrupted"
                            : "Codex: turn interrupted");
                    append_transcript(
                        completed_work_kind ==
                            SessionWorkKind::Compaction
                            ? "Codex context compaction interrupted."
                            : "Codex turn interrupted.");
                }

            } else {
                finalize_live_activities(
                    *session,
                    "finished");

                if (is_current) {
                    status_label_.set_text(
                        "Codex: unexpected turn status");
                    append_transcript(
                        "Codex returned unexpected turn status:\n" +
                        result.status);
                }
            }

            if (is_current) {
                session->transcript_buffer =
                    transcript_.get_buffer();
                update_pause_button();
            }

            std::cout
                << "PASS: GTK handled Codex turn "
                << result.turn_id
                << " for thread "
                << completed.thread_id
                << " with status "
                << result.status
                << '\n';
        }

        save_ui_state();
        refresh_sidebar_threads();
    }

    void play_task_completion_sound() {
        std::vector<std::filesystem::path> candidates;
        std::error_code executable_error;
        const auto executable =
            std::filesystem::canonical(
                "/proc/self/exe",
                executable_error);

        if (!executable_error) {
            const auto executable_directory =
                executable.parent_path();
            candidates.push_back(
                executable_directory /
                "assets/sounds/task-complete.flac");
            candidates.push_back(
                executable_directory.parent_path() /
                "assets/sounds/task-complete.flac");
        }

        candidates.push_back(
            std::filesystem::path(
                "assets/sounds/task-complete.flac"));
        candidates.push_back(
            std::filesystem::path(
                "/usr/local/share/threaddeck/sounds/task-complete.flac"));
        candidates.push_back(
            std::filesystem::path(
                "/usr/share/threaddeck/sounds/task-complete.flac"));

        std::filesystem::path sound_path;

        for (const auto& candidate : candidates) {
            std::error_code exists_error;

            if (
                std::filesystem::is_regular_file(
                    candidate,
                    exists_error) &&
                !exists_error
            ) {
                sound_path = candidate;
                break;
            }
        }

        if (sound_path.empty()) {
            std::cerr
                << "WARN: task completion sound is unavailable\n";
            return;
        }

        std::filesystem::path player;
        bool canberra_arguments = false;

        for (
            const auto& candidate :
            {
                std::filesystem::path{"/usr/bin/paplay"},
                std::filesystem::path{"/usr/bin/pw-play"},
                std::filesystem::path{
                    "/usr/bin/canberra-gtk-play"},
            }
        ) {
            std::error_code player_error;

            if (
                std::filesystem::is_regular_file(
                    candidate,
                    player_error) &&
                !player_error
            ) {
                player = candidate;
                canberra_arguments =
                    candidate.filename() ==
                    "canberra-gtk-play";
                break;
            }
        }

        if (player.empty()) {
            std::cerr
                << "WARN: no supported task completion sound player is available\n";
            return;
        }

        std::string player_argument =
            canberra_arguments
                ? std::string("--file=") +
                    sound_path.string()
                : sound_path.string();
        std::string player_path = player.string();
        gchar* arguments[] = {
            player_path.data(),
            player_argument.data(),
            nullptr,
        };
        GPid child_pid = 0;
        GError* error = nullptr;

        const gboolean started = g_spawn_async(
            nullptr,
            arguments,
            nullptr,
            static_cast<GSpawnFlags>(
                G_SPAWN_DO_NOT_REAP_CHILD),
            nullptr,
            nullptr,
            &child_pid,
            &error);

        if (!started) {
            std::cerr
                << "WARN: could not play task completion sound: "
                << (
                    error != nullptr
                        ? error->message
                        : "unknown player error")
                << '\n';

            if (error != nullptr) {
                g_error_free(error);
            }

            return;
        }

        g_child_watch_add(
            child_pid,
            [](GPid pid, gint, gpointer) {
                g_spawn_close_pid(pid);
            },
            nullptr);
    }

    void select_folder() {
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
                dialog.get_filename(),
                true);

            current_thread_id_.clear();
            last_active_thread_id_.clear();
            last_active_thread_cwd_.clear();

            set_turn_busy(false);
            set_shell_command_busy(false);
            clear_active_thread_surfaces();

            attach_thread_transcript_buffer(
                create_thread_transcript_buffer());

            transcript_.get_buffer()->set_text(
                "Project folder selected.\n\n"
                "Choose an existing thread from the "
                "sidebar or click New Thread.");

            status_label_.set_text(
                "Codex: connected");

            save_ui_state();
            update_send_button_state();
            refresh_sidebar_threads();

            const bool create_initial_thread =
                app_server_.is_running() &&
                !selected_project_id_.empty() &&
                !project_has_threads(
                    selected_project_id_);

            std::cout
                << "PASS: GTK added project folder "
                << selected_folder_path_
                << " without changing it on disk\n";

            if (create_initial_thread) {
                create_thread_for_selected_folder();
            }
        }
    }

    Gtk::HeaderBar header_;
    Gtk::Image shield_image_;
    Gtk::Box root_;
    Gtk::Paned body_;
    Gtk::Paned main_workspace_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Paned workspace_;
    Gtk::Box sidebar_;
    Gtk::Box content_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box composer_area_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box attachment_row_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Box composer_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Box session_controls_{Gtk::ORIENTATION_HORIZONTAL};

    ThreadHeader thread_header_;
    ContextPanel context_panel_;

    Gtk::Image hamburger_image_;
    Gtk::Image sidebar_image_;
    Gtk::Image folder_image_;
    Gtk::Image new_thread_image_;
    Gtk::Image context_image_;
    Gtk::Image attachment_image_;
    Gtk::Image audio_attachment_image_;
    Gtk::Image send_image_;
    Gtk::Image transcript_bottom_image_;
    Gtk::Image auto_copy_image_;
    Gtk::Image pause_image_;
    DoubleShieldIcon remote_shield_icon_;

    Gtk::MenuButton hamburger_button_;
    Gtk::ToggleButton sidebar_toggle_button_;
    Gtk::Button folder_button_;
    Gtk::Button new_thread_button_;
    Gtk::ToggleButton shield_button_;
    Gtk::ToggleButton auto_copy_button_;
    Gtk::Button pause_button_;
    Gtk::ToggleButton remote_shield_button_;
    Gtk::ToggleButton context_toggle_button_;
    Gtk::Button attachment_button_;
    Gtk::Button audio_attachment_button_;
    Gtk::Button clear_attachments_button_;
    Gtk::Button continue_button_;
    Gtk::Button send_button_;
    Gtk::Button transcript_bottom_button_;
    Gtk::ComboBoxText theme_selector_;
    Gtk::ComboBoxText model_selector_;
    Gtk::ComboBoxText effort_selector_;
    Gtk::ComboBoxText mode_selector_;
    Gtk::ComboBoxText access_selector_;
    SettingsWindow settings_window_{theme_selector_};
    AgentsEditorWindow agents_editor_window_;

    Glib::RefPtr<Gio::Menu> app_menu_model_;

    Gtk::Label model_label_;
    Gtk::Label effort_label_;
    Gtk::Label mode_label_;
    Gtk::Label access_label_;
    Gtk::Label usage_label_;
    Gtk::Label selected_folder_;
    Gtk::Label status_label_;
    Gtk::Label sidebar_title_;
    Gtk::Label remote_hosts_title_;

    Gtk::Box remote_hosts_panel_{
        Gtk::ORIENTATION_VERTICAL};
    Gtk::Box remote_hosts_header_{
        Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Button remote_hosts_add_button_;
    Gtk::Button remote_hosts_close_button_;
    Gtk::ScrolledWindow remote_hosts_scroll_;
    Gtk::Box remote_hosts_list_{
        Gtk::ORIENTATION_VERTICAL};

    Gtk::ScrolledWindow sidebar_scroll_;
    Gtk::Box sidebar_search_row_{
        Gtk::ORIENTATION_HORIZONTAL};
    Gtk::SearchEntry sidebar_search_;
    Gtk::Button sidebar_sort_button_;
    Gtk::Image sidebar_sort_image_;
    Gtk::Box sidebar_list_{
        Gtk::ORIENTATION_VERTICAL};

    Gtk::ScrolledWindow transcript_scroll_;
    Gtk::Overlay transcript_overlay_;
    Gtk::ScrolledWindow prompt_scroll_;
    Gtk::ScrolledWindow attachment_preview_scroll_;
    Gtk::Box attachment_previews_{
        Gtk::ORIENTATION_HORIZONTAL};
    Gtk::TextView transcript_;
    Gtk::TextView prompt_;
    Gtk::Popover skill_popover_;
    Gtk::Box skill_suggestions_{
        Gtk::ORIENTATION_VERTICAL};

    std::string selected_folder_path_;
    std::string selected_project_id_;
    std::string current_thread_id_;
    std::string last_active_thread_id_;
    std::string last_active_thread_cwd_;
    std::string current_thread_default_label_;
    std::string sidebar_project_sort_{
        "updated-desc"};
    nlohmann::json current_thread_data_ =
        nlohmann::json::object();

    nlohmann::json model_catalog_ =
        nlohmann::json::array();
    nlohmann::json account_rate_limits_ =
        nlohmann::json::object();
    nlohmann::json account_usage_ =
        nlohmann::json::object();
    nlohmann::json thread_token_usage_ =
        nlohmann::json::object();

    std::string effective_model_;
    std::string effective_reasoning_effort_;
    std::string effective_mode_{"default"};
    nlohmann::json effective_approval_policy_;
    nlohmann::json effective_sandbox_policy_;
    nlohmann::json configured_approval_policy_;
    nlohmann::json configured_sandbox_policy_;

    std::string theme_id_{"system"};
    bool splunk_environment_managed_{false};
    std::string splunk_host_;
    std::string splunk_token_;
    std::size_t codex_environment_generation_{1};

    static constexpr int kMinimumPaneWidth = 48;

    bool session_controls_updating_{false};
    bool pane_position_updating_{false};
    bool pane_position_tracking_ready_{false};
    bool sidebar_visible_{true};
    int sidebar_width_{260};
    bool context_panel_visible_{true};
    int context_panel_width_{320};
    bool turn_in_progress_{false};
    bool shell_command_in_progress_{false};
    bool current_thread_turn_failed_{false};
    bool shield_enabled_{false};
    bool shield_operation_running_{false};
    bool shield_button_updating_{false};
    bool auto_copy_button_updating_{false};
    bool remote_shield_button_updating_{false};
    bool remote_hosts_panel_visible_{false};
    std::string shield_operation_thread_id_;

    std::vector<std::string>
        selected_project_folders_;

    std::map<std::string, std::string>
        project_paths_;

    std::set<std::string>
        collapsed_project_folders_;

    std::vector<nlohmann::json>
        skill_catalog_;
    std::string skill_catalog_cwd_;
    std::map<
        std::string,
        std::vector<nlohmann::json>
    > skill_catalog_by_cwd_;

    std::vector<std::string>
        attached_image_paths_;
    std::vector<std::string>
        attached_audio_paths_;
    std::vector<std::string>
        queued_temporary_attachment_paths_;
    std::filesystem::path
        clipboard_attachment_directory_;

    std::map<std::string, std::string>
        folder_labels_;

    std::map<std::string, std::string>
        thread_labels_;

    std::map<std::string, std::string>
        project_thread_sorts_;

    std::map<
        std::string,
        std::vector<nlohmann::json>
    > thread_catalog_;

    std::map<std::string, std::string>
        thread_access_selections_;

    std::map<std::string, std::string>
        thread_model_selections_;

    std::map<std::string, std::string>
        thread_reasoning_selections_;

    std::map<std::string, std::string>
        thread_project_assignments_;

    std::map<std::string, nlohmann::json>
        moved_thread_summaries_;

    std::set<std::string>
        thread_shield_selections_;

    std::set<std::string>
        thread_auto_copy_selections_;

    std::map<std::string, std::string>
        remote_host_labels_;

    std::set<std::string>
        remote_host_credential_saved_;

    std::map<std::string, std::set<std::string>>
        thread_remote_shield_hosts_;

    std::map<std::string, std::set<std::string>>
        thread_observed_remote_hosts_;

    std::set<std::string>
        paused_threads_;

    std::set<std::string>
        pause_requested_threads_;

    std::set<std::string>
        completed_unseen_threads_;

    std::map<std::string, nlohmann::json>
        thread_configured_approval_policies_;

    std::map<std::string, nlohmann::json>
        thread_configured_sandbox_policies_;

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
    bool main_window_state_save_pending_{false};
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

    Glib::Dispatcher turn_dispatcher_;
    Glib::Dispatcher turn_event_dispatcher_;
    Glib::Dispatcher thread_activation_dispatcher_;
    Glib::Dispatcher thread_move_dispatcher_;
    Glib::Dispatcher skill_load_dispatcher_;
    Glib::Dispatcher shield_dispatcher_;
    Glib::Dispatcher shell_command_dispatcher_;
    Glib::Dispatcher approval_dispatcher_;
    Glib::Dispatcher thread_search_dispatcher_;
    std::mutex shell_command_result_mutex_;
    std::deque<CompletedShellCommand>
        pending_shell_command_results_;

    std::mutex turn_result_mutex_;
    std::deque<CompletedTurn>
        pending_turn_results_;

    std::mutex turn_event_mutex_;
    std::deque<AppServerClient::TurnEvent>
        pending_turn_events_;

    std::mutex thread_activation_result_mutex_;
    std::deque<CompletedThreadActivation>
        pending_thread_activations_;

    std::thread thread_move_worker_;
    std::mutex thread_move_result_mutex_;
    std::deque<CompletedThreadMove>
        pending_thread_moves_;
    std::string moving_thread_id_;

    std::mutex skill_load_result_mutex_;
    std::deque<CompletedSkillLoad>
        pending_skill_load_results_;
    std::map<std::string, std::thread>
        skill_loaders_;

    std::thread shield_worker_;
    std::mutex shield_result_mutex_;
    std::deque<CompletedShieldOperation>
        pending_shield_results_;

    std::mutex approval_mutex_;
    std::deque<
        std::shared_ptr<PendingApprovalState>
    > pending_approvals_;
    bool approval_dialog_open_{false};
    bool approval_blink_on_{false};
    std::vector<Gtk::Widget*>
        approval_question_rows_;
    bool shutting_down_{false};

    std::thread thread_search_worker_;
    std::mutex thread_search_mutex_;
    std::condition_variable thread_search_condition_;
    ThreadSearchRequest thread_search_request_;
    std::deque<CompletedThreadSearch>
        thread_search_completed_;
    std::size_t thread_search_generation_{0};
    std::size_t thread_search_latest_generation_{0};
    bool thread_search_has_request_{false};
    bool thread_search_stop_{false};
    bool thread_search_loading_{false};
    std::vector<nlohmann::json>
        thread_search_results_;
    std::string thread_search_result_term_;
    std::string thread_search_error_;

    std::string active_turn_id_;
    bool stop_requested_{false};

    std::map<
        std::string,
        std::unique_ptr<ThreadTurnSession>
    > turn_sessions_;

    std::vector<TranscriptCopyButton>
        transcript_copy_buttons_;
    std::map<std::string, CodeCopyPayload>
        code_copy_payloads_;
    std::size_t code_copy_marker_sequence_{0};

    Glib::RefPtr<Gdk::Cursor>
        transcript_arrow_cursor_;
    Glib::RefPtr<Gdk::Cursor>
        transcript_hand_cursor_;

    Glib::RefPtr<Gtk::TextBuffer::Mark>
        transcript_end_mark_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_user_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_codex_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_commentary_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_reasoning_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_activity_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_error_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_section_heading_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_user_section_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_user_marker_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_user_top_padding_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_user_bottom_padding_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_expand_activity_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_expand_token_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_code_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_code_header_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_code_copy_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_markdown_marker_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_markdown_heading_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_markdown_bold_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_markdown_inline_code_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_markdown_quote_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_markdown_list_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_markdown_link_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_code_keyword_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_code_string_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_code_comment_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_code_number_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_diff_add_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_diff_delete_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_diff_header_tag_;
    Glib::RefPtr<Gtk::TextTag>
        transcript_command_tag_;
    Glib::RefPtr<Gtk::TextTag>
        prompt_pasted_tag_;
    bool transcript_follow_output_{true};
    bool transcript_scroll_pending_{false};
    bool transcript_scroll_programmatic_{false};
    double transcript_last_scroll_value_{0.0};
    double transcript_last_scroll_upper_{0.0};
    std::size_t follow_up_sequence_{0};
    std::size_t transcript_image_marker_sequence_{0};
    std::map<
        std::string,
        ActivityExpansionPayload
    >
        activity_expansion_payloads_;
    std::map<std::string, std::string>
        activity_expansion_tokens_;
    std::set<std::string>
        expanded_activity_ids_;
    std::size_t activity_expansion_sequence_{0};
    bool pasting_prompt_text_{false};
    bool prompt_history_restoring_{false};
    int prompt_history_transaction_depth_{0};
    PromptEditSnapshot prompt_history_current_;
    PromptEditSnapshot prompt_history_transaction_start_;
    std::vector<PromptEditSnapshot>
        prompt_undo_history_;
    std::vector<PromptEditSnapshot>
        prompt_redo_history_;
    std::string composer_thread_id_;
    std::map<std::string, ComposerDraft>
        composer_drafts_;
    std::map<
        std::string,
        std::vector<Glib::ustring>
    > prompt_command_histories_;
    std::set<std::string>
        prompt_history_seeded_threads_;
    std::map<
        std::string,
        PromptCommandHistoryNavigation
    > prompt_command_history_navigation_;
    sigc::connection sidebar_search_connection_;
    sigc::connection approval_blink_connection_;
    sigc::connection live_text_reveal_connection_;
};

namespace {

void load_default_window_icon(const char* argv0) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;

    if (argv0 != nullptr) {
        std::error_code exe_ec;
        const auto exe_dir =
            fs::weakly_canonical(fs::path(argv0), exe_ec)
                .parent_path();
        if (!exe_ec) {
            candidates.push_back(
                exe_dir / "assets" / "icons" /
                "threaddeck.svg");
            candidates.push_back(
                exe_dir.parent_path() / "assets" /
                "icons" / "threaddeck.svg");
        }
    }
    candidates.push_back(
        fs::path("assets/icons/threaddeck.svg"));

    for (const auto& candidate : candidates) {
        std::error_code exists_ec;
        if (!fs::exists(candidate, exists_ec)) {
            continue;
        }
        try {
            Gtk::Window::set_default_icon_from_file(
                candidate.string());
            return;
        } catch (const Glib::Error&) {
            // Fall through and try the next candidate.
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto application = Gtk::Application::create(
        argc,
        argv,
        "com.ronpatrick.ThreadDeck");

    load_default_window_icon(argc > 0 ? argv[0] : nullptr);

    MainWindow window;

    const auto settings_action =
        Gio::SimpleAction::create("settings");

    settings_action->signal_activate().connect(
        [&window](const Glib::VariantBase&) {
            window.show_settings();
        });

    application->add_action(settings_action);

    const auto project_instructions_action =
        Gio::SimpleAction::create(
            "project-instructions");

    project_instructions_action
        ->signal_activate()
        .connect(
            [&window](const Glib::VariantBase&) {
                window.show_project_instructions();
            });

    application->add_action(
        project_instructions_action);

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
