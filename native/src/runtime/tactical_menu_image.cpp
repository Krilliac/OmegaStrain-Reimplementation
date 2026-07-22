#include "omega/runtime/tactical_menu_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

namespace omega::runtime {
namespace {
constexpr std::size_t kChannelsPerPixel = 4U;
constexpr std::size_t kMenuImageBytes =
    static_cast<std::size_t>(kTacticalMenuImageWidth) *
    kTacticalMenuImageHeight * kChannelsPerPixel;
constexpr int kCoverageAxisSamples = 4;
constexpr int kCoverageSamples = kCoverageAxisSamples * kCoverageAxisSamples;

struct Color {
  std::uint8_t red = 0U;
  std::uint8_t green = 0U;
  std::uint8_t blue = 0U;
  std::uint8_t alpha = 255U;
};

constexpr Color kIce{172U, 219U, 226U, 255U};
constexpr Color kIceDim{93U, 132U, 143U, 255U};
constexpr Color kIceFaint{53U, 80U, 91U, 180U};
constexpr Color kAmber{226U, 171U, 69U, 255U};
constexpr Color kAmberDim{141U, 105U, 42U, 210U};
constexpr Color kPanel{13U, 27U, 35U, 210U};
constexpr Color kPanelEdge{49U, 82U, 92U, 170U};

struct Stroke {
  float x0 = 0.0F;
  float y0 = 0.0F;
  float x1 = 0.0F;
  float y1 = 0.0F;
};

struct Glyph {
  std::span<const Stroke> strokes;
  float advance = 1.0F;
};

// Project-authored monoline face. Coordinates are continuous normalized
// vectors, not bitmap cells; curves are deliberately faceted into short strokes
// for a restrained technical-drafting character.
constexpr Stroke kA[]{{0.05F, 1.0F, 0.5F, 0.0F},
                      {0.5F, 0.0F, 0.95F, 1.0F},
                      {0.23F, 0.61F, 0.77F, 0.61F}};
constexpr Stroke kB[]{{0.08F, 0.0F, 0.08F, 1.0F},  {0.08F, 0.0F, 0.64F, 0.0F},
                      {0.64F, 0.0F, 0.88F, 0.14F}, {0.88F, 0.14F, 0.88F, 0.36F},
                      {0.88F, 0.36F, 0.64F, 0.5F}, {0.08F, 0.5F, 0.67F, 0.5F},
                      {0.67F, 0.5F, 0.92F, 0.65F}, {0.92F, 0.65F, 0.92F, 0.86F},
                      {0.92F, 0.86F, 0.67F, 1.0F}, {0.67F, 1.0F, 0.08F, 1.0F}};
constexpr Stroke kC[]{{0.9F, 0.12F, 0.69F, 0.0F},  {0.69F, 0.0F, 0.27F, 0.0F},
                      {0.27F, 0.0F, 0.08F, 0.18F}, {0.08F, 0.18F, 0.08F, 0.82F},
                      {0.08F, 0.82F, 0.27F, 1.0F}, {0.27F, 1.0F, 0.69F, 1.0F},
                      {0.69F, 1.0F, 0.9F, 0.88F}};
constexpr Stroke kD[]{{0.08F, 0.0F, 0.08F, 1.0F}, {0.08F, 0.0F, 0.58F, 0.0F},
                      {0.58F, 0.0F, 0.9F, 0.25F}, {0.9F, 0.25F, 0.9F, 0.75F},
                      {0.9F, 0.75F, 0.58F, 1.0F}, {0.58F, 1.0F, 0.08F, 1.0F}};
constexpr Stroke kE[]{{0.08F, 0.0F, 0.08F, 1.0F},
                      {0.08F, 0.0F, 0.92F, 0.0F},
                      {0.08F, 0.5F, 0.73F, 0.5F},
                      {0.08F, 1.0F, 0.92F, 1.0F}};
constexpr Stroke kF[]{{0.08F, 0.0F, 0.08F, 1.0F},
                      {0.08F, 0.0F, 0.92F, 0.0F},
                      {0.08F, 0.5F, 0.73F, 0.5F}};
constexpr Stroke kG[]{{0.9F, 0.14F, 0.68F, 0.0F},  {0.68F, 0.0F, 0.27F, 0.0F},
                      {0.27F, 0.0F, 0.08F, 0.18F}, {0.08F, 0.18F, 0.08F, 0.82F},
                      {0.08F, 0.82F, 0.27F, 1.0F}, {0.27F, 1.0F, 0.9F, 1.0F},
                      {0.9F, 1.0F, 0.9F, 0.57F},   {0.9F, 0.57F, 0.55F, 0.57F}};
constexpr Stroke kH[]{{0.08F, 0.0F, 0.08F, 1.0F},
                      {0.92F, 0.0F, 0.92F, 1.0F},
                      {0.08F, 0.5F, 0.92F, 0.5F}};
constexpr Stroke kI[]{{0.17F, 0.0F, 0.83F, 0.0F},
                      {0.5F, 0.0F, 0.5F, 1.0F},
                      {0.17F, 1.0F, 0.83F, 1.0F}};
constexpr Stroke kJ[]{{0.14F, 0.0F, 0.9F, 0.0F},
                      {0.69F, 0.0F, 0.69F, 0.82F},
                      {0.69F, 0.82F, 0.53F, 1.0F},
                      {0.53F, 1.0F, 0.23F, 1.0F},
                      {0.23F, 1.0F, 0.08F, 0.82F}};
constexpr Stroke kK[]{{0.08F, 0.0F, 0.08F, 1.0F},
                      {0.9F, 0.0F, 0.08F, 0.62F},
                      {0.42F, 0.37F, 0.94F, 1.0F}};
constexpr Stroke kL[]{{0.08F, 0.0F, 0.08F, 1.0F}, {0.08F, 1.0F, 0.92F, 1.0F}};
constexpr Stroke kM[]{{0.05F, 1.0F, 0.05F, 0.0F},
                      {0.05F, 0.0F, 0.5F, 0.55F},
                      {0.5F, 0.55F, 0.95F, 0.0F},
                      {0.95F, 0.0F, 0.95F, 1.0F}};
constexpr Stroke kN[]{{0.08F, 1.0F, 0.08F, 0.0F},
                      {0.08F, 0.0F, 0.92F, 1.0F},
                      {0.92F, 1.0F, 0.92F, 0.0F}};
constexpr Stroke kO[]{{0.28F, 0.0F, 0.72F, 0.0F}, {0.72F, 0.0F, 0.92F, 0.2F},
                      {0.92F, 0.2F, 0.92F, 0.8F}, {0.92F, 0.8F, 0.72F, 1.0F},
                      {0.72F, 1.0F, 0.28F, 1.0F}, {0.28F, 1.0F, 0.08F, 0.8F},
                      {0.08F, 0.8F, 0.08F, 0.2F}, {0.08F, 0.2F, 0.28F, 0.0F}};
constexpr Stroke kP[]{
    {0.08F, 1.0F, 0.08F, 0.0F},  {0.08F, 0.0F, 0.65F, 0.0F},
    {0.65F, 0.0F, 0.9F, 0.17F},  {0.9F, 0.17F, 0.9F, 0.36F},
    {0.9F, 0.36F, 0.65F, 0.52F}, {0.65F, 0.52F, 0.08F, 0.52F}};
constexpr Stroke kQ[]{{0.28F, 0.0F, 0.72F, 0.0F},  {0.72F, 0.0F, 0.92F, 0.2F},
                      {0.92F, 0.2F, 0.92F, 0.8F},  {0.92F, 0.8F, 0.72F, 1.0F},
                      {0.72F, 1.0F, 0.28F, 1.0F},  {0.28F, 1.0F, 0.08F, 0.8F},
                      {0.08F, 0.8F, 0.08F, 0.2F},  {0.08F, 0.2F, 0.28F, 0.0F},
                      {0.58F, 0.68F, 0.98F, 1.08F}};
constexpr Stroke kR[]{{0.08F, 1.0F, 0.08F, 0.0F},  {0.08F, 0.0F, 0.65F, 0.0F},
                      {0.65F, 0.0F, 0.9F, 0.17F},  {0.9F, 0.17F, 0.9F, 0.36F},
                      {0.9F, 0.36F, 0.65F, 0.52F}, {0.65F, 0.52F, 0.08F, 0.52F},
                      {0.57F, 0.52F, 0.96F, 1.0F}};
constexpr Stroke kS[]{
    {0.9F, 0.14F, 0.7F, 0.0F},    {0.7F, 0.0F, 0.27F, 0.0F},
    {0.27F, 0.0F, 0.08F, 0.17F},  {0.08F, 0.17F, 0.08F, 0.38F},
    {0.08F, 0.38F, 0.29F, 0.51F}, {0.29F, 0.51F, 0.72F, 0.51F},
    {0.72F, 0.51F, 0.92F, 0.65F}, {0.92F, 0.65F, 0.92F, 0.84F},
    {0.92F, 0.84F, 0.71F, 1.0F},  {0.71F, 1.0F, 0.27F, 1.0F},
    {0.27F, 1.0F, 0.07F, 0.86F}};
constexpr Stroke kT[]{{0.05F, 0.0F, 0.95F, 0.0F}, {0.5F, 0.0F, 0.5F, 1.0F}};
constexpr Stroke kU[]{{0.08F, 0.0F, 0.08F, 0.8F},
                      {0.08F, 0.8F, 0.27F, 1.0F},
                      {0.27F, 1.0F, 0.73F, 1.0F},
                      {0.73F, 1.0F, 0.92F, 0.8F},
                      {0.92F, 0.8F, 0.92F, 0.0F}};
constexpr Stroke kV[]{{0.05F, 0.0F, 0.5F, 1.0F}, {0.5F, 1.0F, 0.95F, 0.0F}};
constexpr Stroke kW[]{{0.03F, 0.0F, 0.22F, 1.0F},
                      {0.22F, 1.0F, 0.5F, 0.55F},
                      {0.5F, 0.55F, 0.78F, 1.0F},
                      {0.78F, 1.0F, 0.97F, 0.0F}};
constexpr Stroke kX[]{{0.06F, 0.0F, 0.94F, 1.0F}, {0.94F, 0.0F, 0.06F, 1.0F}};
constexpr Stroke kY[]{{0.05F, 0.0F, 0.5F, 0.52F},
                      {0.95F, 0.0F, 0.5F, 0.52F},
                      {0.5F, 0.52F, 0.5F, 1.0F}};
constexpr Stroke kZ[]{{0.06F, 0.0F, 0.94F, 0.0F},
                      {0.94F, 0.0F, 0.06F, 1.0F},
                      {0.06F, 1.0F, 0.94F, 1.0F}};

constexpr Stroke k0[]{{0.28F, 0.0F, 0.72F, 0.0F},  {0.72F, 0.0F, 0.9F, 0.2F},
                      {0.9F, 0.2F, 0.9F, 0.8F},    {0.9F, 0.8F, 0.72F, 1.0F},
                      {0.72F, 1.0F, 0.28F, 1.0F},  {0.28F, 1.0F, 0.1F, 0.8F},
                      {0.1F, 0.8F, 0.1F, 0.2F},    {0.1F, 0.2F, 0.28F, 0.0F},
                      {0.25F, 0.82F, 0.75F, 0.18F}};
constexpr Stroke k1[]{{0.25F, 0.22F, 0.5F, 0.0F},
                      {0.5F, 0.0F, 0.5F, 1.0F},
                      {0.22F, 1.0F, 0.8F, 1.0F}};
constexpr Stroke k2[]{{0.1F, 0.2F, 0.28F, 0.0F}, {0.28F, 0.0F, 0.72F, 0.0F},
                      {0.72F, 0.0F, 0.9F, 0.2F}, {0.9F, 0.2F, 0.9F, 0.36F},
                      {0.9F, 0.36F, 0.1F, 1.0F}, {0.1F, 1.0F, 0.94F, 1.0F}};
constexpr Stroke k3[]{
    {0.1F, 0.12F, 0.29F, 0.0F},   {0.29F, 0.0F, 0.72F, 0.0F},
    {0.72F, 0.0F, 0.91F, 0.18F},  {0.91F, 0.18F, 0.91F, 0.36F},
    {0.91F, 0.36F, 0.68F, 0.5F},  {0.68F, 0.5F, 0.91F, 0.64F},
    {0.91F, 0.64F, 0.91F, 0.82F}, {0.91F, 0.82F, 0.72F, 1.0F},
    {0.72F, 1.0F, 0.28F, 1.0F},   {0.28F, 1.0F, 0.08F, 0.87F}};
constexpr Stroke k4[]{{0.73F, 1.0F, 0.73F, 0.0F},
                      {0.73F, 0.0F, 0.08F, 0.68F},
                      {0.08F, 0.68F, 0.94F, 0.68F}};
constexpr Stroke k5[]{
    {0.91F, 0.0F, 0.15F, 0.0F},   {0.15F, 0.0F, 0.15F, 0.48F},
    {0.15F, 0.48F, 0.72F, 0.48F}, {0.72F, 0.48F, 0.91F, 0.65F},
    {0.91F, 0.65F, 0.91F, 0.82F}, {0.91F, 0.82F, 0.72F, 1.0F},
    {0.72F, 1.0F, 0.28F, 1.0F},   {0.28F, 1.0F, 0.08F, 0.87F}};
constexpr Stroke k6[]{{0.87F, 0.1F, 0.7F, 0.0F},  {0.7F, 0.0F, 0.3F, 0.0F},
                      {0.3F, 0.0F, 0.1F, 0.22F},  {0.1F, 0.22F, 0.1F, 0.8F},
                      {0.1F, 0.8F, 0.3F, 1.0F},   {0.3F, 1.0F, 0.7F, 1.0F},
                      {0.7F, 1.0F, 0.9F, 0.8F},   {0.9F, 0.8F, 0.9F, 0.62F},
                      {0.9F, 0.62F, 0.7F, 0.45F}, {0.7F, 0.45F, 0.1F, 0.45F}};
constexpr Stroke k7[]{{0.08F, 0.0F, 0.94F, 0.0F}, {0.94F, 0.0F, 0.32F, 1.0F}};
constexpr Stroke k8[]{{0.28F, 0.0F, 0.72F, 0.0F}, {0.72F, 0.0F, 0.9F, 0.18F},
                      {0.9F, 0.18F, 0.9F, 0.34F}, {0.9F, 0.34F, 0.7F, 0.5F},
                      {0.7F, 0.5F, 0.3F, 0.5F},   {0.3F, 0.5F, 0.1F, 0.34F},
                      {0.1F, 0.34F, 0.1F, 0.18F}, {0.1F, 0.18F, 0.28F, 0.0F},
                      {0.3F, 0.5F, 0.1F, 0.66F},  {0.1F, 0.66F, 0.1F, 0.82F},
                      {0.1F, 0.82F, 0.28F, 1.0F}, {0.28F, 1.0F, 0.72F, 1.0F},
                      {0.72F, 1.0F, 0.9F, 0.82F}, {0.9F, 0.82F, 0.9F, 0.66F},
                      {0.9F, 0.66F, 0.7F, 0.5F}};
constexpr Stroke k9[]{{0.9F, 0.55F, 0.3F, 0.55F}, {0.3F, 0.55F, 0.1F, 0.38F},
                      {0.1F, 0.38F, 0.1F, 0.2F},  {0.1F, 0.2F, 0.3F, 0.0F},
                      {0.3F, 0.0F, 0.7F, 0.0F},   {0.7F, 0.0F, 0.9F, 0.2F},
                      {0.9F, 0.2F, 0.9F, 0.78F},  {0.9F, 0.78F, 0.7F, 1.0F},
                      {0.7F, 1.0F, 0.3F, 1.0F},   {0.3F, 1.0F, 0.13F, 0.9F}};

constexpr Stroke kHyphen[]{{0.08F, 0.52F, 0.92F, 0.52F}};
constexpr Stroke kSlash[]{{0.08F, 1.0F, 0.92F, 0.0F}};
constexpr Stroke kBackslash[]{{0.08F, 0.0F, 0.92F, 1.0F}};
constexpr Stroke kPeriod[]{{0.48F, 0.94F, 0.52F, 0.94F}};
constexpr Stroke kComma[]{{0.52F, 0.89F, 0.42F, 1.08F}};
constexpr Stroke kColon[]{{0.48F, 0.28F, 0.52F, 0.28F},
                          {0.48F, 0.78F, 0.52F, 0.78F}};
constexpr Stroke kSemicolon[]{{0.48F, 0.28F, 0.52F, 0.28F},
                              {0.52F, 0.72F, 0.42F, 1.02F}};
constexpr Stroke kApostrophe[]{{0.52F, 0.0F, 0.44F, 0.24F}};
constexpr Stroke kQuote[]{{0.32F, 0.0F, 0.26F, 0.23F},
                          {0.7F, 0.0F, 0.64F, 0.23F}};
constexpr Stroke kExclamation[]{{0.5F, 0.0F, 0.5F, 0.72F},
                                {0.48F, 0.94F, 0.52F, 0.94F}};
constexpr Stroke kQuestion[]{
    {0.12F, 0.2F, 0.3F, 0.0F},   {0.3F, 0.0F, 0.68F, 0.0F},
    {0.68F, 0.0F, 0.88F, 0.18F}, {0.88F, 0.18F, 0.88F, 0.35F},
    {0.88F, 0.35F, 0.5F, 0.58F}, {0.5F, 0.58F, 0.5F, 0.72F},
    {0.48F, 0.94F, 0.52F, 0.94F}};
constexpr Stroke kPlus[]{{0.08F, 0.5F, 0.92F, 0.5F}, {0.5F, 0.1F, 0.5F, 0.9F}};
constexpr Stroke kEquals[]{{0.12F, 0.36F, 0.88F, 0.36F},
                           {0.12F, 0.66F, 0.88F, 0.66F}};
constexpr Stroke kUnderscore[]{{0.05F, 1.05F, 0.95F, 1.05F}};
constexpr Stroke kLeftParen[]{{0.65F, 0.0F, 0.38F, 0.18F},
                              {0.38F, 0.18F, 0.3F, 0.5F},
                              {0.3F, 0.5F, 0.38F, 0.82F},
                              {0.38F, 0.82F, 0.65F, 1.0F}};
constexpr Stroke kRightParen[]{{0.35F, 0.0F, 0.62F, 0.18F},
                               {0.62F, 0.18F, 0.7F, 0.5F},
                               {0.7F, 0.5F, 0.62F, 0.82F},
                               {0.62F, 0.82F, 0.35F, 1.0F}};
constexpr Stroke kLeftBracket[]{{0.68F, 0.0F, 0.32F, 0.0F},
                                {0.32F, 0.0F, 0.32F, 1.0F},
                                {0.32F, 1.0F, 0.68F, 1.0F}};
constexpr Stroke kRightBracket[]{{0.32F, 0.0F, 0.68F, 0.0F},
                                 {0.68F, 0.0F, 0.68F, 1.0F},
                                 {0.68F, 1.0F, 0.32F, 1.0F}};
constexpr Stroke kHash[]{{0.3F, 0.0F, 0.22F, 1.0F},
                         {0.72F, 0.0F, 0.64F, 1.0F},
                         {0.08F, 0.36F, 0.9F, 0.36F},
                         {0.04F, 0.68F, 0.86F, 0.68F}};
constexpr Stroke kAmpersand[]{
    {0.84F, 0.28F, 0.7F, 0.0F},   {0.7F, 0.0F, 0.37F, 0.0F},
    {0.37F, 0.0F, 0.2F, 0.18F},   {0.2F, 0.18F, 0.2F, 0.34F},
    {0.2F, 0.34F, 0.86F, 1.0F},   {0.86F, 1.0F, 0.7F, 0.74F},
    {0.7F, 0.74F, 0.42F, 1.0F},   {0.42F, 1.0F, 0.18F, 0.82F},
    {0.18F, 0.82F, 0.18F, 0.66F}, {0.18F, 0.66F, 0.86F, 0.23F}};

template <std::size_t Size>
[[nodiscard]] constexpr Glyph MakeGlyph(const Stroke (&strokes)[Size],
                                        const float advance = 1.0F) noexcept {
  return Glyph{.strokes = strokes, .advance = advance};
}

[[nodiscard]] Glyph FindGlyph(char character) noexcept {
  if (character >= 'a' && character <= 'z')
    character = static_cast<char>(character - 'a' + 'A');
  switch (character) {
  case 'A':
    return MakeGlyph(kA);
  case 'B':
    return MakeGlyph(kB);
  case 'C':
    return MakeGlyph(kC);
  case 'D':
    return MakeGlyph(kD);
  case 'E':
    return MakeGlyph(kE);
  case 'F':
    return MakeGlyph(kF);
  case 'G':
    return MakeGlyph(kG);
  case 'H':
    return MakeGlyph(kH);
  case 'I':
    return MakeGlyph(kI, 0.72F);
  case 'J':
    return MakeGlyph(kJ);
  case 'K':
    return MakeGlyph(kK);
  case 'L':
    return MakeGlyph(kL);
  case 'M':
    return MakeGlyph(kM, 1.16F);
  case 'N':
    return MakeGlyph(kN);
  case 'O':
    return MakeGlyph(kO);
  case 'P':
    return MakeGlyph(kP);
  case 'Q':
    return MakeGlyph(kQ);
  case 'R':
    return MakeGlyph(kR);
  case 'S':
    return MakeGlyph(kS);
  case 'T':
    return MakeGlyph(kT);
  case 'U':
    return MakeGlyph(kU);
  case 'V':
    return MakeGlyph(kV);
  case 'W':
    return MakeGlyph(kW, 1.18F);
  case 'X':
    return MakeGlyph(kX);
  case 'Y':
    return MakeGlyph(kY);
  case 'Z':
    return MakeGlyph(kZ);
  case '0':
    return MakeGlyph(k0);
  case '1':
    return MakeGlyph(k1, 0.8F);
  case '2':
    return MakeGlyph(k2);
  case '3':
    return MakeGlyph(k3);
  case '4':
    return MakeGlyph(k4);
  case '5':
    return MakeGlyph(k5);
  case '6':
    return MakeGlyph(k6);
  case '7':
    return MakeGlyph(k7);
  case '8':
    return MakeGlyph(k8);
  case '9':
    return MakeGlyph(k9);
  case '-':
    return MakeGlyph(kHyphen, 0.8F);
  case '/':
    return MakeGlyph(kSlash, 0.82F);
  case '\\':
    return MakeGlyph(kBackslash, 0.82F);
  case '.':
    return MakeGlyph(kPeriod, 0.48F);
  case ',':
    return MakeGlyph(kComma, 0.48F);
  case ':':
    return MakeGlyph(kColon, 0.5F);
  case ';':
    return MakeGlyph(kSemicolon, 0.5F);
  case '\'':
    return MakeGlyph(kApostrophe, 0.45F);
  case '"':
    return MakeGlyph(kQuote, 0.62F);
  case '!':
    return MakeGlyph(kExclamation, 0.5F);
  case '?':
    return MakeGlyph(kQuestion);
  case '+':
    return MakeGlyph(kPlus);
  case '=':
    return MakeGlyph(kEquals);
  case '_':
    return MakeGlyph(kUnderscore);
  case '(':
    return MakeGlyph(kLeftParen, 0.62F);
  case ')':
    return MakeGlyph(kRightParen, 0.62F);
  case '[':
    return MakeGlyph(kLeftBracket, 0.62F);
  case ']':
    return MakeGlyph(kRightBracket, 0.62F);
  case '#':
    return MakeGlyph(kHash);
  case '&':
    return MakeGlyph(kAmpersand);
  case ' ':
    return Glyph{.strokes = {}, .advance = 0.72F};
  default:
    return MakeGlyph(kQuestion);
  }
}

[[nodiscard]] std::uint8_t ChannelAt(const DebugImage &image,
                                     const std::size_t offset) noexcept {
  return std::to_integer<std::uint8_t>(image.rgba8_pixels[offset]);
}

void SetOpaquePixel(DebugImage &image, const std::uint32_t x,
                    const std::uint32_t y, const Color color) noexcept {
  const std::size_t offset =
      (static_cast<std::size_t>(y) * image.width + x) * kChannelsPerPixel;
  image.rgba8_pixels[offset] = std::byte{color.red};
  image.rgba8_pixels[offset + 1U] = std::byte{color.green};
  image.rgba8_pixels[offset + 2U] = std::byte{color.blue};
  image.rgba8_pixels[offset + 3U] = std::byte{255U};
}

// All destinations are opaque. Coverage and straight source alpha are combined
// before one bounded integer source-over operation, so partially covered
// strokes cannot leave hidden RGB or invalid alpha behind for a later GPU
// upload.
void BlendPixel(DebugImage &image, const int x, const int y, const Color color,
                const int covered_samples = kCoverageSamples) noexcept {
  if (x < 0 || y < 0 || x >= static_cast<int>(image.width) ||
      y >= static_cast<int>(image.height) || covered_samples <= 0)
    return;

  const std::uint32_t samples = static_cast<std::uint32_t>(
      std::clamp(covered_samples, 0, kCoverageSamples));
  const std::uint32_t effective_alpha =
      (static_cast<std::uint32_t>(color.alpha) * samples +
       kCoverageSamples / 2U) /
      kCoverageSamples;
  const std::size_t offset = (static_cast<std::size_t>(y) * image.width +
                              static_cast<std::uint32_t>(x)) *
                             kChannelsPerPixel;
  const std::array source{color.red, color.green, color.blue};
  for (std::size_t channel = 0; channel < source.size(); ++channel) {
    const std::uint32_t destination = ChannelAt(image, offset + channel);
    const std::uint32_t blended =
        (static_cast<std::uint32_t>(source[channel]) * effective_alpha +
         destination * (255U - effective_alpha) + 127U) /
        255U;
    image.rgba8_pixels[offset + channel] =
        std::byte{static_cast<std::uint8_t>(blended)};
  }
  image.rgba8_pixels[offset + 3U] = std::byte{255U};
}

void FillBackground(DebugImage &image) noexcept {
  for (std::uint32_t y = 0; y < image.height; ++y) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const std::uint32_t edge_x = std::min(x, image.width - 1U - x);
      const std::uint32_t edge_y = std::min(y, image.height - 1U - y);
      const std::uint32_t edge = std::min(edge_x, edge_y);
      const std::uint32_t vignette = edge < 120U ? (120U - edge) / 24U : 0U;
      const bool major_grid = x % 96U == 0U || y % 96U == 0U;
      const bool minor_grid = x % 48U == 0U || y % 48U == 0U;
      const std::uint32_t grid = major_grid ? 4U : (minor_grid ? 2U : 0U);
      const std::uint32_t vertical = y * 5U / image.height;
      SetOpaquePixel(
          image, x, y,
          Color{
              .red = static_cast<std::uint8_t>(5U + vertical + grid),
              .green = static_cast<std::uint8_t>(13U + vertical + grid -
                                                 std::min(grid, vignette)),
              .blue = static_cast<std::uint8_t>(19U + vertical + 2U * grid -
                                                std::min(2U * grid, vignette)),
          });
    }
  }
}

