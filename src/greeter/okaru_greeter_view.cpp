#include "greeter/okaru_greeter_view.h"

#include "core/resource_paths.h"
#include "render/core/color.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/scene/image_node.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "ui/controls/box.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/palette.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace greeter {

  namespace {

    // Igual que okaru_lock_view.cpp de la shell.
    constexpr Color kText = rgba(1.0f, 1.0f, 1.0f, 0.94f);
    constexpr Color kMuted = rgba(1.0f, 1.0f, 1.0f, 0.62f);
    constexpr Color kFaint = rgba(1.0f, 1.0f, 1.0f, 0.38f);
    constexpr Color kVeil = rgba(0.0f, 0.0f, 0.0f, 0.55f);
    constexpr Color kFieldFill = rgba(0.0f, 0.0f, 0.0f, 0.35f);
    constexpr const char* kPixelFont = "BigBlueTermPlus Nerd Font"; // paquete ttf-bigblueterminal-nerd
    constexpr float kLogoAspect = 647.3f / 200.0f;                  // assets/okaru-logo.svg (de okaru/tools)
    constexpr std::size_t kMaxDots = 24;

    // Desenfoque de caja en tres pasadas (el mismo que Image::setSourceFileBlurred de la shell).
    void boxBlurPass(
        const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& dst, int w, int h, int r, bool horizontal
    ) {
      const int lines = horizontal ? h : w;
      const int length = horizontal ? w : h;
      const auto at = [&](int line, int pos, int channel) {
        const int x = horizontal ? pos : line;
        const int y = horizontal ? line : pos;
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4U
            + static_cast<std::size_t>(channel);
      };
      const auto diameter = static_cast<std::uint32_t>(r * 2 + 1);
      for (int line = 0; line < lines; ++line) {
        for (int channel = 0; channel < 4; ++channel) {
          std::uint32_t sum = 0;
          for (int offset = -r; offset <= r; ++offset) {
            sum += src[at(line, std::clamp(offset, 0, length - 1), channel)];
          }
          for (int pos = 0; pos < length; ++pos) {
            dst[at(line, pos, channel)] = static_cast<std::uint8_t>((sum + diameter / 2U) / diameter);
            sum = sum
                - src[at(line, std::clamp(pos - r, 0, length - 1), channel)]
                + src[at(line, std::clamp(pos + r + 1, 0, length - 1), channel)];
          }
        }
      }
    }

    std::string hexOf(const Color& c) {
      const auto ch = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
      return std::format("#{:02x}{:02x}{:02x}", ch(c.r), ch(c.g), ch(c.b));
    }

    // Copia de un SVG de assets con un relleno cambiado de color, en el directorio temporal del usuario greeter.
    std::string tintedAsset(const std::string& asset, std::string_view fill, const std::string& hex) {
      std::ifstream in(paths::assetPath(asset));
      std::stringstream buffer;
      buffer << in.rdbuf();
      std::string svg = buffer.str();
      if (svg.empty()) {
        return {};
      }
      if (const auto pos = svg.find(fill); pos != std::string::npos) {
        std::string replaced(fill);
        replaced.replace(replaced.size() - 8, 7, hex); // fill="#rrggbb": el color son los 7 antes de la comilla
        svg.replace(pos, fill.size(), replaced);
      }
      const char* runtime = std::getenv("XDG_RUNTIME_DIR");
      const std::filesystem::path dir = runtime != nullptr && runtime[0] != '\0'
          ? std::filesystem::path(runtime)
          : std::filesystem::temp_directory_path();
      const auto path = dir
          / std::format("okaru-greeter-{}-{}-{}.svg", getuid(), std::filesystem::path(asset).stem().string(),
                        hex.substr(1));
      std::ofstream(path, std::ios::trunc) << svg;
      return path.string();
    }

    Label* addLabel(Node& root, std::int32_t z) {
      auto label = std::make_unique<Label>();
      label->setFontFamily(kPixelFont);
      label->setMaxLines(1);
      label->setZIndex(z);
      return static_cast<Label*>(root.addChild(std::move(label)));
    }

    // Coloca una etiqueta centrada en x dentro de [x, x + width]; devuelve su alto.
    float placeCentered(Renderer& renderer, Label& label, float x, float width, float y) {
      label.measure(renderer);
      label.setPosition(std::round(x + (width - label.width()) * 0.5f), std::round(y));
      return label.height();
    }

  } // namespace

  std::string okaruPasswordPlaceholder() {
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
      if (const char* value = std::getenv(var); value != nullptr && value[0] != '\0') {
        return std::string_view(value).starts_with("es") ? "Introduce la contraseña" : "Enter password";
      }
    }
    return "Enter password";
  }

  std::optional<LoadedImageFile> loadBlurredWallpaper(const std::string& path) {
    // Reducido a 384 px: el blur es barato y, estirado a pantalla completa, muy fuerte.
    auto loaded = loadImageFile(path, 384);
    if (!loaded.has_value() || loaded->width <= 0 || loaded->height <= 0) {
      return std::nullopt;
    }
    std::vector<std::uint8_t> scratch(loaded->rgba.size());
    for (int pass = 0; pass < 3; ++pass) {
      boxBlurPass(loaded->rgba, scratch, loaded->width, loaded->height, 6, true);
      boxBlurPass(scratch, loaded->rgba, loaded->width, loaded->height, 6, false);
    }
    return loaded;
  }

  void OkaruGreeterView::build(Node& root) {
    auto veil = std::make_unique<Box>();
    veil->setZIndex(1);
    m_veil = static_cast<Box*>(root.addChild(std::move(veil)));
    auto logo = std::make_unique<ImageNode>();
    logo->setZIndex(3);
    logo->setFitMode(ImageFitMode::Contain);
    m_logo = static_cast<ImageNode*>(root.addChild(std::move(logo)));
    m_user = addLabel(root, 3);
    auto userArea = std::make_unique<InputArea>();
    userArea->setZIndex(4);
    userArea->setOnClick([this](const InputArea::PointerData&) {
      if (m_onNextUser) {
        m_onNextUser();
      }
    });
    m_userArea = static_cast<InputArea*>(root.addChild(std::move(userArea)));
    auto lock = std::make_unique<ImageNode>();
    lock->setZIndex(3);
    lock->setFitMode(ImageFitMode::Contain);
    m_lockIcon = static_cast<ImageNode*>(root.addChild(std::move(lock)));
    auto field = std::make_unique<Box>();
    field->setZIndex(3);
    m_field = static_cast<Box*>(root.addChild(std::move(field)));
    m_fieldText = addLabel(root, 5);
    m_status = addLabel(root, 3);
    auto session = std::make_unique<Box>();
    session->setZIndex(3);
    m_session = static_cast<Box*>(root.addChild(std::move(session)));
    m_sessionLabel = addLabel(root, 4);
    auto sessionArea = std::make_unique<InputArea>();
    sessionArea->setZIndex(5);
    sessionArea->setOnEnter([this](const InputArea::PointerData&) { m_sessionHovered = true; });
    sessionArea->setOnLeave([this] { m_sessionHovered = false; });
    // Clic: siguiente sesión; rueda: anterior o siguiente.
    sessionArea->setOnClick([this](const InputArea::PointerData&) {
      if (m_onNextSession) {
        m_onNextSession();
      }
    });
    sessionArea->setOnAxisHandler([this](const InputArea::PointerData& data) {
      const auto& callback = data.axisValue > 0.0 ? m_onNextSession : m_onPreviousSession;
      if (callback) {
        callback();
      }
      return true;
    });
    m_sessionArea = static_cast<InputArea*>(root.addChild(std::move(sessionArea)));
  }

  void OkaruGreeterView::adoptPasswordField(Input* field) {
    if (field == nullptr || field == m_input) {
      return;
    }
    m_input = field;
    m_input->setZIndex(4);
    // Sin placeholder propio: al enviar la contraseña el valor se vacía y lo mostraba a la izquierda.
    m_input->setPlaceholder("");
    m_input->setOpacity(0.0f);
  }

  void OkaruGreeterView::syncTexture(
      TextureManager& textures, ImageNode* node, TextureHandle& handle, std::string& key, const std::string& asset,
      std::string_view fill, const std::string& hex, int size
  ) {
    const std::string wanted = std::format("{}@{}", hex, size);
    if (wanted == key) {
      return;
    }
    const std::string path = tintedAsset(asset, fill, hex);
    if (path.empty()) {
      return;
    }
    TextureHandle loaded = textures.loadFromFile(path, size, true);
    if (!loaded.valid()) {
      return;
    }
    if (handle.valid()) {
      textures.unload(handle);
    }
    handle = loaded;
    key = wanted;
    node->setTextureId(handle.id);
    node->setTextureSize(handle.width, handle.height);
  }

  void OkaruGreeterView::layout(
      Renderer& renderer, TextureManager& textures, float ox, float oy, float width, float height,
      const OkaruGreeterState& state
  ) {
    const float s = std::clamp(height / 1080.0f, 0.7f, 2.0f); // todo escala con la altura (1080 p = 1)
    const Color accent = colorForRole(ColorRole::Primary);
    const Color error = colorForRole(ColorRole::Error);

    m_veil->setStyle(RoundedRectStyle{.fill = kVeil, .fillMode = FillMode::Solid});
    m_veil->setPosition(ox, oy);
    m_veil->setSize(width, height);

    // Logo: 郁 (acento) + OKARU, con el mismo centrado óptico que el bloqueo (2 % a la izquierda).
    const float logoHeight = std::round(150.0f * s);
    const float logoWidth = std::round(logoHeight * kLogoAspect);
    syncTexture(
        textures, m_logo, m_logoTexture, m_logoKey, "okaru-logo.svg", "id=\"kanji\" fill=\"#7095c8\"", hexOf(accent),
        static_cast<int>(logoWidth)
    );
    const float logoY = std::round(oy + height * 0.47f - logoHeight);
    m_logo->setPosition(std::round(ox + (width - logoWidth) * 0.5f - logoWidth * 0.02f), logoY);
    m_logo->setSize(logoWidth, logoHeight);

    // Usuario (clic para cambiar si hay varios).
    float y = logoY + logoHeight + 48.0f * s;
    m_user->setFontSize(22.0f * s);
    m_user->setColor(kMuted);
    m_user->setText(state.canSwitchUser ? std::format("< {} >", state.user) : state.user);
    const float userH = placeCentered(renderer, *m_user, ox, width, y);
    m_userArea->setVisible(state.canSwitchUser);
    m_userArea->setPosition(m_user->x(), m_user->y());
    m_userArea->setSize(m_user->width(), userH);
    y += userH + 14.0f * s;

    // Campo con borde de acento (rojo si falló) y el candado pixelado a la izquierda.
    const float fieldW = std::round(420.0f * s);
    const float fieldH = std::round(58.0f * s);
    const float fieldX = std::round(ox + (width - fieldW) * 0.5f);
    const float fieldY = std::round(y);
    m_field->setStyle(
        RoundedRectStyle{
            .fill = kFieldFill,
            .border = state.statusError ? error : accent,
            .fillMode = FillMode::Solid,
            .borderWidth = std::max(2.0f, std::round(2.0f * s)),
        }
    );
    m_field->setPosition(fieldX, fieldY);
    m_field->setSize(fieldW, fieldH);
    const float iconSize = std::round(12.0f * std::max(2.0f, std::round(2.0f * s)));
    syncTexture(
        textures, m_lockIcon, m_lockTexture, m_lockKey, "pixel-icons/lock.svg", "fill=\"#ffffff\"",
        hexOf(state.statusError ? error : accent), static_cast<int>(iconSize)
    );
    m_lockIcon->setPosition(fieldX - iconSize - 18.0f * s, std::round(fieldY + (fieldH - iconSize) * 0.5f));
    m_lockIcon->setSize(iconSize, iconSize);
    if (m_input != nullptr) {
      m_input->setOpacity(0.0f);
      m_input->setPosition(fieldX, fieldY);
      m_input->setSize(fieldW, fieldH);
      m_input->layout(renderer);
    }
    m_fieldText->setFontSize(22.0f * s);
    if (state.passwordLength > 0) {
      std::string dots;
      for (std::size_t i = 0; i < std::min(state.passwordLength, kMaxDots); ++i) {
        dots += i == 0 ? "●" : " ●";
      }
      m_fieldText->setColor(kText);
      m_fieldText->setText(dots);
    } else {
      m_fieldText->setColor(kMuted);
      m_fieldText->setText(state.placeholder);
    }
    m_fieldText->setMaxWidth(fieldW - 32.0f * s);
    m_fieldText->measure(renderer);
    m_fieldText->setPosition(
        std::round(fieldX + (fieldW - m_fieldText->width()) * 0.5f),
        std::round(fieldY + (fieldH - m_fieldText->height()) * 0.5f)
    );

    // Estado debajo del campo.
    m_status->setVisible(!state.status.empty());
    if (!state.status.empty()) {
      m_status->setFontSize(15.0f * s);
      m_status->setColor(state.statusError ? error : kMuted);
      m_status->setMaxWidth(width - 64.0f * s);
      m_status->setText(state.status);
      placeCentered(renderer, *m_status, ox, width, fieldY + fieldH + 16.0f * s);
    }

    // Selector de sesión abajo al centro: el recuadro de los botones del bloqueo, con el nombre de la sesión.
    m_sessionLabel->setFontSize(18.0f * s);
    m_sessionLabel->setColor(kText);
    m_sessionLabel->setText(state.canSwitchSession ? std::format("< {} >", state.session) : state.session);
    m_sessionLabel->measure(renderer);
    const float boxW = std::round(m_sessionLabel->width() + 48.0f * s);
    const float boxH = std::round(56.0f * s);
    const float boxX = std::round(ox + (width - boxW) * 0.5f);
    const float boxY = std::round(oy + height - 56.0f * s - boxH);
    m_session->setStyle(
        RoundedRectStyle{
            .fill = m_sessionHovered ? rgba(1.0f, 1.0f, 1.0f, 0.14f) : rgba(1.0f, 1.0f, 1.0f, 0.0f),
            .border = m_sessionHovered ? kMuted : kFaint,
            .fillMode = FillMode::Solid,
            .borderWidth = 1.0f,
        }
    );
    m_session->setPosition(boxX, boxY);
    m_session->setSize(boxW, boxH);
    m_sessionLabel->setPosition(
        std::round(boxX + (boxW - m_sessionLabel->width()) * 0.5f),
        std::round(boxY + (boxH - m_sessionLabel->height()) * 0.5f)
    );
    m_sessionArea->setVisible(state.canSwitchSession);
    m_sessionArea->setPosition(boxX, boxY);
    m_sessionArea->setSize(boxW, boxH);
  }

} // namespace greeter
