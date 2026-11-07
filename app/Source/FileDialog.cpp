#include "FileDialog.h"

#include "Views.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace afx
{
namespace
{
bool endsWith (const std::string& s, const std::string& end)
{
    return s.size() >= end.size() && s.compare (s.size() - end.size(), end.size(), end) == 0;
}

// The extensions, separated by spaces (".mp3 .wav .flac"); matched without regard to case.
std::vector<std::string> extensionsOf (const std::string& list)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < list.size())
    {
        const size_t j = list.find (' ', i);
        if (j != i) out.push_back (list.substr (i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return out;
}

std::string lower (std::string s)
{
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
    return s;
}

bool matches (const std::string& name, const std::string& list)
{
    const auto n = lower (name);
    for (const auto& e : extensionsOf (list))
        if (endsWith (n, lower (e))) return true;
    return false;
}
} // namespace

void FileDialog::show (const char* t, Mode m, const std::string& f, const std::string& n, const std::string& ext)
{
    title = t;
    mode = m;
    std::error_code ec;
    folder = fs::is_directory (f, ec) ? f : fs::current_path (ec).string();
    name = n;
    extension = ext;
    error.clear();
    visible = justOpened = true;
    confirmOverwrite = false;
    list();
}

void FileDialog::list()
{
    entries.clear();
    std::error_code ec;
    for (fs::directory_iterator it (folder, fs::directory_options::skip_permission_denied, ec), end; ! ec && it != end; it.increment (ec))
    {
        const auto n = it->path().filename().string();
        if (n.empty() || n[0] == '.') continue;
        std::error_code e2;
        const bool dir = it->is_directory (e2);
        if (dir || extension.empty() || matches (n, extension)) entries.push_back ({ n, dir });
    }
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) {
        return a.folder != b.folder ? a.folder : a.name < b.name;
    });
}

bool FileDialog::draw (std::string& path)
{
    if (! visible) return false;
    if (justOpened)
    {
        ImGui::OpenPopup (title.c_str());
        justOpened = false;
    }
    bool chosen = false;
    ImGui::SetNextWindowSize ({ 640, 460 }, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos (ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
    if (ImGui::BeginPopupModal (title.c_str(), &visible))
    {
        if (ImGui::Button ("Up"))
        {
            folder = fs::path (folder).parent_path().string();
            list();
        }
        ImGui::SameLine();
        if (ImGui::Button ("Home"))
            if (const char* h = std::getenv ("HOME"))
            {
                folder = h;
                list();
            }
        ImGui::SameLine();
        ImGui::SetNextItemWidth (-1);
        if (ImGui::InputText ("##folder", &folder, ImGuiInputTextFlags_EnterReturnsTrue)) list();

        auto confirm = [&] {
            std::string n = name;
            if (n.empty()) return;
            if (mode == save && ! extension.empty() && ! matches (n, extension) && ! endsWith (n, ".json"))
                n += extensionsOf (extension).front();
            const auto full = (fs::path (folder) / n).string();
            std::error_code ec;
            if (mode == open && ! fs::is_regular_file (full, ec))
            {
                error = "There is no such file.";
                return;
            }
            if (mode == save && fs::exists (full, ec) && ! confirmOverwrite)
            {
                confirmOverwrite = true;
                error = n + " exists: press Save again to replace it.";
                return;
            }
            path = full;
            chosen = true;
            visible = false;
        };

        const float footer = ImGui::GetFrameHeightWithSpacing() * 2 + 26;
        if (ImGui::BeginChild ("files", { 0, -footer }, ImGuiChildFlags_Borders))
        {
            for (const auto& e : entries)
            {
                const std::string label = (e.folder ? "[ ] " : "    ") + e.name + (e.folder ? "/" : "");
                const bool picked = ImGui::Selectable (label.c_str(), ! e.folder && e.name == name, ImGuiSelectableFlags_AllowDoubleClick);
                markItem ("dialog.file." + e.name);
                if (picked)
                {
                    if (e.folder)
                    {
                        folder = (fs::path (folder) / e.name).string();
                        list();
                        break;
                    }
                    name = e.name;
                    confirmOverwrite = false;
                    error.clear();
                    if (ImGui::IsMouseDoubleClicked (0)) confirm();
                }
            }
            if (entries.empty()) helpText (extension.empty() ? "(empty)" : "(no " + extension + " files here)");
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth (-170);
        if (ImGui::InputText ("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue)) confirm();
        markItem ("dialog.name");
        if (ImGui::IsItemEdited())
        {
            confirmOverwrite = false;
            error.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button (mode == save ? "Save" : "Open", { 76, 0 })) confirm();
        markItem ("dialog.confirm");
        ImGui::SameLine();
        if (ImGui::Button ("Cancel", { 76, 0 })) visible = false;
        if (! error.empty())
        {
            ImGui::PushStyleColor (ImGuiCol_Text, accent);
            ImGui::TextUnformatted (error.c_str());
            ImGui::PopStyleColor();
        }
        if (! visible) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    else
        visible = false;
    return chosen;
}
} // namespace afx
