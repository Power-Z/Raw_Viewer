#pragma once

#include "application/bayer_extract.h"
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
        return state_.displayMapping;
    }
    const domain::DisplayMapping& initialDisplayMapping() const noexcept {
        return initialMapping_;
    }
    std::uint64_t revision() const noexcept { return revision_; }
    const std::shared_ptr<const DecodedImage>& displaySource() const noexcept {
        return state_.displaySource;
    }
    const std::shared_ptr<const DecodedImage>& preDemosaicSource() const noexcept {
        return state_.preDemosaicSource;
    }
    const std::shared_ptr<const BayerExtraction>& bayerExtraction() const noexcept {
        return state_.bayerExtraction;
    }

    void beginDisplayEdit();
    domain::DisplayMappingValidation updateDisplayMapping(
        const domain::DisplayMapping& mapping);
    bool commitDisplayEdit();
    void cancelDisplayEdit();
    bool commitPipelineEdit(
        std::shared_ptr<const DecodedImage> displaySource,
        std::shared_ptr<const DecodedImage> preDemosaicSource = {},
        std::shared_ptr<const BayerExtraction> bayerExtraction = {});

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    bool undo();
    bool redo();
    std::size_t undoCount() const noexcept { return undo_.size(); }
    std::size_t redoCount() const noexcept { return redo_.size(); }

private:
    struct State {
        domain::DisplayMapping displayMapping;
        std::shared_ptr<const DecodedImage> displaySource;
        std::shared_ptr<const DecodedImage> preDemosaicSource;
        std::shared_ptr<const BayerExtraction> bayerExtraction;

        bool operator==(const State&) const = default;
    };

    struct Change {
        State before;
        State after;
    };

    static domain::DisplayMapping defaultMapping(
        const ImageMetadata& metadata) noexcept;
    bool commitChange(const State& before, const State& after);
    void apply(const State& state);

    std::shared_ptr<const DecodedImage> original_;
    domain::DisplayMapping initialMapping_;
    State state_;
    std::uint64_t revision_ = 0;
    std::optional<State> editStart_;
    std::vector<Change> undo_;
    std::vector<Change> redo_;
};

} // namespace rawviewer::application