void FillRect(DebugImage &image, const int x, const int y, const int width,
              const int height, const Color color) noexcept {
  if (width <= 0 || height <= 0)
    return;
  const int left = std::clamp(x, 0, static_cast<int>(image.width));
  const int top = std::clamp(y, 0, static_cast<int>(image.height));
  const std::int64_t raw_right = static_cast<std::int64_t>(x) + width;
  const std::int64_t raw_bottom = static_cast<std::int64_t>(y) + height;
  const int right = static_cast<int>(std::clamp<std::int64_t>(
      raw_right, 0, static_cast<std::int64_t>(image.width)));
  const int bottom = static_cast<int>(std::clamp<std::int64_t>(
      raw_bottom, 0, static_cast<std::int64_t>(image.height)));
  for (int row = top; row < bottom; ++row) {
    for (int column = left; column < right; ++column)
      BlendPixel(image, column, row, color);
  }
}

void StrokeRect(DebugImage &image, const int x, const int y, const int width,
                const int height, const Color color) noexcept {
  if (width <= 1 || height <= 1)
    return;
  FillRect(image, x, y, width, 1, color);
  FillRect(image, x, y + height - 1, width, 1, color);
  FillRect(image, x, y, 1, height, color);
  FillRect(image, x + width - 1, y, 1, height, color);
}

