#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

class Timer {
public:
    enum class Mode {
        Seconds,
        Milliseconds,
        Microseconds,
        Nanoseconds
    };
    Timer(const std::string& name = "time", Mode mode = Mode::Milliseconds) 
        : mode(mode), name(name) {
        start_time = std::chrono::steady_clock::now();
    }
    ~Timer() {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = end_time - start_time;


        std::filesystem::create_directories("Log/Timer");
        std::string filename = "Log/Timer/" + (name.empty() ? "timer" : name) + ".log";
        std::ofstream ofs(filename, std::ios::app);

        if (!ofs) {
            std::cerr << "Failed to open log file: " << filename << std::endl;
            return;
        }

        switch (mode) {
            case Mode::Seconds:
                ofs << name + " Elapsed time: " << std::chrono::duration_cast<std::chrono::seconds>(duration).count() << " seconds\n";
                break;
            case Mode::Milliseconds:
                ofs << name + "Elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() << " milliseconds\n";
                break;
            case Mode::Microseconds:
                ofs << name + "Elapsed time: " << std::chrono::duration_cast<std::chrono::microseconds>(duration).count() << " microseconds\n";
                break;
            case Mode::Nanoseconds:
                ofs << name + "Elapsed time: " << std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count() << " nanoseconds\n";
                break;
        }
    }
    void setMode(Mode new_mode) {
        mode = new_mode;
    }
private:
    Mode mode;
    std::string name;
    std::chrono::steady_clock::time_point start_time;
};