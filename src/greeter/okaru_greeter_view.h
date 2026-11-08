#pragma once

// OKARU: greeter con el mismo diseño que la pantalla de bloqueo de la shell (okaru_lock_view): wallpaper con
// mucho blur bajo un velo oscuro, logo 郁 (color de acento) + OKARU con el arte de bloques del instalador,
// usuario y campo de contraseña rectangular con candado en tipografía pixelada, y abajo un selector de sesión
// en lugar de los botones de energía. Texto siempre claro sobre el velo.
//
// La autenticación, el foco y la edición siguen en GreeterSurface: la vista toma su Input de contraseña (lo deja
// invisible, sin placeholder) y dibuja encima los puntos o el texto de ayuda.

#include "render/core/image_file_loader.h"
#include "render/core/texture_handle.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

class Box;
class ImageNode;
class Input;
class InputArea;
class Label;
class Node;
class Renderer;
class TextureManager;

namespace greeter {

  struct OkaruGreeterState {
    std::string user;
    bool canSwitchUser = false; // más de un usuario: clic en el nombre para cambiar
    std::size_t passwordLength = 0;
    std::string placeholder;
    std::string status;
    bool statusError = false;
    std::string session; // nombre de la sesión elegida
    bool canSwitchSession = false;
  };

  // Texto de ayuda del campo en el idioma del sistema (el greeter no tiene catálogo de traducciones):
  // español si LC_ALL / LC_MESSAGES / LANG empieza por "es", inglés en otro caso.
  [[nodiscard]] std::string okaruPasswordPlaceholder();

  // El fondo: wallpaper reducido y con blur fuerte en CPU (el greeter no tiene blur de GPU).
  [[nodiscard]] std::optional<LoadedImageFile> loadBlurredWallpaper(const std::string& path);

  class OkaruGreeterView {
  public:
    void build(Node& root);
    void adoptPasswordField(Input* field);
    void setOnNextUser(std::function<void()> callback) { m_onNextUser = std::move(callback); }
    void setOnNextSession(std::function<void()> callback) { m_onNextSession = std::move(callback); }
    void setOnPreviousSession(std::function<void()> callback) { m_onPreviousSession = std::move(callback); }
    void layout(
        Renderer& renderer, TextureManager& textures, float x, float y, float width, float height,
        const OkaruGreeterState& state
    );

  private:
    void syncTexture(
        TextureManager& textures, ImageNode* node, TextureHandle& handle, std::string& key, const std::string& asset,
        std::string_view fill, const std::string& hex, int size
    );

    Box* m_veil = nullptr;
    ImageNode* m_logo = nullptr;
    TextureHandle m_logoTexture{};
    std::string m_logoKey;
    Label* m_user = nullptr;
    InputArea* m_userArea = nullptr;
    ImageNode* m_lockIcon = nullptr;
    TextureHandle m_lockTexture{};
    std::string m_lockKey;
    Box* m_field = nullptr;
    Label* m_fieldText = nullptr;
    Label* m_status = nullptr;
    Box* m_session = nullptr;
    Label* m_sessionLabel = nullptr;
    InputArea* m_sessionArea = nullptr;
    bool m_sessionHovered = false;
    Input* m_input = nullptr;
    std::function<void()> m_onNextUser;
    std::function<void()> m_onNextSession;
    std::function<void()> m_onPreviousSession;
  };

} // namespace greeter