[[nodiscard]] float SegmentDistanceSquared(const float x, const float y,
                                           const float x0, const float y0,
                                           const float x1,
                                           const float y1) noexcept {
  const float delta_x = x1 - x0;
  const float delta_y = y1 - y0;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  if (length_squared <= std::numeric_limits<float>::epsilon()) {
    const float point_x = x - x0;
    const float point_y = y - y0;
    return point_x * point_x + point_y * point_y;
  }
  const float projection = std::clamp(
      ((x - x0) * delta_x + (y - y0) * delta_y) / length_squared, 0.0F, 1.0F);
  const float nearest_x = x0 + projection * delta_x;
  const float nearest_y = y0 + projection * delta_y;
  const float distance_x = x - nearest_x;
  const float distance_y = y - nearest_y;
  return distance_x * distance_x + distance_y * distance_y;
}

void DrawAntialiasedStroke(DebugImage &image, const float x0, const float y0,
                           const float x1, const float y1, const float radius,
                           const Color color) noexcept {
  const int raw_left =
      static_cast<int>(std::floor(std::min(x0, x1) - radius - 1.0F));
  const int raw_top =
      static_cast<int>(std::floor(std::min(y0, y1) - radius - 1.0F));
  const int raw_right =
      static_cast<int>(std::ceil(std::max(x0, x1) + radius + 1.0F));
  const int raw_bottom =
      static_cast<int>(std::ceil(std::max(y0, y1) + radius + 1.0F));
  const int left = std::clamp(raw_left, 0, static_cast<int>(image.width));
  const int top = std::clamp(raw_top, 0, static_cast<int>(image.height));
  const int right = std::clamp(raw_right, 0, static_cast<int>(image.width));
  const int bottom = std::clamp(raw_bottom, 0, static_cast<int>(image.height));
  const float radius_squared = radius * radius;
  constexpr float reciprocal_samples =
      1.0F / static_cast<float>(kCoverageAxisSamples);
  for (int y = top; y < bottom; ++y) {
    for (int x = left; x < right; ++x) {
      int covered = 0;
      for (int sample_y = 0; sample_y < kCoverageAxisSamples; ++sample_y) {
        const float position_y =
            static_cast<float>(y) +
            (static_cast<float>(sample_y) + 0.5F) * reciprocal_samples;
        for (int sample_x = 0; sample_x < kCoverageAxisSamples; ++sample_x) {
          const float position_x =
              static_cast<float>(x) +
              (static_cast<float>(sample_x) + 0.5F) * reciprocal_samples;
          if (SegmentDistanceSquared(position_x, position_y, x0, y0, x1, y1) <=
              radius_squared)
            ++covered;
        }
      }
      BlendPixel(image, x, y, color, covered);
    }
  }
}

