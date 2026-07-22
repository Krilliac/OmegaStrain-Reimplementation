#pragma once

#include <utility>

namespace omega::content
{
class GameDataService;
namespace detail
{
struct RetailFrontEndPresentationCapabilityTestAccess;
}

// Move-only evidence that an owned canonical front-end presentation came
// through the retail game-data boundary. Ordinary host/runtime code cannot
// construct or renew this capability. The eventual evidenced loader will mint
// it inside GameDataService and move it with the presentation it authenticates.
class RetailFrontEndPresentationCapability final
{
public:
    RetailFrontEndPresentationCapability() = delete;
    RetailFrontEndPresentationCapability(
        const RetailFrontEndPresentationCapability&) = delete;
    RetailFrontEndPresentationCapability& operator=(
        const RetailFrontEndPresentationCapability&) = delete;
    RetailFrontEndPresentationCapability(
        RetailFrontEndPresentationCapability&& other) noexcept
        : valid_(std::exchange(other.valid_, false))
    {
    }
    RetailFrontEndPresentationCapability& operator=(
        RetailFrontEndPresentationCapability&& other) noexcept
    {
        if (this != &other)
            valid_ = std::exchange(other.valid_, false);
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return valid_;
    }

private:
    struct ConstructionKey final
    {
    };

    explicit RetailFrontEndPresentationCapability(ConstructionKey) noexcept
        : valid_(true)
    {
    }

    bool valid_ = false;

    friend class GameDataService;
    friend struct detail::RetailFrontEndPresentationCapabilityTestAccess;
};
} // namespace omega::content
