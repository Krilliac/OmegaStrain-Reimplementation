#pragma once

#include "omega/asset/frontend_ir.h"
#include "omega/asset/indexed_image_ir.h"
#include "omega/content/retail_front_end_presentation_capability.h"
#include "omega/retail/fnt_v3_decoder.h"
#include "omega/retail/retail_string_table_decoder.h"

#include <map>
#include <string>
#include <utility>

namespace omega::content
{
class GameDataService;

enum class FrontEndScreenKey
{
    Title,
    CreateAgent,
    LoadAgent,
};

// Fully owned canonical presentation data for one retail front-end screen. The
// game-data service is the only constructor: ordinary runtime code can consume
// or move this value, but cannot fabricate retail presentation evidence.
class FrontEndScreenBundle final
{
public:
    using TextureMap = std::map<std::string, asset::IndexedImageIR, std::less<>>;
    using FontMap = std::map<std::string, retail::FntV3IR, std::less<>>;

    FrontEndScreenBundle() = delete;
    FrontEndScreenBundle(const FrontEndScreenBundle&) = delete;
    FrontEndScreenBundle& operator=(const FrontEndScreenBundle&) = delete;
    FrontEndScreenBundle(FrontEndScreenBundle&&) noexcept = default;
    FrontEndScreenBundle& operator=(FrontEndScreenBundle&&) noexcept = default;

    [[nodiscard]] FrontEndScreenKey key() const noexcept { return key_; }
    [[nodiscard]] const asset::FrontendWidgetDocumentIR& widget_document() const noexcept
    {
        return widget_document_;
    }
    [[nodiscard]] const asset::FrontendVisualDocumentIR& visual_document() const noexcept
    {
        return visual_document_;
    }
    [[nodiscard]] const TextureMap& screen_textures() const noexcept
    {
        return screen_textures_;
    }
    [[nodiscard]] const FontMap& fonts() const noexcept { return fonts_; }
    [[nodiscard]] const TextureMap& font_atlases() const noexcept
    {
        return font_atlases_;
    }
    [[nodiscard]] const retail::RetailStringTableIR& strings() const noexcept
    {
        return strings_;
    }
    [[nodiscard]] const RetailFrontEndPresentationCapability&
    presentation_capability() const noexcept
    {
        return presentation_capability_;
    }

private:
    FrontEndScreenBundle(FrontEndScreenKey key,
        asset::FrontendWidgetDocumentIR widget_document,
        asset::FrontendVisualDocumentIR visual_document,
        TextureMap screen_textures, FontMap fonts, TextureMap font_atlases,
        retail::RetailStringTableIR strings,
        RetailFrontEndPresentationCapability presentation_capability) noexcept
        : key_(key),
          widget_document_(std::move(widget_document)),
          visual_document_(std::move(visual_document)),
          screen_textures_(std::move(screen_textures)),
          fonts_(std::move(fonts)),
          font_atlases_(std::move(font_atlases)),
          strings_(std::move(strings)),
          presentation_capability_(std::move(presentation_capability))
    {
    }

    FrontEndScreenKey key_ = FrontEndScreenKey::Title;
    asset::FrontendWidgetDocumentIR widget_document_;
    asset::FrontendVisualDocumentIR visual_document_;
    TextureMap screen_textures_;
    FontMap fonts_;
    TextureMap font_atlases_;
    retail::RetailStringTableIR strings_;
    RetailFrontEndPresentationCapability presentation_capability_;

    friend class GameDataService;
};
} // namespace omega::content