struct TextStyle {
  float height = 16.0F;
  float stroke_radius = 0.75F;
  float tracking = 0.1F;
  Color color = kIce;
};

[[nodiscard]] float GlyphWidth(const TextStyle &style) noexcept {
  return style.height * 0.56F;
}

[[nodiscard]] float MeasureText(const std::string_view text,
                                const TextStyle &style) noexcept {
  float width = 0.0F;
  const float glyph_width = GlyphWidth(style);
  for (const char character : text) {
    const Glyph glyph = FindGlyph(character);
    width += glyph_width * glyph.advance + style.height * style.tracking;
  }
  if (!text.empty())
    width -= style.height * style.tracking;
  return width;
}

void DrawText(DebugImage &image, const std::string_view text, const float x,
              const float y, const TextStyle &style) noexcept {
  float cursor = x;
  const float glyph_width = GlyphWidth(style);
  for (const char character : text) {
    const Glyph glyph = FindGlyph(character);
    for (const Stroke &stroke : glyph.strokes) {
      DrawAntialiasedStroke(
          image, cursor + stroke.x0 * glyph_width, y + stroke.y0 * style.height,
          cursor + stroke.x1 * glyph_width, y + stroke.y1 * style.height,
          style.stroke_radius, style.color);
    }
    cursor += glyph_width * glyph.advance + style.height * style.tracking;
  }
}

