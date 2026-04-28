#pragma once
#include <string>
#include "../common/status.h"
#include "page.h"

namespace db {

class PageManager {
public:
    PageManager(std::string file_path, std::size_t page_size);

    Status Open();
    Status Flush();

    Status ReadPage(PageId id, Page* out);
    Status WritePage(const Page& page);
    Status AllocatePage(PageId* out);

    std::size_t page_size() const { return page_size_; }

private:
    std::string file_path_;
    std::size_t page_size_;
};

}
