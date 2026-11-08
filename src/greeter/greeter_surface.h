#pragma once

#include "config/config_types.h"
#include "greetd/greetd_client.h"
#include "greeter/appearance_config.h"
#include "greeter/greeter_sessions.h"
#include "greeter/okaru_greeter_view.h"
#include "render/animation/animation_manager.h"
#include "render/core/color.h"
#include "render/core/texture_handle.h"
#include "render/core/wallpaper_types.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

class GreeterWindow;
class Input;
class Label;
class RenderContext;
class RectNode;
class WallpaperNode;

class GreeterSurface {
public:
  GreeterSurface();
  ~GreeterSurface();

  void initialize(RenderContext* context);

  void setWindow(GreeterWindow* window);
  void setBoundOutputName(std::string outputName);
  void setWallpaperSpanParams(const WallpaperSpanParams& params);
  void setGreetdClient(GreetdClient* client);
  void setUsername(const std::string& username);
  void setOnExitRequested(std::function<void()> callback);
  void setOnStateChanged(std::function<void(GreeterSurface*)> callback);
  void setOnAuthBeginRequested(std::function<bool(GreeterSurface*)> callback);
  void setOnAuthEnded(std::function<void(GreeterSurface*)> callback);
  void setOnGreetdTransportError(std::function<void(const GreetdError&)> callback);
  void setKeyboardOwner(bool owner) noexcept;

  void mirrorStateFrom(const GreeterSurface& other);

  // Drive the greetd auth conversation forward when its socket is readable.
  void onGreetdReadable();
  // Fail closed without sending another request when the shared greetd backend
  // can no longer make progress.
  void setGreetdUnavailable(std::string_view reason);
  // Prevent this surface from starting a second conversation on the shared
  // greetd connection while another output owns authentication.
  void setSharedAuthBlocked(bool blocked);
  // True while this surface is mid-conversation with greetd.
  [[nodiscard]] bool authInProgress() const noexcept { return m_authenticating; }

  void onPointerLeave();
  void onPointerEvent(float x, float y, std::uint32_t button, bool pressed);
  void onPointerMotion(float x, float y);
  void onKeyEvent(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit);

  void onThemeChanged();
  [[nodiscard]] bool handleNavigationKey(std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers);
  void requestLayout();
  void requestRedraw();
  void flushDeferredFrameRequests();

  void prepareFrame(std::uint32_t width, std::uint32_t height, bool needsLayout);
  void setOutputViewport(float x, float y, float width, float height);
  void clearOutputViewport();

  [[nodiscard]] Node* sceneRoot() noexcept { return &m_root; }
  [[nodiscard]] bool animating() const noexcept { return m_animations.hasActive(); }

private:
  void notifyStateChanged();
  [[nodiscard]] bool pointerInViewport(float x, float y) const;
  void layoutScene(std::uint32_t width, std::uint32_t height);
  void tryAuthenticate();
  void handleGreetdResponse(const GreetdResponse& response);
  void handleAuthMessage(const GreetdAuthMessage& message);
  void postAuthResponse(const std::string& data);
  // Disable the password field while a request is in flight.
  void syncAuthInteractivity();
  void beginSessionStart();
  void onAuthError(const GreetdError& error);
  void reportGreetdTransportError(const GreetdError& error);
  [[nodiscard]] bool resetAuthSession();
  void clearPasswordInput();
  void updateStatus(const std::string& text, bool isError);
  void commitImmediateFrame(bool layout);
  void selectUser(std::size_t userIndex);
  void stepSession(std::size_t delta);
  void focusPasswordField();
  void applyInitialUserSelection();
  void loadPreferences();
  void reconcileKeyboardFocus();
  void savePreferences() const;
  void buildSchemeNames();
  void applyScheme(std::size_t schemeIndex);
  void applyConfiguredWallpaper();
  void clearWallpaperDisplay();
  [[nodiscard]] bool isSyncedScheme(std::size_t schemeIndex) const;
  [[nodiscard]] std::optional<std::size_t> findSchemeIndex(std::string_view name) const;
  void syncWallpaperTexture();
  void loadUsers();
  void loadSessions();

  // Nodes unregister animations while being destroyed, so the manager must
  // outlive the entire scene tree (members are destroyed in reverse order).
  AnimationManager m_animations;
  Node m_root;
  // OKARU: diseño propio igual al bloqueo de la shell (okaru_greeter_view.h).
  greeter::OkaruGreeterView m_okaru;
  void layoutOkaru(float ox, float oy, float sw, float sh);
  GreeterWindow* m_window = nullptr;
  RenderContext* m_renderContext = nullptr;
  GreetdClient* m_greetdClient = nullptr;
  InputDispatcher m_inputDispatcher;
  std::function<void(GreeterSurface*)> m_onStateChanged;

  WallpaperNode* m_wallpaper = nullptr;
  RectNode* m_letterbox = nullptr;
  Node* m_backdrop = nullptr;
  float m_viewportX = 0.0f;
  float m_viewportY = 0.0f;
  float m_viewportWidth = 0.0f;
  float m_viewportHeight = 0.0f;
  bool m_outputViewportActive = false;
  Input* m_passwordField = nullptr;
  RectNode* m_configErrorBanner = nullptr;
  Label* m_configErrorHeading = nullptr;
  Label* m_configErrorLabel = nullptr;
  bool m_allowEmptyPassword = false;

  // greetd replies in request order, so m_pendingReplies (a FIFO of these) tells
  // which request each reply answers.
  enum class AuthRequest {
    CreateSession,
    PostAuthData,
    StartSession,
    Cancel,
  };

  [[nodiscard]] bool awaitingReply() const noexcept { return !m_pendingReplies.empty(); }

  std::string m_username;
  std::string m_password;
  std::string m_status;
  bool m_statusIsError = false;
  bool m_authenticating = false;
  bool m_greetdUnavailable = false;
  bool m_sharedAuthBlocked = false;
  bool m_authSessionStarted = false;
  bool m_secretPromptWaiting = false; // greetd wants secret input from the user
  bool m_hasPendingResponse = false;  // user-supplied input armed for next prompt
  std::string m_pendingResponse;
  std::deque<AuthRequest> m_pendingReplies;
  std::function<void()> m_onExitRequested;
  std::function<bool(GreeterSurface*)> m_onAuthBeginRequested;
  std::function<void(GreeterSurface*)> m_onAuthEnded;
  std::function<void(const GreetdError&)> m_onGreetdTransportError;
  TextureHandle m_wallpaperTexture{};
  std::string m_boundOutputName;
  std::string m_wallpaperPath;
  WallpaperFillMode m_wallpaperFillMode = WallpaperFillMode::Crop;
  Color m_wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  WallpaperSpanParams m_wallpaperSpanParams;
  bool m_wallpaperDirty = false;
  bool m_hasWallpaper = false;
  std::chrono::steady_clock::time_point m_lastAnimTick{};
  bool m_animTickInitialized = false;
  bool m_inInputDispatch = false;
  bool m_inLayout = false;
  bool m_deferredLayoutRequest = false;
  bool m_deferredRedrawRequest = false;
  bool m_isKeyboardOwner = false;

  std::vector<std::string> m_users;
  std::vector<greeter::SessionOption> m_sessions;
  std::size_t m_selectedUser = 0;
  std::size_t m_selectedSession = 0;
  std::vector<std::string> m_schemeNames;
  std::size_t m_selectedScheme = 0;
  std::optional<GreeterSyncedAppearance> m_syncedAppearance;
  std::optional<GreeterWallpaperAppearance> m_wallpaperAppearance;
};