void DrawCenteredText(DebugImage &image, const std::string_view text,
                      const float center_x, const float y,
                      const TextStyle &style) noexcept {
  DrawText(image, text, center_x - MeasureText(text, style) * 0.5F, y, style);
}

void DrawCornerMarks(DebugImage &image) noexcept {
  constexpr Color marks{57U, 98U, 110U, 145U};
  FillRect(image, 48, 44, 72, 1, marks);
  FillRect(image, 48, 44, 1, 28, marks);
  FillRect(image, 840, 44, 72, 1, marks);
  FillRect(image, 911, 44, 1, 28, marks);
  FillRect(image, 48, 468, 1, 28, marks);
  FillRect(image, 48, 495, 72, 1, marks);
  FillRect(image, 911, 468, 1, 28, marks);
  FillRect(image, 840, 495, 72, 1, marks);
}

void DrawChrome(DebugImage &image, const std::string_view section,
                const std::string_view index) noexcept {
  DrawCornerMarks(image);
  FillRect(image, 72, 52, 816, 1, Color{58U, 91U, 99U, 125U});
  DrawText(image, "OPENOMEGA / OPERATIONS TERMINAL", 72.0F, 68.0F,
           TextStyle{.height = 12.0F,
                     .stroke_radius = 0.62F,
                     .tracking = 0.16F,
                     .color = kIceDim});
  DrawText(image, section, 72.0F, 91.0F,
           TextStyle{.height = 13.0F,
                     .stroke_radius = 0.7F,
                     .tracking = 0.14F,
                     .color = kIce});
  const TextStyle index_style{.height = 12.0F,
                              .stroke_radius = 0.62F,
                              .tracking = 0.12F,
                              .color = kIceDim};
  DrawText(image, index, 888.0F - MeasureText(index, index_style), 68.0F,
           index_style);
  FillRect(image, 72, 475, 816, 1, Color{58U, 91U, 99U, 110U});
}

