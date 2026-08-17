#pragma once

#include "application/recent_documents.h"

#include <QString>

namespace rawviewer::infrastructure {

class QtRecentDocumentStore final
    : public application::IRecentDocumentStore {
public:
    // An explicit INI path is intended for isolated tests. Production uses
    // the application's organization and application QSettings identity.
    explicit QtRecentDocumentStore(QString settingsFile = {});

    std::vector<application::RecentDocument> load() const override;
    void remember(const application::RecentDocument& document) override;
    void clear() override;

private:
    void save(const std::vector<application::RecentDocument>& documents) const;

    QString settingsFile_;
};

} // namespace rawviewer::infrastructure
