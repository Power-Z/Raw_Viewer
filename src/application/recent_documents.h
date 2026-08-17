#pragma once

#include "domain/raw_descriptor.h"

#include <filesystem>
#include <vector>

namespace rawviewer::application {

struct RecentDocument {
    std::filesystem::path path;
    domain::RawDescriptor rawDescriptor;
};

class IRecentDocumentStore {
public:
    virtual ~IRecentDocumentStore() = default;
    virtual std::vector<RecentDocument> load() const = 0;
    virtual void remember(const RecentDocument& document) = 0;
    virtual void clear() = 0;
};

} // namespace rawviewer::application