void DrawMenuRow(DebugImage &image, const std::string_view label, const int x,
                 const int y, const int width, const bool selected) noexcept {
  if (selected) {
    FillRect(image, x, y, width, 38, Color{30U, 39U, 38U, 178U});
    FillRect(image, x, y, 3, 38, kAmber);
    FillRect(image, x + 14, y + 37, width - 14, 1, kAmberDim);
  } else {
    FillRect(image, x + 14, y + 37, width - 14, 1, Color{39U, 64U, 72U, 115U});
  }
  DrawText(image, label, static_cast<float>(x + 22), static_cast<float>(y + 8),
           TextStyle{.height = 20.0F,
                     .stroke_radius = 0.9F,
                     .tracking = 0.12F,
                     .color = selected ? kAmber : kIce});
}

void DrawHelp(DebugImage &image, const std::string_view help) noexcept {
  DrawCenteredText(image, help, 480.0F, 493.0F,
                   TextStyle{.height = 12.0F,
                             .stroke_radius = 0.62F,
                             .tracking = 0.13F,
                             .color = kIceDim});
}

void DrawTitleScreen(DebugImage &image,
                     const TacticalMenuImageModel &model) noexcept {
  DrawChrome(image, "SECURE ACCESS", "01 / 04");
  DrawText(image, "OMEGA STRAIN", 92.0F, 148.0F,
           TextStyle{.height = 34.0F,
                     .stroke_radius = 1.35F,
                     .tracking = 0.14F,
                     .color = kIce});
  DrawText(image, "COVERT OPERATIONS NETWORK", 94.0F, 193.0F,
           TextStyle{.height = 13.0F,
                     .stroke_radius = 0.68F,
                     .tracking = 0.19F,
                     .color = kIceDim});

  // Abstract signal plot: decorative, deliberately sparse, and unrelated to
  // retail data.
  constexpr std::array plot{
      std::array{112.0F, 310.0F}, std::array{153.0F, 278.0F},
      std::array{198.0F, 332.0F}, std::array{246.0F, 257.0F},
      std::array{291.0F, 296.0F}, std::array{344.0F, 229.0F},
      std::array{398.0F, 282.0F}};
  for (std::size_t index = 1U; index < plot.size(); ++index) {
    DrawAntialiasedStroke(image, plot[index - 1U][0], plot[index - 1U][1],
                          plot[index][0], plot[index][1], 0.72F, kIceFaint);
  }
  FillRect(image, 94, 354, 306, 1, Color{47U, 76U, 85U, 120U});
  DrawText(image, "LINK STATUS: READY", 94.0F, 369.0F,
           TextStyle{.height = 12.0F,
                     .stroke_radius = 0.62F,
                     .tracking = 0.14F,
                     .color = kIceDim});

  constexpr std::array<std::string_view, 3> rows{"CREATE AGENT", "LOAD AGENT",
                                                 "QUIT"};
  for (std::size_t index = 0U; index < rows.size(); ++index) {
    DrawMenuRow(image, rows[index], 548, 187 + static_cast<int>(index) * 58,
                314, model.selected_row == index);
  }
  DrawHelp(image, "ARROWS / W S  NAVIGATE     ENTER / CLICK  SELECT");
}

