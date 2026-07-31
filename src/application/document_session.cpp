#include "application/document_session.h"

#include <limits>

namespace rawviewer::application {

DocumentSession::DocumentSession(
    std::shared_ptr<const DecodedImage> original)
    : original_(std::move(original)),
      initialMapping_(defaultMapping(original_->metadata)),
      mapping_(initialMapping_) {}

void DocumentSession::beginDisplayEdit() {
    if (!editStart_) {
        editStart_ = mapping_;
    }
}

domain::DisplayMappingValidation DocumentSession::updateDisplayMapping(
    const domain::DisplayMapping& mapping) {
    const auto validation = domain::validateDisplayMapping(mapping);
    if (!validation.valid) {
        return validation;
    }
    if (!editStart_) {
        beginDisplayEdit();
    }
    if (mapping != mapping_) {
        apply(mapping);
    }
    return validation;
}

bool DocumentSession::commitDisplayEdit() {
    if (!editStart_) {
        return false;
    }
    const auto before = *editStart_;
    editStart_.reset();
    if (before == mapping_) {
        return false;
    }
    if (undo_.size() == undoLimit) {
        undo_.erase(undo_.begin());
    }
    undo_.push_back({before, mapping_});
    redo_.clear();
    return true;
}

void DocumentSession::cancelDisplayEdit() {
    if (!editStart_) {
        return;
    }
    const auto before = *editStart_;
    editStart_.reset();
    if (before != mapping_) {
        apply(before);
    }
}

bool DocumentSession::canUndo() const noexcept {
    return !undo_.empty() && !editStart_;
}

bool DocumentSession::canRedo() const noexcept {
    return !redo_.empty() && !editStart_;
}

bool DocumentSession::undo() {
    if (!canUndo()) {
        return false;
    }
    const auto change = undo_.back();
    undo_.pop_back();
    apply(change.before);
    redo_.push_back(change);
    return true;
}

bool DocumentSession::redo() {
    if (!canRedo()) {
        return false;
    }
    const auto change = redo_.back();
    redo_.pop_back();
    apply(change.after);
    undo_.push_back(change);
    return true;
}

domain::DisplayMapping DocumentSession::defaultMapping(
    const ImageMetadata& metadata) noexcept {
    domain::DisplayMapping mapping;
    mapping.blackPoint = metadata.sensorBlackLevel;
    mapping.whitePoint = metadata.whiteLevel;
    if (mapping.whitePoint <= mapping.blackPoint) {
        mapping.blackPoint = 0.0;
        switch (metadata.scalarType) {
        case domain::ScalarType::UInt8:
            mapping.whitePoint = 255.0;
            break;
        case domain::ScalarType::UInt16:
            mapping.whitePoint = 65535.0;
            break;
        case domain::ScalarType::UInt32:
            mapping.whitePoint =
                static_cast<double>(std::numeric_limits<std::uint32_t>::max());
            break;
        case domain::ScalarType::Float32:
            mapping.whitePoint = 1.0;
            break;
        }
    }
    mapping.gamma =
        metadata.kind == ImageKind::Standard ? 1.0 : 2.2;
    return mapping;
}

void DocumentSession::apply(const domain::DisplayMapping& mapping) {
    mapping_ = mapping;
    ++revision_;
}

} // namespace rawviewer::application
