#include "snailtrail/log/mapped_file.hpp"

#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace snailtrail::log {

#ifdef _WIN32

MappedFile::MappedFile(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "cannot open " + path.string());
    }
    file_ = file;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        const auto error = static_cast<int>(GetLastError());
        release();
        throw std::system_error(error, std::system_category(), "cannot stat " + path.string());
    }
    size_ = static_cast<std::size_t>(size.QuadPart);
    if (size_ == 0) return;

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        const auto error = static_cast<int>(GetLastError());
        release();
        throw std::system_error(error, std::system_category(), "cannot map " + path.string());
    }
    mapping_ = mapping;
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        const auto error = static_cast<int>(GetLastError());
        release();
        throw std::system_error(error, std::system_category(), "cannot map " + path.string());
    }
    data_ = static_cast<const char*>(view);
}

void MappedFile::release() noexcept {
    if (data_ != nullptr) UnmapViewOfFile(data_);
    if (mapping_ != nullptr) CloseHandle(static_cast<HANDLE>(mapping_));
    if (file_ != nullptr) CloseHandle(static_cast<HANDLE>(file_));
    data_ = nullptr;
    mapping_ = nullptr;
    file_ = nullptr;
    size_ = 0;
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
      file_(std::exchange(other.file_, nullptr)), mapping_(std::exchange(other.mapping_, nullptr)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        release();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        file_ = std::exchange(other.file_, nullptr);
        mapping_ = std::exchange(other.mapping_, nullptr);
    }
    return *this;
}

#else

MappedFile::MappedFile(const std::filesystem::path& path) {
    fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) throw std::system_error(errno, std::generic_category(), "cannot open " + path.string());

    struct stat st {};
    if (::fstat(fd_, &st) != 0) {
        const int error = errno;
        release();
        throw std::system_error(error, std::generic_category(), "cannot stat " + path.string());
    }
    if (!S_ISREG(st.st_mode)) {
        release();
        throw std::system_error(EINVAL, std::generic_category(), "not a regular file: " + path.string());
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ == 0) return;

    void* view = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (view == MAP_FAILED) {
        const int error = errno;
        release();
        throw std::system_error(error, std::generic_category(), "cannot map " + path.string());
    }
    ::madvise(view, size_, MADV_SEQUENTIAL);
    data_ = static_cast<const char*>(view);
}

void MappedFile::release() noexcept {
    if (data_ != nullptr) ::munmap(const_cast<char*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
    data_ = nullptr;
    size_ = 0;
    fd_ = -1;
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
      fd_(std::exchange(other.fd_, -1)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        release();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

#endif

MappedFile::~MappedFile() { release(); }

}