void DrawCreateAgentScreen(DebugImage &image,
                           const TacticalMenuImageModel &model) noexcept {
  DrawChrome(image, "FIELD OPERATIVE REGISTRATION", "02 / 04");
  DrawText(image, "CREATE AGENT", 90.0F, 137.0F,
           TextStyle{.height = 32.0F,
                     .stroke_radius = 1.25F,
                     .tracking = 0.14F,
                     .color = kIce});

  if (model.create_agent_presentation ==
      CreateAgentPresentation::Confirmation) {
    FillRect(image, 205, 217, 550, 178, kPanel);
    StrokeRect(image, 205, 217, 550, 178, kPanelEdge);
    DrawCenteredText(image, "CONFIRM AGENT", 480.0F, 246.0F,
                     TextStyle{.height = 23.0F,
                               .stroke_radius = 1.0F,
                               .tracking = 0.14F,
                               .color = kIce});
    DrawCenteredText(image, model.agent_name, 480.0F, 292.0F,
                     TextStyle{.height = 20.0F,
                               .stroke_radius = 0.9F,
                               .tracking = 0.14F,
                               .color = kAmber});
    DrawMenuRow(image, "CREATE AGENT", 286, 338, 186, model.selected_row == 0U);
    DrawMenuRow(image, "RETURN", 488, 338, 186, model.selected_row == 1U);
    DrawHelp(image, "ENTER / CLICK  CONFIRM     ESC  RETURN");
    return;
  }

  FillRect(image, 90, 211, 780, 112, kPanel);
  StrokeRect(image, 90, 211, 780, 112, kPanelEdge);
  DrawText(image, "CALLSIGN", 116.0F, 232.0F,
           TextStyle{.height = 12.0F,
                     .stroke_radius = 0.62F,
                     .tracking = 0.17F,
                     .color = kIceDim});
  const bool empty =
      model.create_agent_presentation == CreateAgentPresentation::Empty;
  DrawText(image, empty ? "ENTER AGENT NAME" : model.agent_name, 116.0F, 267.0F,
           TextStyle{.height = 22.0F,
                     .stroke_radius = 0.95F,
                     .tracking = 0.13F,
                     .color = empty ? kIceDim : kIce});
  FillRect(image, 116, 302, 708, 1, empty ? kIceFaint : kAmberDim);
  if (empty) {
    FillRect(image, 116, 267, 2, 27, kAmber);
    DrawHelp(image, "TYPE TO NAME AGENT     ENTER  CONTINUE     ESC  RETURN");
    return;
  }

  DrawMenuRow(image, "CONTINUE", 265, 364, 210, model.selected_row == 0U);
  DrawMenuRow(image, "BACK", 491, 364, 210, model.selected_row == 1U);
  DrawHelp(image, "ARROWS / W S  NAVIGATE     ENTER / CLICK  SELECT");
}

void DrawLoadAgentScreen(DebugImage &image,
                         const TacticalMenuImageModel &model) noexcept {
  DrawChrome(image, "PERSONNEL ARCHIVE", "03 / 04");
  DrawText(image, "LOAD AGENT", 90.0F, 137.0F,
           TextStyle{.height = 32.0F,
                     .stroke_radius = 1.25F,
                     .tracking = 0.14F,
                     .color = kIce});
  DrawText(image, "LOCAL AGENT RECORDS", 92.0F, 181.0F,
           TextStyle{.height = 12.0F,
                     .stroke_radius = 0.62F,
                     .tracking = 0.17F,
                     .color = kIceDim});

  if (model.saved_agent_count == 0U) {
    FillRect(image, 90, 217, 780, 92, kPanel);
    StrokeRect(image, 90, 217, 780, 92, kPanelEdge);
    DrawCenteredText(image, "NO AGENTS ON FILE", 480.0F, 253.0F,
                     TextStyle{.height = 18.0F,
                               .stroke_radius = 0.82F,
                               .tracking = 0.15F,
                               .color = kIceDim});
  } else {
    for (std::size_t index = 0U; index < model.saved_agent_count; ++index) {
      const int y = 211 + static_cast<int>(index) * 55;
      DrawText(
          image, index == 0U ? "01" : (index == 1U ? "02" : "03"), 112.0F,
          static_cast<float>(y + 10),
          TextStyle{.height = 13.0F,
                    .stroke_radius = 0.67F,
                    .tracking = 0.12F,
                    .color = model.selected_row == index ? kAmber : kIceDim});
      DrawMenuRow(image, model.saved_agent_labels[index], 154, y, 650,
                  model.selected_row == index);
    }
  }
  DrawMenuRow(image, "BACK", 620, 400, 184,
              model.selected_row == model.saved_agent_count);
  DrawHelp(image, "ARROWS / W S  NAVIGATE     ENTER / CLICK  LOAD");
}

