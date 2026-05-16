#include "FileDialogs.hpp"

#ifndef ATLAS_PLATFORM_ANDROID
#include <GLFW/glfw3.h>

#ifdef ATLAS_PLATFORM_DESKTOP
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <commdlg.h>

#endif

#ifdef ATLAS_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#include <gtk/gtk.h>
#endif

#include <GLFW/glfw3native.h>
#endif // ATLAS_PLATFORM_ANDROID

#include <stdexcept>
#include <string>

#ifdef ATLAS_PLATFORM_ANDROID
// On Android provide stubs so this file can be compiled but the desktop dialogs are not used.
std::string FileDialogs::openFile(const char *filter) {
    (void)filter;
    return {};
}

std::string FileDialogs::saveFile(const char *filter) {
    (void)filter;
    return {};
}

#else

std::string FileDialogs::openFile(const char *filter) {
    auto glfwWindow = glfwGetCurrentContext();
    if (glfwWindow == nullptr) {
        throw std::runtime_error("No glfw window in context, create a window before running this.");
    }

#ifdef _WIN32
    // Windows code
    OPENFILENAMEA ofn;
    CHAR szFile[260] = {0};
    CHAR currentDir[256] = {0};
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = glfwGetWin32Window(glfwWindow);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    if (GetCurrentDirectoryA(256, currentDir)) {
        ofn.lpstrInitialDir = currentDir;
    }
    ofn.lpstrFilter = filter; // https://learn.microsoft.com/en-us/dotnet/api/microsoft.win32.filedialog.filter?view=windowsdesktop-8.0
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE)
        return ofn.lpstrFile;

#endif // _WIN32

#ifdef __linux__
    // Linux code (GTK)
    if (!gtk_init_check(nullptr, nullptr)) {
        throw std::runtime_error("Failed to initialize GTK.");
    }

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Open File",
        GTK_WINDOW(glfwGetWindowUserPointer(glfwWindow)),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        nullptr);

    // Set the file filter (if provided)
    if (filter != nullptr) {
        GtkFileFilter* fileFilter = gtk_file_filter_new();
        gtk_file_filter_set_name(fileFilter, "File Filter");
        gtk_file_filter_add_pattern(fileFilter, filter);
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), fileFilter);
    }

    int result = gtk_dialog_run(GTK_DIALOG(dialog));
    std::string filePath;

    if (result == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        filePath = filename;
        g_free(filename);
    }

    gtk_widget_destroy(dialog);

    return filePath;
#endif // __linux__

    return {};
}

std::string FileDialogs::saveFile(const char *filter) {
    auto glfwWindow = glfwGetCurrentContext();
    if (glfwWindow == nullptr) {
        throw std::runtime_error("No glfw window in context, create a window before running this.");
    }

#ifdef _WIN32
    OPENFILENAMEA ofn;
    CHAR szFile[260] = {0};
    CHAR currentDir[256] = {0};
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = glfwGetWin32Window(glfwWindow);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    if (GetCurrentDirectoryA(256, currentDir)) {
        ofn.lpstrInitialDir = currentDir;
    }
    ofn.lpstrFilter = filter; // https://learn.microsoft.com/en-us/dotnet/api/microsoft.win32.filedialog.filter?view=windowsdesktop-8.0
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    ofn.lpstrDefExt = strchr(filter, '\0') + 1;

    if (GetSaveFileNameA(&ofn) == TRUE)
        return ofn.lpstrFile;

#endif // _WIN32

#ifdef __linux__
    // Linux code (GTK)
        if (!gtk_init_check(nullptr, nullptr)) {
            throw std::runtime_error("Failed to initialize GTK.");
        }

        GtkWidget* dialog = gtk_file_chooser_dialog_new(
            "Save File",
            GTK_WINDOW(glfwGetWindowUserPointer(glfwWindow)),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL,
            "_Save", GTK_RESPONSE_ACCEPT,
            nullptr);

        // Set the file filter (if provided)
        if (filter != nullptr) {
            GtkFileFilter* fileFilter = gtk_file_filter_new();
            gtk_file_filter_set_name(fileFilter, "File Filter");
            gtk_file_filter_add_pattern(fileFilter, filter);
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), fileFilter);
        }

        int result = gtk_dialog_run(GTK_DIALOG(dialog));
        std::string filePath;

        if (result == GTK_RESPONSE_ACCEPT) {
            char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            filePath = filename;
            g_free(filename);
        }

        gtk_widget_destroy(dialog);

        return filePath;
#endif // __linux__

    return {};
}

#endif // __ANDROID__
