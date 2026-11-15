#pragma once
// Update dialog (modelled on Tabularis' UpdateNotificationModal): shows what
// changes (version, date, release notes) and what is happening right now
// (download with size/speed/ETA, verification, install), with errors inline.
//
// The class only builds and updates controls. MainWindow drives the flow:
// it creates the dialog for the current state, feeds progress into it and
// reacts to the buttons. All members must be used on the UI thread.
#include "Localization.hpp"
#include "winchisel/core/i18n.hpp"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Text.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace winchisel::ui {

class UpdateDialog {
public:
    enum class State { Offer, Downloading, Verifying, Ready, Failed };

    struct Info {
        std::string current_version;
        std::string new_version;
        std::string published_at;   // ISO-8601, may be empty
        std::string notes;          // Markdown, may be empty
        std::string release_url;    // may be empty
        bool downgrade_to_stable{};
        bool portable{};
    };

    explicit UpdateDialog(Info info) : info_(std::move(info)) { build(); }

    winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& dialog() const { return dialog_; }
    State state() const { return state_; }

    void set_offer() {
        state_ = State::Offer;
        steps_.Visibility(Visibility::Collapsed);
        error_.IsOpen(false);
        dialog_.PrimaryButtonText(text(info_.downgrade_to_stable ? L"Switch to stable" : L"Download and install"));
        dialog_.IsPrimaryButtonEnabled(true);
        dialog_.SecondaryButtonText(L"");
        dialog_.CloseButtonText(text(L"Later"));
        dialog_.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
    }

    void set_downloading(std::uint64_t done, std::uint64_t total) {
        if (state_ != State::Downloading) {
            state_ = State::Downloading;
            started_ = std::chrono::steady_clock::now();
            start_bytes_ = done;
            steps_.Visibility(Visibility::Visible);
            error_.IsOpen(false);
            set_step(download_, StepState::Active);
            set_step(verify_, StepState::Pending);
            set_step(install_, StepState::Pending);
            dialog_.PrimaryButtonText(text(L"Downloading …"));
            dialog_.IsPrimaryButtonEnabled(false);
            // Esc/Close only hides the dialog: the download keeps running and
            // the title bar keeps showing it. Cancelling is the explicit button.
            dialog_.SecondaryButtonText(text(L"Cancel download"));
            dialog_.IsSecondaryButtonEnabled(true);
            dialog_.CloseButtonText(text(L"Hide"));
            dialog_.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::None);
        }
        if (!total) {
            bar_.IsIndeterminate(true);
            download_.detail.Text(text(L"Connecting …"));
            return;
        }
        bar_.IsIndeterminate(false);
        bar_.Value(static_cast<double>(done) * 100.0 / static_cast<double>(total));
        std::wstring line = mb(done) + L" / " + mb(total) + L" MB";
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
        const auto fresh = done > start_bytes_ ? done - start_bytes_ : 0;
        if (seconds >= 0.5 && fresh > 0) {
            const double rate = static_cast<double>(fresh) / seconds;
            line += L"  ·  " + mb(static_cast<std::uint64_t>(rate)) + L" MB/s";
            const double left = static_cast<double>(total - done) / rate;
            if (done < total) line += L"  ·  " + eta(left);
        }
        download_.detail.Text(line);
    }

    void set_verifying() {
        state_ = State::Verifying;
        steps_.Visibility(Visibility::Visible);
        bar_.IsIndeterminate(false);
        bar_.Value(100.0);
        set_step(download_, StepState::Done);
        set_step(verify_, StepState::Active);
        dialog_.IsSecondaryButtonEnabled(false);
    }

    void set_ready() {
        state_ = State::Ready;
        steps_.Visibility(Visibility::Visible);
        error_.IsOpen(false);
        bar_.Value(100.0);
        set_step(download_, StepState::Done);
        set_step(verify_, StepState::Done);
        set_step(install_, StepState::Active);
        install_.detail.Text(text(info_.portable
            ? L"Ready. Winchisel closes, replaces itself and starts the new version."
            : L"Ready. Winchisel closes and the installer updates it in place."));
        dialog_.PrimaryButtonText(text(L"Restart now"));
        dialog_.IsPrimaryButtonEnabled(true);
        dialog_.SecondaryButtonText(L"");
        dialog_.CloseButtonText(text(L"Later"));
        dialog_.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
    }

    void set_failed(std::wstring const& message) {
        const bool verifying = state_ == State::Verifying;
        state_ = State::Failed;
        set_step(verifying ? verify_ : download_, StepState::Failed);
        error_.Message(message);
        error_.IsOpen(true);
        dialog_.PrimaryButtonText(text(L"Try again"));
        dialog_.IsPrimaryButtonEnabled(true);
        dialog_.SecondaryButtonText(L"");
        dialog_.CloseButtonText(text(L"Close"));
        dialog_.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
    }

