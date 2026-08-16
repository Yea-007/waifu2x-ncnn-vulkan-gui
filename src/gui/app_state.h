#ifndef GUI_APP_STATE_H
#define GUI_APP_STATE_H

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include "filesystem_utils.h"

struct AppState
{
    // --- Settings ---
    int noise = 0;                   // -1..3
    int scale = 2;                   // 1,2,4,8,16,32
    int tilesize = 0;                // 0=auto, >=32=manual
    std::string model_dir = "models-cunet";
    int gpu_id = 0;                  // -1=cpu, 0..N
    bool tta_mode = false;
    int jobs_load = 1;
    int jobs_proc = 2;
    int jobs_save = 2;
    std::string output_format = "png";
    std::string output_dir_utf8;     // UTF-8 copy of output_dir (for the input field)

    // --- File list ---
    std::vector<path_t> input_files;
    std::vector<path_t> output_files;
    path_t output_dir;
    int selected_file_index = -1;

    // --- Processing state ---
    enum class Status { Idle, Processing, Completed, Cancelled, Error };
    std::atomic<Status> status{Status::Idle};
    std::atomic<int> processed_count{0};
    std::atomic<int> total_count{0};
    std::atomic<bool> cancel_requested{false};
    std::string error_message;

    // --- File status tracking ---
    enum class FileStatus { Pending, Processing, Completed, Failed };
    std::vector<FileStatus> file_statuses;
    int current_file_index = -1;

    // --- Preview data ---
    bool preview_available = false;
    unsigned char* preview_input_data = nullptr;
    unsigned char* preview_output_data = nullptr;
    int preview_input_w = 0;
    int preview_input_h = 0;
    int preview_input_c = 0;
    int preview_output_w = 0;
    int preview_output_h = 0;
    int preview_output_c = 0;
    std::mutex preview_mutex;
    unsigned int preview_input_tex = 0;
    unsigned int preview_output_tex = 0;

    // --- GPU info ---
    int gpu_count = 0;
    std::vector<std::string> gpu_names;

    // --- Settings persistence path ---
    path_t settings_path;

    // --- Output path selection method ---
    bool output_is_dir = true;

    void reset_files()
    {
        input_files.clear();
        output_files.clear();
        file_statuses.clear();
        current_file_index = -1;
        selected_file_index = -1;
        free_preview_data();
        preview_available = false;
        status = Status::Idle;
        processed_count = 0;
        total_count = 0;
        cancel_requested = false;
        error_message.clear();
    }

    void free_preview_data()
    {
        if (preview_input_data) { free(preview_input_data); preview_input_data = nullptr; }
        if (preview_output_data) { free(preview_output_data); preview_output_data = nullptr; }
        preview_available = false;
    }
};

#endif // GUI_APP_STATE_H
