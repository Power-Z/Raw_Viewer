#include "application/document_session.h"

#include <limits>

namespace rawviewer::application {

DocumentSession::DocumentSession(
    std::shared_ptr<const DecodedImage> original)
    : original_(std::move(original)),
      initialMapping_(defaultMapping(original_->metadata)),
      state_{initialMapping_, original_, {}, {}} {}

void DocumentSession::beginDisplayEdit() {
    if (!editStart_) {
        editStart_ = state_;
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
    if (mapping != state_.displayMapping) {
        redo_.clear();
        auto next = state_;
        next.displayMapping = mapping;
        apply(next);
    }
    return validation;
}

bool DocumentSession::commitDisplayEdit() {
    if (!editStart_) {
        return false;
    }
    const State before = *editStart_;
    editStart_.reset();
    return commitChange(before, state_);
}

void DocumentSession::cancelDisplayEdit() {
    if (!editStart_) {
        return;
    }
    const State before = *editStart_;
    editStart_.reset();
    if (before != state_) {
        apply(before);
    }
}

bool DocumentSession::commitPipelineEdit(
    std::shared_ptr<const DecodedImage> displaySource,
    std::shared_ptr<const DecodedImage> preDemosaicSource,
    std::shared_ptr<const BayerExtraction> bayerExtraction) {
    if (!displaySource) {
        return false;
    }
    // A background pipeline task may finish while a display control gesture
    // is still active. Serialize both user-visible operations instead of
    // dropping the pipeline result or merging it into the display edit.
    commitDisplayEdit();
    const State before = state_;
    State after = state_;
    after.displaySource = std::move(displaySource);
    after.preDemosaicSource = std::move(preDemosaicSource);
    after.bayerExtraction = std::move(bayerExtraction);
    if (before == after) {
        return false;
    }
    apply(after);
    return commitChange(before, after);
}

bool DocumentSession::canUndo() const noexcept {
    return editStart_ ? *editStart_ != state_ : !undo_.empty();
}

bool DocumentSession::canRedo() const noexcept {
    return !redo_.empty() && !editStart_;
}

bool DocumentSession::undo() {
    if (!canUndo()) {
        return false;
    }
    if (editStart_) {
        const State before = *editStart_;
        const State after = state_;
        editStart_.reset();
        apply(before);
        redo_.push_back({before, after});
        return true;
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
    if (undo_.size() == undoLimit) {
        undo_.erase(undo_.begin());
    }
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
    mapping.gamma = 1.0;
    return mapping;
}

bool DocumentSession::commitChange(const State& before, const State& after) {
    if (before == after) {
        return false;
    }
    if (undo_.size() == undoLimit) {
        undo_.erase(undo_.begin());
    }
    undo_.push_back({before, after});
    redo_.clear();
    return true;
}

void DocumentSession::apply(const State& state) {
    state_ = state;
    ++revision_;
}

} // namespace rawviewer::application
