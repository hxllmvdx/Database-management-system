#include "storage/page_manager.h"
#include "common/file_utils.h"
#include <cstdint>
#include <fstream>
#include <sstream>
#include <vector>

db::PageManager::PageManager(std::string file_path, std::size_t page_size)
    : file_path_(file_path),
    page_size_(page_size),
    file_(),
    is_open_(false),
    page_count_(0)
{}

db::Status db::PageManager::Open() {
    if (is_open_) {
        return Status::Ok();
    }

    if (file_path_.empty() || page_size_ == 0) {
        return Status::Error(StatusCode::kInvalidArgument, "Invalid file path or page size");
    }

    std::size_t dir_end = file_path_.find_last_of('/');
    std::string dir = dir_end == std::string::npos ? std::string() : file_path_.substr(0, dir_end);

    if (!dir.empty()) {
        Status code = file_utils::EnsureDir(dir);
        if (!code.ok()) {
            return code;
        }
    }

    bool exists = false;
    Status file_code = file_utils::FileExists(file_path_, &exists);
    if (!file_code.ok()) {
        return file_code;
    }

    if (!exists) {
        std::ofstream create_file(file_path_, std::ios::binary);
        if (!create_file) {
            return Status::Error(StatusCode::kIoError, "Failed to create file");
        }
        create_file.close();
    }

    std::fstream file(
        file_path_,
        std::ios::in | std::ios::out | std::ios::binary
    );

    if (!file) {
        return Status::Error(StatusCode::kIoError, "Failed to open file");
    }

    file.seekg(0, std::ios::end);
    std::streampos end_pos = file.tellg();
    if (end_pos < 0) {
        return Status::Error(StatusCode::kIoError, "Failed to determine file size for " + file_path_);
    }
    std::uint64_t size = static_cast<std::uint64_t>(end_pos);
    file.seekg(0, std::ios::beg);

    if (size % page_size_ != 0) {
        std::ostringstream oss;
        oss << "File size " << size << " is not a multiple of page size " << page_size_;
        return Status::Error(StatusCode::kInvalidArgument, oss.str());
    }

    page_count_ = size / page_size_;

    file_ = std::move(file);
    is_open_ = true;

    return Status::Ok();
}

db::Status db::PageManager::ReadPage(PageId id, Page* out) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "Page manager is not open");
    }

    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Out pointer is null");
    }

    if (id.value >= page_count_) {
        std::ostringstream oss;
        oss << "Page " << id.value << " is out of range, page count is " << page_count_;
        return Status::Error(StatusCode::kNotFound, oss.str());
    }

    std::uint64_t offset = id.value * page_size_;

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) {
        std::ostringstream oss;
        oss << "Failed to seek to page " << id.value << " at offset " << offset;
        return Status::Error(StatusCode::kIoError, oss.str());
    }

    out->id = id;
    out->data.resize(page_size_);

    file_.read(reinterpret_cast<char*>(out->data.data()), static_cast<std::streamsize>(page_size_));

    if (!file_ || file_.gcount() != static_cast<std::streamsize>(page_size_)) {
        std::ostringstream oss;
        oss << "Failed to read page " << id.value << ", expected " << page_size_
            << " bytes, got " << file_.gcount();
        return Status::Error(StatusCode::kIoError, oss.str());
    }

    return Status::Ok();
}

db::Status db::PageManager::WritePage(const Page& page) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "Page manager is not open");
    }

    if (page.id.value >= page_count_) {
        std::ostringstream oss;
        oss << "Page " << page.id.value << " is out of range, page count is " << page_count_;
        return Status::Error(StatusCode::kNotFound, oss.str());
    }

    if (page.data.size() != page_size_) {
        return Status::Error(StatusCode::kInvalidArgument, "Page data size does not match page size");
    }

    std::uint64_t offset = page.id.value * page_size_;

    file_.clear();
    file_.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file_) {
        std::ostringstream oss;
        oss << "Failed to seek to page " << page.id.value << " at offset " << offset
            << " for writing";
        return Status::Error(StatusCode::kIoError, oss.str());
    }

    file_.write(reinterpret_cast<const char*>(page.data.data()), static_cast<std::streamsize>(page_size_));

    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to write page");
    }

    return Status::Ok();
}

db::Status db::PageManager::AllocatePage(PageId* out) {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "Page manager is not open");
    }

    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "Out pointer is null");
    }

    file_.clear();
    file_.seekp(0, std::ios::end);
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to seek end of file");
    }

    std::vector<char> zeros(page_size_, 0);

    file_.write(zeros.data(), static_cast<std::streamsize>(page_size_));

    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to allocate page");
    }

    out->value = page_count_;

    page_count_++;

    return Status::Ok();
}

db::Status db::PageManager::Flush() {
    if (!is_open_) {
        return Status::Error(StatusCode::kInternalError, "Page manager is not open");
    }

    file_.clear();
    file_.flush();
    if (!file_) {
        return Status::Error(StatusCode::kIoError, "Failed to flush file");
    }
    return Status::Ok();
}
