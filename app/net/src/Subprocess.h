// A child process whose stdin and stdout are pipes to this one; its stderr is ours.
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "fulmetry/core/Result.h"

namespace fulmetry::net {

class Subprocess {
public:
    static Result<std::unique_ptr<Subprocess>> start(const std::vector<std::string>& argv);
    ~Subprocess();
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    bool writeLine(const std::string& line);  // thread-safe
    bool readLine(std::string& line);         // blocks; false at end of output. One reader at a time.
    void closeInput();                        // the child sees end of input

private:
    Subprocess() = default;
    std::mutex writeMu_;
    std::string buffer_;
#ifdef _WIN32
    void* process_ = nullptr;
    void* in_ = nullptr;
    void* out_ = nullptr;
#else
    int pid_ = -1;
    int in_ = -1;
    int out_ = -1;
#endif
};

}  // namespace fulmetry::net