private:
    using Visibility = winrt::Microsoft::UI::Xaml::Visibility;
    using Thickness = winrt::Microsoft::UI::Xaml::Thickness;
    using UIElement = winrt::Microsoft::UI::Xaml::UIElement;
    using TextWrapping = winrt::Microsoft::UI::Xaml::TextWrapping;
    using VerticalAlignment = winrt::Microsoft::UI::Xaml::VerticalAlignment;
    enum class StepState { Pending, Active, Done, Failed };
    struct Step {
        winrt::Microsoft::UI::Xaml::Controls::Grid row{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::FontIcon icon{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ProgressRing ring{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock detail{nullptr};
    };

    static winrt::hstring text(wchar_t const* english) { return winrt::hstring{winchisel::core::loc(english)}; }

    static std::wstring mb(std::uint64_t bytes) {
        wchar_t buffer[32]{};
        std::swprintf(buffer, 32, L"%.1f", static_cast<double>(bytes) / (1024.0 * 1024.0));
        return buffer;
    }

    static std::wstring eta(double seconds) {
        if (seconds < 1.5) return text(L"almost done").c_str();
        if (seconds < 60) return L"~" + std::to_wstring(static_cast<int>(seconds + 0.5)) + L" s " + text(L"left").c_str();
        return L"~" + std::to_wstring(static_cast<int>(seconds / 60 + 0.5)) + L" min " + text(L"left").c_str();
    }

    static winrt::Microsoft::UI::Xaml::Media::Brush theme_brush(wchar_t const* key) {
        auto resources = winrt::Microsoft::UI::Xaml::Application::Current().Resources();
        return resources.Lookup(winrt::box_value(winrt::hstring{key})).as<winrt::Microsoft::UI::Xaml::Media::Brush>();
    }

    static winrt::Microsoft::UI::Xaml::Controls::TextBlock caption(wchar_t const* english) {
        winrt::Microsoft::UI::Xaml::Controls::TextBlock block;
        block.Text(text(english));
        block.FontSize(12);
        block.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        block.Foreground(theme_brush(L"TextFillColorSecondaryBrush"));
        return block;
    }

    Step make_step(wchar_t const* title, wchar_t const* detail) {
        namespace C = winrt::Microsoft::UI::Xaml::Controls;
        Step step;
        step.row = C::Grid();
        step.row.ColumnSpacing(12);
        C::ColumnDefinition icon_column;
        icon_column.Width(winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(20));
        step.row.ColumnDefinitions().Append(icon_column);
        step.row.ColumnDefinitions().Append(C::ColumnDefinition());
        step.icon = C::FontIcon();
        step.icon.FontSize(16);
        step.icon.VerticalAlignment(VerticalAlignment::Top);
        step.icon.Margin(Thickness{0, 2, 0, 0});
        step.ring = C::ProgressRing();
        step.ring.Width(16);
        step.ring.Height(16);
        step.ring.IsActive(true);
        step.ring.VerticalAlignment(VerticalAlignment::Top);
        step.ring.Margin(Thickness{0, 2, 0, 0});
        step.row.Children().Append(step.icon);
        step.row.Children().Append(step.ring);
        C::StackPanel texts;
        texts.Spacing(2);
        C::TextBlock heading;
        heading.Text(text(title));
        heading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        step.detail = C::TextBlock();
        step.detail.Text(text(detail));
        step.detail.FontSize(12);
        step.detail.TextWrapping(TextWrapping::Wrap);
        step.detail.Foreground(theme_brush(L"TextFillColorSecondaryBrush"));
        texts.Children().Append(heading);
        texts.Children().Append(step.detail);
        C::Grid::SetColumn(texts, 1);
        step.row.Children().Append(texts);
        set_step(step, StepState::Pending);
        return step;
    }

    static void set_step(Step& step, StepState state) {
        const bool active = state == StepState::Active;
        step.ring.Visibility(active ? Visibility::Visible : Visibility::Collapsed);
        step.icon.Visibility(active ? Visibility::Collapsed : Visibility::Visible);
        switch (state) {
            case StepState::Pending:
                step.icon.Glyph(L"");  // empty circle
                step.icon.Foreground(theme_brush(L"TextFillColorTertiaryBrush"));
                break;
            case StepState::Done:
                step.icon.Glyph(L"");  // completed (filled check circle)
                step.icon.Foreground(theme_brush(L"SystemFillColorSuccessBrush"));
                break;
            case StepState::Failed:
                step.icon.Glyph(L"");  // status error
                step.icon.Foreground(theme_brush(L"SystemFillColorCriticalBrush"));
                break;
            case StepState::Active:
                break;
        }
    }

    // Renders the subset of Markdown our release notes use (tools/changelog.sh):
    // headings, bullets, **bold**, `code`, [text](url) links; the collapsed
    // Maintenance block and everything after the "---" footer are skipped.
    static void render_notes(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& panel, std::string const& markdown) {
        namespace C = winrt::Microsoft::UI::Xaml::Controls;
        auto clean = [](std::string line) {
            // [text](url) -> text, and drop the "([`sha`](...))" commit suffix.
            for (std::size_t open; (open = line.find('[')) != std::string::npos;) {
                const auto close = line.find("](", open);
                if (close == std::string::npos) break;
                const auto end = line.find(')', close);
                if (end == std::string::npos) break;
                line = line.substr(0, open) + line.substr(open + 1, close - open - 1) + line.substr(end + 1);
            }
            for (auto token : {"**", "`"}) {
                for (std::size_t at; (at = line.find(token)) != std::string::npos;) line.erase(at, std::string_view(token).size());
            }
            // "(abc1234)" left over from a commit link.
            if (const auto paren = line.rfind(" ("); paren != std::string::npos && line.back() == ')' && line.size() - paren <= 12) line.resize(paren);
            while (!line.empty() && (line.back() == ' ' || line.back() == '\r')) line.pop_back();
            return line;
        };
        std::size_t start{};
        bool skipping{};
        int shown{};
        while (start <= markdown.size() && shown < 60) {
            auto end = markdown.find('\n', start);
            if (end == std::string::npos) end = markdown.size();
            std::string line = markdown.substr(start, end - start);
            start = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.starts_with("<details")) { skipping = true; continue; }
            if (line.starts_with("</details")) { skipping = false; continue; }
            if (skipping || line.empty() || line.starts_with("<")) continue;
            if (line == "---") break;
            C::TextBlock block;
            block.TextWrapping(TextWrapping::Wrap);
            block.IsTextSelectionEnabled(true);
            if (line.starts_with("#")) {
                const auto level = line.find_first_not_of('#');
                if (level == std::string::npos) continue;
                block.Text(winrt::to_hstring(clean(line.substr(level + (line.size() > level && line[level] == ' ' ? 1 : 0)))));
                block.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                block.Margin(Thickness{0, shown ? 8.0 : 0.0, 0, 2});
                if (level <= 2) block.FontSize(15);
            } else if (line.starts_with("- ") || line.starts_with("* ")) {
                block.Text(L"•  " + winrt::to_hstring(clean(line.substr(2))));
                block.Margin(Thickness{4, 0, 0, 0});
            } else {
                block.Text(winrt::to_hstring(clean(line)));
            }
            block.FontSize(block.FontSize() > 14 ? block.FontSize() : 13);
            panel.Children().Append(block);
            ++shown;
        }
        if (!shown) {
            C::TextBlock none;
            none.Text(text(L"No release notes were published for this version."));
            none.Foreground(theme_brush(L"TextFillColorSecondaryBrush"));
            panel.Children().Append(none);
        }
    }

    void build() {
        namespace C = winrt::Microsoft::UI::Xaml::Controls;
        dialog_ = C::ContentDialog();
        dialog_.Title(winrt::box_value(text(info_.downgrade_to_stable ? L"Switch back to stable" : L"Winchisel update available")));

        C::StackPanel root;
        root.Spacing(14);
        root.MinWidth(460);
        root.MaxWidth(560);

        // Version line: "v1.0.9.1  ->  v1.1.0  ·  2026-11-15"
        C::TextBlock versions;
        versions.TextWrapping(TextWrapping::Wrap);
        std::wstring line = L"v" + std::wstring(winrt::to_hstring(info_.current_version)) + L"  →  v" + std::wstring(winrt::to_hstring(info_.new_version));
        if (info_.published_at.size() >= 10) line += L"  ·  " + std::wstring(winrt::to_hstring(info_.published_at.substr(0, 10)));
        if (info_.downgrade_to_stable) line += L"  ·  " + std::wstring(text(L"stable channel"));
        versions.Text(line);
        versions.Foreground(theme_brush(L"TextFillColorSecondaryBrush"));
        root.Children().Append(versions);

        // Release notes
        C::StackPanel notes_section;
        notes_section.Spacing(6);
        notes_section.Children().Append(caption(L"What's new"));
        C::Border notes_frame;
        notes_frame.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadius{6, 6, 6, 6});
        notes_frame.BorderThickness(Thickness{1, 1, 1, 1});
        notes_frame.BorderBrush(theme_brush(L"CardStrokeColorDefaultBrush"));
        notes_frame.Background(theme_brush(L"CardBackgroundFillColorDefaultBrush"));
        C::ScrollViewer scroll;
        scroll.MaxHeight(220);
        scroll.Padding(Thickness{12, 10, 12, 10});
        C::StackPanel notes;
        notes.Spacing(2);
        render_notes(notes, info_.notes);
        scroll.Content(notes);
        notes_frame.Child(scroll);
        notes_section.Children().Append(notes_frame);
        root.Children().Append(notes_section);

        // Steps (shown once the download starts)
        steps_ = C::StackPanel();
        steps_.Spacing(10);
        steps_.Children().Append(caption(L"Progress"));
        download_ = make_step(L"Download", L"Connecting …");
        bar_ = C::ProgressBar();
        bar_.Minimum(0);
        bar_.Maximum(100);
        bar_.Margin(Thickness{32, -4, 0, 0});
        verify_ = make_step(L"Verify", L"SHA-256 checksum and signed release manifest");
        install_ = make_step(L"Install", info_.portable
            ? L"Winchisel closes, replaces itself and starts the new version"
            : L"Winchisel closes and the installer updates it in place");
        steps_.Children().Append(download_.row);
        steps_.Children().Append(bar_);
        steps_.Children().Append(verify_.row);
        steps_.Children().Append(install_.row);
        steps_.Visibility(Visibility::Collapsed);
        root.Children().Append(steps_);

        error_ = C::InfoBar();
        error_.Severity(C::InfoBarSeverity::Error);
        error_.Title(text(L"Update failed"));
        error_.IsClosable(false);
        error_.IsOpen(false);
        root.Children().Append(error_);

        if (!info_.release_url.empty()) {
            C::HyperlinkButton link;
            link.Content(winrt::box_value(text(L"View release on GitHub")));
            link.NavigateUri(winrt::Windows::Foundation::Uri(winrt::to_hstring(info_.release_url)));
            link.Padding(Thickness{0, 0, 0, 0});
            root.Children().Append(link);
        }

        dialog_.Content(root);
        set_offer();
    }

    Info info_;
    State state_{State::Offer};
    winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel steps_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ProgressBar bar_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::InfoBar error_{nullptr};
    Step download_, verify_, install_;
    std::chrono::steady_clock::time_point started_{};
    std::uint64_t start_bytes_{};
};

}  // namespace winchisel::ui
