#pragma once

// A small file browser for Save / Load / Record, drawn with ImGui (no
// platform dialog library): the folder, its subfolders and matching files, a
// name to type, and a check before overwriting.

#include <string>
#include <vector>

namespace afx
{
class FileDialog
{
public:
    enum Mode { open, save };

    // extension: e.g. ".json", or several separated by spaces (".mp3 .wav .flac"): the files
    // shown, and when saving the first is added if missing.
    void show (const char* title, Mode, const std::string& folder, const std::string& name, const std::string& extension);
    // Draws it when shown; true once, with the chosen path, when the user confirms.
    bool draw (std::string& path);
    bool isOpen() const { return visible; }

private:
    void list();

    std::string title, folder, name, extension, error;
    Mode mode = open;
    bool visible = false, justOpened = false, confirmOverwrite = false;
    struct Entry
    {
        std::string name;
        bool folder;
    };
    std::vector<Entry> entries;
};
} // namespace afx
