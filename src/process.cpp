/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/

#include <miopen/errors.hpp>
#include <miopen/process.hpp>
#include <vector>
#include <string_view>

#ifdef _WIN32
#include <system_error>
#endif

namespace miopen {

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

enum class Direction : bool
{
    Input,
    Output
};

struct SystemError : std::runtime_error
{
    SystemError() : std::runtime_error{std::system_category().message(GetLastError())} {}
};

template <Direction direction>
struct Pipe
{
    HANDLE readHandle, writeHandle;

    Pipe() : readHandle{nullptr}, writeHandle{nullptr}
    {
        SECURITY_ATTRIBUTES attrs;
        attrs.nLength              = sizeof(SECURITY_ATTRIBUTES);
        attrs.bInheritHandle       = TRUE;
        attrs.lpSecurityDescriptor = nullptr;

        if(CreatePipe(&readHandle, &writeHandle, &attrs, 0) == FALSE)
            throw SystemError();

        if(direction == Direction::Output)
        {
            // Do not inherit the read handle for the output pipe
            if(SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0) == 0)
                throw SystemError();
        }
        else
        {
            // Do not inherit the write handle for the input pipe
            if(SetHandleInformation(writeHandle, HANDLE_FLAG_INHERIT, 0) == 0)
                throw SystemError();
        }
    }

    Pipe(Pipe&&) = default;

    ~Pipe()
    {
        if(writeHandle != nullptr)
        {
            CloseHandle(writeHandle);
        }
        if(readHandle != nullptr)
        {
            CloseHandle(readHandle);
        }
    }

    bool CloseWriteHandle()
    {
        auto result = true;
        if(writeHandle != nullptr)
        {
            result      = CloseHandle(writeHandle) == TRUE;
            writeHandle = nullptr;
        }
        return result;
    }

    bool CloseReadHandle()
    {
        auto result = true;
        if(readHandle != nullptr)
        {
            result     = CloseHandle(readHandle) == TRUE;
            readHandle = nullptr;
        }
        return result;
    }

    std::pair<bool, DWORD> Read(LPVOID buffer, DWORD size) const
    {
        DWORD bytes_read;
        if(ReadFile(readHandle, buffer, size, &bytes_read, nullptr) == FALSE &&
           GetLastError() == ERROR_MORE_DATA)
        {
            return {true, bytes_read};
        }
        return {false, bytes_read};
    }

    bool Write(LPVOID buffer, DWORD size) const
    {
        DWORD bytes_written;
        return WriteFile(writeHandle, buffer, size, &bytes_written, nullptr) == TRUE;
    }
};

struct ProcessImpl
{
    explicit ProcessImpl(const std::string_view cmd) : command{cmd} {}

    template <Direction direction>
    void Create(std::vector<char>& buffer)
    {
        STARTUPINFOA info;
        ZeroMemory(&info, sizeof(STARTUPINFO));
        info.cb = sizeof(STARTUPINFO);

        std::string cmd{command};
        if(!args.empty())
            cmd += " " + args;

        // Refer to
        // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa
        constexpr std::size_t BUFFER_CAPACITY = 32767;

        if(cmd.size() < BUFFER_CAPACITY)
            cmd.resize(BUFFER_CAPACITY, '\0');

        if(CreateProcess(command.c_str(),
                         cmd.data(),
                         nullptr,
                         nullptr,
                         FALSE,
                         0,
                         envs.empty() ? nullptr : envs.data(),
                         cwd.empty() ? nullptr : cwd.c_str(),
                         &info,
                         &processInfo) == FALSE)
            MIOPEN_THROW("CreateProcess error: " + std::to_string(GetLastError()));

        CloseHandle(processInfo.hThread);

        if(!output.CloseWriteHandle())
            MIOPEN_THROW("Error closing STDOUT handle for writing (" +
                         std::to_string(GetLastError()) + ")");

        if(!input.CloseReadHandle())
            MIOPEN_THROW("Error closing STDIN handle for reading (" +
                         std::to_string(GetLastError()) + ")");
    }

    void EnvironmentVariables(const std::map<std::string_view, std::string_view>& map) {}