void DrawBriefingScreen(DebugImage &image,
                        const TacticalMenuImageModel &model) noexcept {
  DrawChrome(image, "OPERATIONS ROOM", "04 / 04");
  DrawText(image, "MISSION BRIEFING", 90.0F, 137.0F,
           TextStyle{.height = 32.0F,
                     .stroke_radius = 1.25F,
                     .tracking = 0.14F,
                     .color = kIce});
  FillRect(image, 90, 217, 780, 116, kPanel);
  StrokeRect(image, 90, 217, 780, 116, kPanelEdge);
  DrawText(image, "CURRENT ASSIGNMENT", 116.0F, 240.0F,
           TextStyle{.height = 12.0F,
                     .stroke_radius = 0.62F,
                     .tracking = 0.17F,
                     .color = kIceDim});
  DrawText(image,
           model.mission_label.empty() ? "NO MISSION SELECTED"
                                       : model.mission_label,
           116.0F, 277.0F,
           TextStyle{.height = 21.0F,
                     .stroke_radius = 0.92F,
                     .tracking = 0.13F,
                     .color = model.mission_label.empty() ? kIceDim : kIce});
  DrawMenuRow(image, "ENTER BRIEFING", 248, 374, 245, model.selected_row == 0U);
  DrawMenuRow(image, "RETURN", 510, 374, 202, model.selected_row == 1U);
  DrawHelp(image, "ENTER / CLICK  SELECT     ESC  RETURN");
}

[[nodiscard]] std::expected<void, std::string>
ValidateLabel(const std::string_view label, const std::string_view field,
              const bool allow_empty) {
  if (!allow_empty && label.empty())
    return std::unexpected(std::string(field) + " must not be empty");
  if (label.size() > kTacticalMenuMaximumLabelBytes)
    return std::unexpected(std::string(field) + " exceeds the 24-byte limit");
  for (const unsigned char character : label) {
    if (character < 0x20U || character > 0x7eU)
      return std::unexpected(std::string(field) +
                             " must contain printable ASCII");
  }
  return {};
}

[[nodiscard]] std::expected<void, std::string>
ValidateModel(const TacticalMenuImageModel &model) {
  switch (model.screen) {
  case TacticalMenuScreen::Title:
    if (model.selected_row > 2U)
      return std::unexpected("title selection is out of range");
    return {};
  case TacticalMenuScreen::CreateAgent:
    switch (model.create_agent_presentation) {
    case CreateAgentPresentation::Empty:
      if (model.selected_row != 0U)
        return std::unexpected("empty create-agent selection must be zero");
      return ValidateLabel(model.agent_name, "agent name", true);
    case CreateAgentPresentation::Ready:
    case CreateAgentPresentation::Confirmation:
      if (model.selected_row > 1U)
        return std::unexpected("create-agent selection is out of range");
      return ValidateLabel(model.agent_name, "agent name", false);
    default:
      return std::unexpected("create-agent presentation is invalid");
    }
  case TacticalMenuScreen::LoadAgent:
    if (model.saved_agent_count > kTacticalMenuMaximumSavedAgents)
      return std::unexpected("saved-agent count exceeds three entries");
    if (model.selected_row > model.saved_agent_count)
      return std::unexpected("load-agent selection is out of range");
    for (std::size_t index = 0U; index < model.saved_agent_count; ++index) {
      auto validated = ValidateLabel(model.saved_agent_labels[index],
                                     "saved-agent label", false);
      if (!validated)
        return validated;
    }
    return {};
  case TacticalMenuScreen::Briefing:
    if (model.selected_row > 1U)
      return std::unexpected("briefing selection is out of range");
    return ValidateLabel(model.mission_label, "mission label", true);
  default:
    return std::unexpected("tactical menu screen is invalid");
  }
}
} // namespace

std::expected<DebugImage, std::string>
BuildTacticalMenuImage(const TacticalMenuImageModel &model) {
  static_assert(kTacticalMenuImageWidth != 0U &&
                kTacticalMenuImageHeight != 0U);
  static_assert(static_cast<std::size_t>(kTacticalMenuImageWidth) <=
                std::numeric_limits<std::size_t>::max() /
                    kTacticalMenuImageHeight / kChannelsPerPixel);
  auto validated = ValidateModel(model);
  if (!validated)
    return std::unexpected(validated.error());

  DebugImage image{
      .width = kTacticalMenuImageWidth,
      .height = kTacticalMenuImageHeight,
      .rgba8_pixels = std::vector<std::byte>(kMenuImageBytes),
  };
  FillBackground(image);
  switch (model.screen) {
  case TacticalMenuScreen::Title:
    DrawTitleScreen(image, model);
    break;
  case TacticalMenuScreen::CreateAgent:
    DrawCreateAgentScreen(image, model);
    break;
  case TacticalMenuScreen::LoadAgent:
    DrawLoadAgentScreen(image, model);
    break;
  case TacticalMenuScreen::Briefing:
    DrawBriefingScreen(image, model);
    break;
  default:
    return std::unexpected("tactical menu screen is invalid");
  }
  return image;
}
} // namespace omega::runtime
