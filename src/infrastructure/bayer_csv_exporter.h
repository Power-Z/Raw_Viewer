#pragma once

#include "application/bayer_extract.h"

namespace rawviewer::infrastructure {

class BayerCsvExporter final : public application::IBayerPlaneExporter {
public:
    application::BayerExportResult exportCsv(
        const application::BayerExportRequest& request) const override;
};

} // namespace rawviewer::infrastructure
