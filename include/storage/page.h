#pragma once
#include "../common/bytes.h"
#include "page_id.h"

namespace db {

struct Page {
    PageId id;
    ByteBuffer data;
};

}
