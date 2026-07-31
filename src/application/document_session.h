#pragma once

#include "application/image_types.h"
#include "domain/display_mapping.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rawviewer::application {

class DocumentSession {
public:
    static constexpr std::size_t undoLimit = 5;

    explicit DocumentSession(std::shared_ptr<const DecodedImage> original);

    const std::shared_ptr<const DecodedImage>& original() const noexcept {
        return original_;
    }
    const domain::DisplayMapping& displayMapping() const noexcept {
        return mapping_;
    }
    const domain::DisplayMapping& initialDisplayMapping() const noexcept {
        return initialMapping_;
    }
    std::uint64_t revision() const noexcept { return revision_; }

    void beginDisplayEdit();
    domain::DisplayMappingValidation updateDisplayMapping(
        const domain::DisplayMapping& mapping);
    bool commitDisplayEdit();
    void cancelDisplayEdit();

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    bool undo();
    bool redo();
    std::size_t undoCount() const noexcept { return undo_.size(); }
    std::size_t redoCount() const noexcept { return redo_.size(); }

private:
    struct Change {
        domain::DisplayMapping before;
        domain::DisplayMapping after;
    };

    static domain::DisplayMapping defaultMapping(
        const ImageMetadata& metadata) noexcept;
    void apply(const domain::DisplayMapping& mapping);

    std::shared_ptr<const DecodedImage> original_;
    domain::DisplayMapping initialMapping_;
    domain::DisplayMapping mapping_;
    std::uint64_t revision_ = 0;
    std::optional<domain::DisplayMapping> editStart_;
    std::vector<Change> undo_;
    std::vector<Change> redo_;
};

} // namespace rawviewer::application