    void Arguments(std::string_view arguments) { this->args = arguments; }
    void WorkingDirectory(const fs::path& path) { this->cwd = path.string(); }

    int Wait() const
    {
        if(!input.CloseWriteHandle())
            MIOPEN_THROW("Error closing STDIN handle for writing (" +
                         std::to_string(GetLastError()) + ")");

        WaitForSingleObject(processInfo.hProcess, INFINITE);

        DWORD status;
        const auto getExitCodeStatus = GetExitCodeProcess(processInfo.hProcess, &status);

        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);

        if(getExitCodeStatus == 0)
            MIOPEN_THROW("GetExitCodeProcess error: " + std::to_string(GetLastError()));

        return status;
    }

private:
    std::string command;
    PROCESS_INFORMATION processInfo{};
    std::string args;
    std::string cwd; // converted to std::string from fs::path in WokringDirectory() (read-only)
    std::vector<char> envs;
    mutable Pipe<Direction::Input> input;
    Pipe<Direction::Output> output;
};

#else

struct ProcessImpl
{
    ProcessImpl(std::string_view cmd, std::string_view arguments) : command{cmd}, args{arguments} {}

    std::string GetCommand() const
    {
        std::string cmd{cwd};
        if(!cwd.empty())
            cmd += "; ";
        cmd += envs + command;
        if(!args.empty())
            cmd += " " + args;
        return cmd;
    }

    void Execute()
    {
        pipe = popen(GetCommand().c_str(), "r");
        if(pipe == nullptr)
            MIOPEN_THROW("Error: popen()");
        output_buffer = nullptr;
    }

    void Read(std::vector<char>& buffer)
    {
        pipe = popen(GetCommand().c_str(), "r");
        if(pipe == nullptr)
            MIOPEN_THROW("Error: popen()");
        output_buffer = &buffer;
    }

    void Write(const std::vector<char>& buffer)
    {
        pipe = popen(GetCommand().c_str(), "w");
        if(pipe == nullptr)
            MIOPEN_THROW("Error: popen()");
        output_buffer = nullptr;
        std::fwrite(buffer.data(), 1, buffer.size(), pipe);
    }

    int Wait() const
    {
        std::array<char, 1024> buffer{};
        if(output_buffer != nullptr)
        {
            output_buffer->clear();
            while(feof(pipe) == 0)
            {
                if(fgets(buffer.data(), buffer.size(), pipe) != nullptr)
                    output_buffer->insert(output_buffer->end(), buffer.begin(), buffer.end());
            }
        }
        else
        {
            while(feof(pipe) == 0)
            {
                if(fgets(buffer.data(), buffer.size(), pipe) != nullptr)
                    std::cout << buffer.data();
            }
        }
        auto status = pclose(pipe);
        return WEXITSTATUS(status);
    }

    void WorkingDirectory(const fs::path& path) { cwd = path.string(); }
    void EnvironmentVariables(const std::map<std::string_view, std::string_view>& map)
    {
        envs.clear();
        for (const auto& [name, value] : map)
            envs += name + "=" + value + " ";
    }

private:
    std::string command;
    FILE* pipe = nullptr;
    std::string args;
    std::string cwd;
    std::string envs;
    std::vector<char>* output_buffer = nullptr;
};

#endif

Process::Process(const fs::path& cmd, std::string_view args)
    : impl{std::make_unique<ProcessImpl>(cmd.string(), args)}
{
}

Process::~Process() noexcept = default;

Process::Process(Process&&) noexcept = default;
Process& Process::operator=(Process&&) noexcept = default;

Process& Process::WorkingDirectory(const fs::path& cwd)
{
    impl->WorkingDirectory(cwd);
    return *this;
}

Process& Process::EnvironmentVariables(std::map<std::string_view, std::string_view> vars)
{
    impl->EnvironmentVariables(vars);
    return *this;
}

const Process& Process::Execute() const
{
    impl->Execute();
    return *this;
}

const Process& Process::Read(std::vector<char>& buffer) const
{
    impl->Read(buffer);
    return *this;
}

const Process& Process::Write(const std::vector<char>& buffer) const
{
    impl->Write(buffer);
    return *this;
}

int Process::Wait() const { return impl->Wait(); }

} // namespace miopen
