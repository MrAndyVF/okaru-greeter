#include "greeter/greeter_surface.h"

#include "accounts/accounts_icon.h"
#include "core/key_modifiers.h"
#include "core/key_symbols.h"
#include "core/log.h"
#include "core/resource_paths.h"
#include "greeter/appearance_config.h"
#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"
#include "greeter/greeter_sessions.h"
#include "greeter/greeter_window.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "render/scene/rect_node.h"
#include "render/scene/wallpaper_node.h"
#include "theme/builtin_palettes.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <pwd.h>
#include <sstream>
#include <sys/types.h>
#include <unordered_set>

namespace {
  constexpr Logger kLog("greeter-surface");

  void appendDummyUsers(std::vector<std::string>& users) {
    const char* dummyEnv = std::getenv("OKARU_GREETER_DUMMY_USERS");
    if (dummyEnv == nullptr) {
      return;
    }

    std::size_t count = 15;
    if (dummyEnv[0] != '\0') {
      char* end = nullptr;
      const unsigned long parsed = std::strtoul(dummyEnv, &end, 10);
      if (end != dummyEnv && parsed > 0) {
        count = static_cast<std::size_t>(parsed);
      }
    }

    for (std::size_t i = 0; i < count; ++i) {
      char name[32];
      std::snprintf(name, sizeof(name), "dummy-user-%02zu", i + 1);
      users.push_back(name);
    }
  }

  bool parseColorWallpaperPath(std::string_view path, Color& out) {
    constexpr std::string_view kPrefix = "color:";
    if (!path.starts_with(kPrefix)) {
      return false;
    }
    return tryParseHexColor(path.substr(kPrefix.size()), out);
  }

} // namespace

GreeterSurface::GreeterSurface() = default;

GreeterSurface::~GreeterSurface() {
  if (m_renderContext != nullptr && m_wallpaperTexture.id != 0) {
    m_renderContext->textureManager().unload(m_wallpaperTexture);
  }
}

void GreeterSurface::initialize(RenderContext* context) {
  m_renderContext = context;

  auto letterbox = std::make_unique<RectNode>();
  m_letterbox = letterbox.get();
  m_letterbox->setZIndex(-1);
  m_letterbox->setHitTestVisible(false);
  m_letterbox->setVisible(false);
  m_letterbox->setStyle(
      RoundedRectStyle{
          .fill = rgba(0.0f, 0.0f, 0.0f, 1.0f),
          .fillMode = FillMode::Solid,
      }
  );
  m_root.addChild(std::move(letterbox));

  auto wallpaper = std::make_unique<WallpaperNode>();
  m_wallpaper = wallpaper.get();
  m_wallpaper->setZIndex(0);
  m_wallpaper->setSpan(m_wallpaperSpanParams);
  m_root.addChild(std::move(wallpaper));

  auto backdrop = std::make_unique<RectNode>();
  backdrop->setStyle(
      RoundedRectStyle{
          .fill = colorForRole(ColorRole::Surface),
          .fillMode = FillMode::Solid,
      }
  );
  m_backdrop = backdrop.get();
  m_backdrop->setZIndex(1);
  m_root.addChild(std::move(backdrop));

  auto pwField = std::make_unique<Input>();
  pwField->setPasswordMode(true);
  pwField->setControlHeight(Style::controlHeight());
  pwField->setOnChange([this](const std::string& value) { m_password = value; });
  pwField->setOnSubmit([this](const std::string&) { tryAuthenticate(); });
  m_passwordField = pwField.get();
  m_passwordField->setZIndex(6);
  m_root.addChild(std::move(pwField));

  auto configErrorBanner = std::make_unique<RectNode>();
  configErrorBanner->setHitTestVisible(false);
  configErrorBanner->setVisible(false);
  m_configErrorBanner = configErrorBanner.get();
  m_configErrorBanner->setZIndex(9);
  m_root.addChild(std::move(configErrorBanner));

  auto configErrorHeading = std::make_unique<Label>();
  configErrorHeading->setText("Configuration errors");
  configErrorHeading->setFontSize(Style::fontSizeCaption());
  configErrorHeading->setBold(true);
  configErrorHeading->setColor(colorForRole(ColorRole::Error));
  configErrorHeading->setVisible(false);
  m_configErrorHeading = configErrorHeading.get();
  m_configErrorHeading->setZIndex(10);
  m_root.addChild(std::move(configErrorHeading));

  auto configError = std::make_unique<Label>();
  configError->setFontSize(Style::fontSizeCaption());
  configError->setColor(colorForRole(ColorRole::OnSurface));
  configError->setMaxLines(4);
  configError->setVisible(false);
  m_configErrorLabel = configError.get();
  m_configErrorLabel->setZIndex(10);
  m_root.addChild(std::move(configError));

  m_root.setAnimationManager(&m_animations);
  m_inputDispatcher.setSceneRoot(&m_root);
  m_inputDispatcher.setCursorShapeCallback([](std::uint32_t serial, std::uint32_t shape) {
    (void)serial;
    (void)shape;
  });

  loadUsers();
  loadSessions();
  buildSchemeNames();
  loadPreferences();
  if (m_selectedScheme >= m_schemeNames.size()) {
    if (const auto fallback = findSchemeIndex("OKARU")) {
      m_selectedScheme = *fallback;
    } else {
      m_selectedScheme = 0;
    }
  }
  applyScheme(m_selectedScheme);
  applyInitialUserSelection();

  if (const auto& diagnostics = greeter::config::configDiagnostics(); !diagnostics.empty()) {
    const auto& diagnostic = diagnostics.front();
    std::ostringstream message;
    message << diagnostic.path.filename().string();
    if (diagnostic.line > 0) {
      message << ':' << diagnostic.line;
      if (diagnostic.column > 0) {
        message << ':' << diagnostic.column;
      }
    }
    message << ": " << diagnostic.message;
    if (diagnostics.size() > 1) {
      message << " (+" << diagnostics.size() - 1 << " more)";
    }
    m_configErrorLabel->setText(message.str());
    m_configErrorLabel->setVisible(true);
    m_configErrorHeading->setVisible(true);
    m_configErrorBanner->setVisible(true);
  }

  m_okaru.build(m_root);
  m_okaru.adoptPasswordField(m_passwordField);
  m_okaru.setOnNextUser([this] {
    if (m_users.size() > 1) {
      selectUser((m_selectedUser + 1) % m_users.size());
    }
  });
  m_okaru.setOnNextSession([this] { stepSession(1); });
  m_okaru.setOnPreviousSession([this] { stepSession(m_sessions.size() - 1); });

  requestLayout();
}

void GreeterSurface::applyInitialUserSelection() {
  const auto initialUser = greeter::resolveInitialUserName(greeter::loadGreeterPreferences());
  if (initialUser.has_value()) {
    const auto it = std::ranges::find(m_users, *initialUser);
    if (it != m_users.end()) {
      m_selectedUser = static_cast<std::size_t>(std::distance(m_users.begin(), it));
    } else {
      // Domain / FreeIPA users may be absent from the list until cached; still
      // honor --user / [user].default so PAM can resolve them by name.
      m_users.push_back(*initialUser);
      m_selectedUser = m_users.size() - 1;
      kLog.info("default_user '{}' not in user list; using it anyway", *initialUser);
    }
  } else {
    m_selectedUser = 0;
  }
  setUsername(m_users[m_selectedUser]);
  m_password.clear();
  m_passwordField->setValue("");
}

void GreeterSurface::setKeyboardOwner(const bool owner) noexcept {
  m_isKeyboardOwner = owner;
  if (owner) {
    reconcileKeyboardFocus();
  }
}

void GreeterSurface::focusPasswordField() {
  if (m_passwordField != nullptr && m_passwordField->inputArea() != nullptr) {
    m_inputDispatcher.setFocus(m_passwordField->inputArea());
  }
}

void GreeterSurface::reconcileKeyboardFocus() {
  // El campo de contraseña es lo único con foco de teclado en esta vista.
  if (m_passwordField != nullptr && InputArea::getFocused() != m_passwordField->inputArea()) {
    focusPasswordField();
  }
}

void GreeterSurface::setWindow(GreeterWindow* window) { m_window = window; }

void GreeterSurface::setGreetdClient(GreetdClient* client) { m_greetdClient = client; }

void GreeterSurface::setUsername(const std::string& username) { m_username = username; }

void GreeterSurface::setOnExitRequested(std::function<void()> callback) { m_onExitRequested = std::move(callback); }

void GreeterSurface::setOnStateChanged(std::function<void(GreeterSurface*)> callback) {
  m_onStateChanged = std::move(callback);
}

void GreeterSurface::setOnAuthBeginRequested(std::function<bool(GreeterSurface*)> callback) {
  m_onAuthBeginRequested = std::move(callback);
}

void GreeterSurface::setOnAuthEnded(std::function<void(GreeterSurface*)> callback) {
  m_onAuthEnded = std::move(callback);
}

void GreeterSurface::setOnGreetdTransportError(std::function<void(const GreetdError&)> callback) {
  m_onGreetdTransportError = std::move(callback);
}

void GreeterSurface::notifyStateChanged() {
  if (m_onStateChanged) {
    m_onStateChanged(this);
  }
}

void GreeterSurface::mirrorStateFrom(const GreeterSurface& other) {
  if (&other == this) {
    return;
  }

  const bool schemeChanged = m_selectedScheme != other.m_selectedScheme;

  m_selectedUser = other.m_selectedUser;
  m_selectedSession = other.m_selectedSession;
  m_selectedScheme = other.m_selectedScheme;
  m_username = other.m_username;
  m_status = other.m_status;
  m_statusIsError = other.m_statusIsError;

  if (schemeChanged && m_selectedScheme < m_schemeNames.size()) {
    applyScheme(m_selectedScheme);
  }

  commitImmediateFrame(true);
}

void GreeterSurface::setOutputViewport(float x, float y, float width, float height) {
  m_viewportX = x;
  m_viewportY = y;
  m_viewportWidth = std::max(width, 1.0f);
  m_viewportHeight = std::max(height, 1.0f);
  m_outputViewportActive = true;
  requestLayout();
}

void GreeterSurface::clearOutputViewport() {
  if (!m_outputViewportActive) {
    return;
  }
  m_outputViewportActive = false;
  requestLayout();
}

bool GreeterSurface::pointerInViewport(float x, float y) const {
  if (!m_outputViewportActive) {
    return true;
  }
  return x >= m_viewportX
      && y >= m_viewportY
      && x < m_viewportX + m_viewportWidth
      && y < m_viewportY + m_viewportHeight;
}

void GreeterSurface::onPointerLeave() {
  m_inputDispatcher.pointerLeave();
  requestRedraw();
}

void GreeterSurface::onPointerEvent(float x, float y, std::uint32_t button, bool pressed) {
  if (!pointerInViewport(x, y)) {
    return;
  }
  m_inInputDispatch = true;
  m_inputDispatcher.pointerButton(x, y, button, pressed);
  m_inInputDispatch = false;
  flushDeferredFrameRequests();
  if (m_root.paintDirty() || m_root.layoutDirty()) {
    if (m_root.layoutDirty())
      requestLayout();
    else
      requestRedraw();
  }
}

void GreeterSurface::onPointerMotion(float x, float y) {
  if (!pointerInViewport(x, y)) {
    return;
  }
  m_inInputDispatch = true;
  m_inputDispatcher.pointerMotion(x, y, 0);
  m_inInputDispatch = false;
  flushDeferredFrameRequests();
  if (m_root.paintDirty() || m_root.layoutDirty()) {
    if (m_root.layoutDirty())
      requestLayout();
    else
      requestRedraw();
  }
}

void GreeterSurface::onKeyEvent(
    std::uint32_t sym, std::uint32_t utf32, std::uint32_t modifiers, bool pressed, bool preedit
) {
  if (!pressed)
    return;
  reconcileKeyboardFocus();
  m_inInputDispatch = true;
  if (!handleNavigationKey(sym, utf32, modifiers)) {
    m_inputDispatcher.keyEvent(sym, utf32, modifiers, pressed, preedit);
  }
  m_inInputDispatch = false;
  flushDeferredFrameRequests();
  if (m_root.paintDirty() || m_root.layoutDirty()) {
    if (m_root.layoutDirty())
      requestLayout();
    else
      requestRedraw();
  }
}

void GreeterSurface::onThemeChanged() { requestLayout(); }

void GreeterSurface::requestLayout() {
  if (m_inInputDispatch) {
    m_deferredLayoutRequest = true;
    return;
  }
  // Requests issued while a layout is already running (the window renders
  // synchronously) would recurse; the in-progress pass already covers them.
  if (m_inLayout) {
    return;
  }
  if (m_window != nullptr) {
    m_window->requestLayout();
  }
}

void GreeterSurface::requestRedraw() {
  if (m_inInputDispatch) {
    m_deferredRedrawRequest = true;
    return;
  }
  if (m_inLayout) {
    return;
  }
  if (m_window != nullptr) {
    m_window->requestRedraw();
  }
}

void GreeterSurface::flushDeferredFrameRequests() {
  if (m_window == nullptr) {
    m_deferredLayoutRequest = false;
    m_deferredRedrawRequest = false;
    return;
  }
  const bool layout = m_deferredLayoutRequest;
  const bool redraw = m_deferredRedrawRequest;
  m_deferredLayoutRequest = false;
  m_deferredRedrawRequest = false;
  if (layout) {
    m_window->requestLayout();
  } else if (redraw) {
    m_window->requestRedraw();
  }
}

void GreeterSurface::commitImmediateFrame(bool layout) {
  m_deferredLayoutRequest = false;
  m_deferredRedrawRequest = false;
  if (m_window == nullptr) {
    return;
  }
  if (layout) {
    m_window->requestLayout();
  } else {
    m_window->requestRedraw();
  }
}

void GreeterSurface::prepareFrame(std::uint32_t width, std::uint32_t height, bool needsLayout) {
  if (!m_renderContext || m_window == nullptr) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!m_animTickInitialized) {
    m_lastAnimTick = now;
    m_animTickInitialized = true;
  } else {
    const float deltaMs = std::chrono::duration<float, std::milli>(now - m_lastAnimTick).count();
    m_lastAnimTick = now;
    if (deltaMs > 0.0f) {
      m_animations.tick(deltaMs);
    }
  }

  if (needsLayout) {
    m_renderContext->syncContentScale(m_window->renderTarget());
    syncWallpaperTexture();
    m_inLayout = true;
    layoutScene(width, height);
    m_inLayout = false;
  }
}

void GreeterSurface::selectUser(std::size_t userIndex) {
  if (userIndex >= m_users.size()) {
    return;
  }
  m_selectedUser = userIndex;
  setUsername(m_users[userIndex]);
  m_passwordField->setValue("");
  m_password.clear();
  focusPasswordField();
  notifyStateChanged();
  commitImmediateFrame(true);
}

void GreeterSurface::stepSession(std::size_t delta) {
  if (m_sessions.size() < 2) {
    return;
  }
  m_selectedSession = (m_selectedSession + delta) % m_sessions.size();
  savePreferences();
  focusPasswordField();
  notifyStateChanged();
  commitImmediateFrame(true);
}

void GreeterSurface::layoutScene(std::uint32_t width, std::uint32_t height) {
  auto* renderer = m_renderContext;
  if (!renderer) {
    return;
  }

  const float fullW = static_cast<float>(width);
  const float fullH = static_cast<float>(height);
  const float ox = m_outputViewportActive ? m_viewportX : 0.0f;
  const float oy = m_outputViewportActive ? m_viewportY : 0.0f;
  const float sw = m_outputViewportActive ? m_viewportWidth : fullW;
  const float sh = m_outputViewportActive ? m_viewportHeight : fullH;

  m_root.setSize(fullW, fullH);

  if (m_letterbox != nullptr) {
    m_letterbox->setVisible(m_outputViewportActive);
    if (m_outputViewportActive) {
      m_letterbox->setPosition(0.0f, 0.0f);
      m_letterbox->setSize(fullW, fullH);
    }
  }

  if (m_wallpaper != nullptr) {
    m_wallpaper->setPosition(ox, oy);
    m_wallpaper->setSize(sw, sh);
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
  }

  m_backdrop->setPosition(ox, oy);
  m_backdrop->setSize(sw, sh);
  if (m_hasWallpaper) {
    // WallpaperNode draws the image and any letterbox fill; keep backdrop
    // hidden so we do not paint wallpaperFillColor as a full-screen overlay on
    // top.
    m_backdrop->setVisible(false);
  } else {
    static_cast<RectNode*>(m_backdrop)
        ->setStyle(
            RoundedRectStyle{
                .fill = colorForRole(ColorRole::Surface),
                .fillMode = FillMode::Solid,
            }
        );
    m_backdrop->setVisible(true);
  }

  if (m_configErrorBanner != nullptr && m_configErrorBanner->visible()) {
    constexpr float kBannerPadding = 14.0f;
    constexpr float kBannerMaxWidth = 960.0f;
    const float bannerWidth = std::max(200.0f, std::min(sw - 80.0f, Style::scaled(kBannerMaxWidth)));
    const float bannerContentWidth = bannerWidth - Style::scaled(kBannerPadding * 2.0f);
    m_configErrorHeading->setColor(colorForRole(ColorRole::Error));
    m_configErrorHeading->setMaxWidth(bannerContentWidth);
    m_configErrorHeading->measure(*renderer);
    m_configErrorLabel->setColor(colorForRole(ColorRole::OnSurface));
    m_configErrorLabel->setMaxWidth(bannerContentWidth);
    m_configErrorLabel->measure(*renderer);

    const float padding = Style::scaled(kBannerPadding);
    const float gap = Style::spaceSm();
    const float bannerHeight = padding * 2.0f + m_configErrorHeading->height() + gap + m_configErrorLabel->height();
    const float bannerX = std::round(ox + (sw - bannerWidth) * 0.5f);
    const float bannerY = oy + Style::spaceLg();
    m_configErrorBanner->setPosition(bannerX, bannerY);
    m_configErrorBanner->setSize(bannerWidth, bannerHeight);
    m_configErrorBanner->setStyle(
        RoundedRectStyle{
            .fill = colorForRole(ColorRole::SurfaceVariant, 0.97f),
            .border = colorForRole(ColorRole::Error, 0.75f),
            .fillMode = FillMode::Solid,
            .radius = Style::scaledRadiusXl(),
            .softness = 1.0f,
            .borderWidth = Style::borderWidth(),
        }
    );
    m_configErrorHeading->setPosition(bannerX + padding, bannerY + padding);
    m_configErrorLabel->setPosition(bannerX + padding, bannerY + padding + m_configErrorHeading->height() + gap);
  }

  layoutOkaru(ox, oy, sw, sh);

  if (m_isKeyboardOwner) {
    reconcileKeyboardFocus();
  }
}

void GreeterSurface::layoutOkaru(float ox, float oy, float sw, float sh) {
  const std::string& password = m_passwordField != nullptr ? m_passwordField->value() : m_password;
  const auto codepoints = static_cast<std::size_t>(std::ranges::count_if(password, [](char c) {
    return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
  }));
  m_okaru.layout(
      *m_renderContext, m_renderContext->textureManager(), ox, oy, sw, sh,
      greeter::OkaruGreeterState{
          .user = m_selectedUser < m_users.size() ? m_users[m_selectedUser] : m_username,
          .canSwitchUser = m_users.size() > 1,
          .passwordLength = codepoints,
          .placeholder = greeter::okaruPasswordPlaceholder(),
          .status = m_status,
          .statusError = m_statusIsError,
          .session = m_selectedSession < m_sessions.size() ? m_sessions[m_selectedSession].name : std::string(),
          .canSwitchSession = m_sessions.size() > 1,
      }
  );
}

void GreeterSurface::tryAuthenticate() {
  // Ignore re-submits while a request is already in flight.
  if (m_greetdClient == nullptr || m_greetdUnavailable || m_sharedAuthBlocked || awaitingReply()) {
    return;
  }
  if (m_username.empty()) {
    updateStatus("Enter a username", true);
    commitImmediateFrame(false);
    return;
  }
  if (m_password.empty() && !m_allowEmptyPassword) {
    updateStatus("Enter a password", true);
    commitImmediateFrame(false);
    return;
  }

  if (!m_authSessionStarted) {
    if (m_onAuthBeginRequested && !m_onAuthBeginRequested(this)) {
      return;
    }
    // Arm the typed input (possibly empty) to answer the first secret prompt.
    m_pendingResponse = m_password;
    m_hasPendingResponse = true;
    m_authenticating = true;
    kLog.info("greetd: create_session for '{}'", m_username);
    if (!m_greetdClient->requestCreateSession(m_username)) {
      reportGreetdTransportError(
          m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
      );
      return;
    }
    m_authSessionStarted = true;
    m_pendingReplies.push_back(AuthRequest::CreateSession);
    syncAuthInteractivity();
    commitImmediateFrame(false);
  } else if (m_secretPromptWaiting) {
    postAuthResponse(m_password);
  }
}

void GreeterSurface::onGreetdReadable() {
  if (m_greetdClient == nullptr) {
    return;
  }
  // Drain every frame currently buffered; readMessage() stops at nullopt.
  for (;;) {
    const std::optional<GreetdResponse> response = m_greetdClient->readMessage();
    if (!response.has_value()) {
      if (const auto error = m_greetdClient->lastError()) {
        kLog.warn("greetd connection lost: {}", error->description);
        reportGreetdTransportError(*error);
      }
      return;
    }
    handleGreetdResponse(*response);
  }
}

void GreeterSurface::handleGreetdResponse(const GreetdResponse& response) {
  if (m_pendingReplies.empty()) {
    return; // unsolicited reply — nothing to correlate it with
  }
  const AuthRequest expected = m_pendingReplies.front();
  m_pendingReplies.pop_front();

  const char* reqName = expected == AuthRequest::CreateSession ? "create_session"
      : expected == AuthRequest::PostAuthData                  ? "post_auth_data"
      : expected == AuthRequest::StartSession                  ? "start_session"
                                                               : "cancel_session";
  const char* respName = response.type == GreetdResponseType::AuthMessage ? "auth_message"
      : response.type == GreetdResponseType::Error                        ? "error"
                                                                          : "success";
  kLog.debug("greetd reply to {}: {}", reqName, respName);

  // Cancel ack drained the queue; re-enable input it had locked.
  if (expected == AuthRequest::Cancel) {
    if (m_onAuthEnded) {
      m_onAuthEnded(this);
    }
    syncAuthInteractivity();
    commitImmediateFrame(false);
    return;
  }

  switch (response.type) {
  case GreetdResponseType::Error:
    onAuthError(response.error);
    return;
  case GreetdResponseType::AuthMessage:
    handleAuthMessage(response.authMessage);
    return;
  case GreetdResponseType::Success:
    if (expected == AuthRequest::StartSession) {
      kLog.info("session start confirmed, exiting greeter");
      if (m_onExitRequested) {
        m_onExitRequested();
      }
    } else {
      // Auth finished; launch the session.
      beginSessionStart();
    }
    return;
  }
}

void GreeterSurface::handleAuthMessage(const GreetdAuthMessage& message) {
  const char* typeName = message.type == GreetdAuthMessageType::Secret ? "secret"
      : message.type == GreetdAuthMessageType::Visible                 ? "visible"
      : message.type == GreetdAuthMessageType::Error                   ? "error"
                                                                       : "info";
  kLog.info("PAM {} message: {}", typeName, message.message);

  if (message.type == GreetdAuthMessageType::Info || message.type == GreetdAuthMessageType::Error) {
    const bool isError = message.type == GreetdAuthMessageType::Error;
    if (!message.message.empty()) {
      updateStatus(message.message, isError);
    }
    if (isError) {
      clearPasswordInput();
    }
    // Ack without a credential to resume PAM; paint now so the message shows.
    if (!m_greetdClient->requestPostAuthData(std::nullopt)) {
      reportGreetdTransportError(
          m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
      );
      return;
    }
    m_pendingReplies.push_back(AuthRequest::PostAuthData);
    syncAuthInteractivity();
    commitImmediateFrame(false);
    return;
  }

  // Secret / Visible prompt. Answer with already-submitted input if we have it
  // (empty is allowed), otherwise surface the prompt and wait for the user.
  if (m_hasPendingResponse) {
    const std::string response = m_pendingResponse;
    m_hasPendingResponse = false;
    m_pendingResponse.clear();
    postAuthResponse(response);
    return;
  }

  m_secretPromptWaiting = true;
  if (!message.message.empty()) {
    updateStatus(message.message, false);
  }
  syncAuthInteractivity();
  commitImmediateFrame(false);
}

void GreeterSurface::postAuthResponse(const std::string& data) {
  kLog.debug("greetd: post_auth_data ({} bytes)", data.size());
  if (!m_greetdClient->requestPostAuthData(data)) {
    reportGreetdTransportError(
        m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
    );
    return;
  }
  clearPasswordInput();
  m_secretPromptWaiting = false;
  m_pendingReplies.push_back(AuthRequest::PostAuthData);
  syncAuthInteractivity();
  commitImmediateFrame(false);
}

void GreeterSurface::syncAuthInteractivity() {
  const bool busy = m_greetdUnavailable || m_sharedAuthBlocked || awaitingReply();
  if (m_passwordField != nullptr) {
    m_passwordField->setEnabled(!busy);
  }
}

void GreeterSurface::setSharedAuthBlocked(const bool blocked) {
  if (m_sharedAuthBlocked == blocked) {
    return;
  }
  m_sharedAuthBlocked = blocked;
  syncAuthInteractivity();
  commitImmediateFrame(false);
}

void GreeterSurface::setGreetdUnavailable(const std::string_view reason) {
  m_greetdUnavailable = true;
  m_authenticating = false;
  m_authSessionStarted = false;
  m_secretPromptWaiting = false;
  m_hasPendingResponse = false;
  m_pendingResponse.clear();
  m_pendingReplies.clear();
  clearPasswordInput();
  syncAuthInteractivity();
  updateStatus(std::string(reason), true);
  commitImmediateFrame(false);
}

void GreeterSurface::beginSessionStart() {
  kLog.info("authentication successful");

  savePreferences();

  if (m_greetdClient == nullptr) {
    kLog.error("no greetd client to start session");
    return;
  }

  GreetdSessionCommand cmd;
  greeter::SessionOption session{
      .name = "Shell", .desktopId = {}, .command = "/bin/sh", .desktopNames = {}, .sessionType = "tty"
  };
  if (!m_sessions.empty() && m_selectedSession < m_sessions.size()) {
    session = m_sessions[m_selectedSession];
  }

  // greetd cmd is argv; Exec= may be multiple tokens (e.g. "dbus-run-session gnome-session").
  // x11 sessions are wrapped in okaru-greeter-xsession to bootstrap Xorg first.
  {
    const std::vector<std::string> argv = greeter::sessionArgv(session);
    if (argv.empty()) {
      kLog.error("session '{}' has empty Exec", session.name);
      m_authenticating = false;
      clearPasswordInput();
      updateStatus("Failed to start session: empty command", true);
      if (!resetAuthSession()) {
        return;
      }
      syncAuthInteractivity();
      commitImmediateFrame(false);
      return;
    }
    cmd.command = argv.front();
    cmd.arguments.assign(argv.begin() + 1, argv.end());
  }
  cmd.environment = greeter::sessionStartEnvironment(session);
  {
    std::string envSummary;
    for (const auto& entry : cmd.environment) {
      if (!envSummary.empty()) {
        envSummary += ' ';
      }
      envSummary += entry.key + '=' + entry.value;
    }
    kLog.info(
        "greetd: start_session session='{}' cmd='{}' args={} env=[{}]", session.name, cmd.command, cmd.arguments.size(),
        envSummary
    );
  }

  if (!m_greetdClient->requestStartSession(cmd)) {
    reportGreetdTransportError(
        m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
    );
    return;
  }

  m_pendingReplies.push_back(AuthRequest::StartSession);
}

bool GreeterSurface::resetAuthSession() {
  if (m_greetdClient != nullptr && m_authSessionStarted) {
    if (!m_greetdClient->requestCancelSession()) {
      m_authSessionStarted = false;
      reportGreetdTransportError(
          m_greetdClient->lastError().value_or(GreetdError{GreetdErrorType::Error, "failed to send request"})
      );
      return false;
    }
    // Track the ack so a later create_session reply is not mistaken for it.
    m_pendingReplies.push_back(AuthRequest::Cancel);
    m_authSessionStarted = false;
    return true;
  }
  m_authSessionStarted = false;
  if (m_onAuthEnded) {
    m_onAuthEnded(this);
  }
  return true;
}

void GreeterSurface::clearPasswordInput() {
  m_password.clear();
  if (m_passwordField != nullptr) {
    m_passwordField->setValue("");
  }
}

void GreeterSurface::onAuthError(const GreetdError& error) {
  m_authenticating = false;
  m_secretPromptWaiting = false;
  m_hasPendingResponse = false;
  m_pendingResponse.clear();
  m_pendingReplies.clear();
  clearPasswordInput();
  updateStatus(error.description, true);
  kLog.warn("authentication failed: {}", error.description);
  if (!resetAuthSession()) {
    return;
  }
  syncAuthInteractivity();
  commitImmediateFrame(false);
}

void GreeterSurface::reportGreetdTransportError(const GreetdError& error) {
  if (m_onGreetdTransportError) {
    m_onGreetdTransportError(error);
    return;
  }
  if (m_greetdClient != nullptr) {
    m_greetdClient->disconnect();
  }
  setGreetdUnavailable("Login service is unavailable. Restart greetd.");
}

void GreeterSurface::updateStatus(const std::string& text, bool isError) {
  // Empty text clears the line; non-empty text shows as an error or, for PAM
  // info prompts, as a neutral hint rather than being discarded.
  m_status = text;
  m_statusIsError = isError;
  requestLayout();
}

void GreeterSurface::loadUsers() {
  m_users.clear();
  static const std::unordered_set<std::string> kHiddenSystemUsers = {
      "greeter", "greetd", "sddm", "lightdm", "gdm", "nobody",
  };

  std::unordered_set<std::string> seen;

  // Prefer AccountsService cached users (same source as ReGreet) so FreeIPA /
  // LDAP accounts that have logged in here appear even when NSS enumeration
  // does not list them.
  const auto cachedUsers = accounts::listCachedUsers();
  for (const auto& cached : cachedUsers) {
    if (cached.username.empty() || kHiddenSystemUsers.contains(cached.username)) {
      continue;
    }
    if (!seen.insert(cached.username).second) {
      continue;
    }
    m_users.push_back(cached.username);
  }
  if (!cachedUsers.empty()) {
    kLog.info("AccountsService: {} cached user(s), {} after filters", cachedUsers.size(), m_users.size());
  }

  int userEnumerationErrno = 0;
  ::setpwent();
  while (true) {
    errno = 0;
    struct passwd* pw = ::getpwent();
    if (pw == nullptr) {
      userEnumerationErrno = errno;
      break;
    }

    if (pw->pw_name == nullptr || pw->pw_shell == nullptr) {
      continue;
    }

    std::string user = pw->pw_name;
    std::string shell = pw->pw_shell;
    const uid_t uid = pw->pw_uid;

    if (uid < 1000 || kHiddenSystemUsers.contains(user)) {
      continue;
    }
    if (shell.find("nologin") != std::string::npos || shell.find("false") != std::string::npos) {
      continue;
    }
    if (!seen.insert(user).second) {
      continue;
    }
    m_users.push_back(user);
  }
  ::endpwent();

  if (userEnumerationErrno != 0) {
    kLog.warn("failed to enumerate NSS users: {}", std::strerror(userEnumerationErrno));
  }

  if (m_users.empty()) {
    m_users.push_back("greeter");
  }

  appendDummyUsers(m_users);

  m_selectedUser = 0;
  setUsername(m_users[m_selectedUser]);
}

void GreeterSurface::loadSessions() {
  m_sessions = greeter::discoverSessions();
  m_selectedSession = 0;
}

void GreeterSurface::buildSchemeNames() {
  m_schemeNames.clear();
  m_syncedAppearance = loadGreeterSyncedAppearance();
  m_wallpaperAppearance = loadGreeterWallpaperAppearance();
  if (m_syncedAppearance.has_value()) {
    m_schemeNames.emplace_back(greeter::appearance::kSyncedSchemeDisplayName);
    m_selectedScheme = 0;
  }

  for (const auto& builtinPalette : noctalia::theme::builtinPalettes()) {
    m_schemeNames.emplace_back(builtinPalette.name);
    if (!m_syncedAppearance.has_value() && builtinPalette.name == "OKARU") {
      m_selectedScheme = m_schemeNames.size() - 1;
    }
  }
}

bool GreeterSurface::isSyncedScheme(const std::size_t schemeIndex) const {
  return schemeIndex < m_schemeNames.size()
      && m_schemeNames[schemeIndex] == greeter::appearance::kSyncedSchemeDisplayName;
}

std::optional<std::size_t> GreeterSurface::findSchemeIndex(const std::string_view name) const {
  for (std::size_t i = 0; i < m_schemeNames.size(); ++i) {
    if (m_schemeNames[i] == name) {
      return i;
    }
  }
  return std::nullopt;
}

void GreeterSurface::setBoundOutputName(std::string outputName) {
  if (m_boundOutputName == outputName) {
    return;
  }
  m_boundOutputName = std::move(outputName);
  applyConfiguredWallpaper();
  requestLayout();
}

void GreeterSurface::setWallpaperSpanParams(const WallpaperSpanParams& params) {
  if (m_wallpaperSpanParams == params) {
    return;
  }
  m_wallpaperSpanParams = params;
  if (m_wallpaper != nullptr) {
    m_wallpaper->setSpan(params);
  }
  requestRedraw();
}

void GreeterSurface::clearWallpaperDisplay() {
  m_hasWallpaper = false;
  m_wallpaperPath.clear();
  m_wallpaperFillMode = WallpaperFillMode::Crop;
  m_wallpaperFillColor = rgba(0.0f, 0.0f, 0.0f, 0.0f);
  m_wallpaperDirty = true;
}

void GreeterSurface::applyConfiguredWallpaper() {
  const auto wallpaper = m_wallpaperAppearance.has_value()
      ? m_wallpaperAppearance->wallpaperForOutput(m_boundOutputName)
      : std::nullopt;
  if (!wallpaper.has_value()) {
    // OKARU: sin fondo sincronizado ni configurado, el mismo wallpaper por defecto que la shell.
    clearWallpaperDisplay();
    if (const auto fallback = paths::assetPath("okaru-wallpaper.png"); std::filesystem::exists(fallback)) {
      m_wallpaperPath = fallback.string();
      m_hasWallpaper = true;
    }
    return;
  }

  m_wallpaperPath = wallpaper->path;
  m_wallpaperFillMode = wallpaper->fillMode;
  m_wallpaperFillColor = wallpaper->fillColor;
  m_hasWallpaper = !m_wallpaperPath.empty() || m_wallpaperFillColor.a > 0.0f;
  m_wallpaperDirty = true;
}

void GreeterSurface::applyScheme(const std::size_t schemeIndex) {
  if (schemeIndex >= m_schemeNames.size()) {
    return;
  }

  m_selectedScheme = schemeIndex;
  if (isSyncedScheme(schemeIndex)) {
    if (!m_syncedAppearance.has_value()) {
      m_syncedAppearance = loadGreeterSyncedAppearance();
    }
    if (!m_syncedAppearance.has_value()) {
      if (const auto fallback = findSchemeIndex("OKARU")) {
        applyScheme(*fallback);
      }
      return;
    }

    setPalette(m_syncedAppearance->palette);
    Style::setCornerRadiusScale(m_syncedAppearance->cornerRadiusScale);
    if (m_renderContext != nullptr && !m_syncedAppearance->fontFamily.empty()) {
      m_renderContext->setTextFontFamily(m_syncedAppearance->fontFamily);
    }
    applyConfiguredWallpaper();
    return;
  }

  if (const auto* builtinPalette = noctalia::theme::findBuiltinPalette(m_schemeNames[schemeIndex])) {
    setPalette(builtinPalette->dark.palette);
  }
  Style::setCornerRadiusScale(0.0f); // OKARU: esquinas rectas también con las paletas integradas
  applyConfiguredWallpaper();
}

void GreeterSurface::syncWallpaperTexture() {
  if (!m_wallpaperDirty || m_wallpaper == nullptr || m_renderContext == nullptr) {
    return;
  }

  if (m_wallpaperTexture.id != 0) {
    m_renderContext->textureManager().unload(m_wallpaperTexture);
    m_wallpaperTexture = {};
  }

  const auto useFillColor = [this]() {
    if (m_wallpaperFillColor.a <= 0.0f) {
      return false;
    }
    m_wallpaper->setSources(
        WallpaperSourceKind::Color, {}, m_wallpaperFillColor, WallpaperSourceKind::Color, {}, m_wallpaperFillColor,
        0.0f, 0.0f, 0.0f, 0.0f
    );
    m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f, TransitionParams{});
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
    return true;
  };

  Color color;
  if (parseColorWallpaperPath(m_wallpaperPath, color)) {
    m_wallpaper->setSources(
        WallpaperSourceKind::Color, {}, color, WallpaperSourceKind::Color, {}, color, 0.0f, 0.0f, 0.0f, 0.0f
    );
    m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f, TransitionParams{});
    m_wallpaper->setFillMode(m_wallpaperFillMode);
    m_wallpaper->setFillColor(m_wallpaperFillColor);
  } else if (!m_wallpaperPath.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(m_wallpaperPath, ec) && !ec) {
      // OKARU: el fondo va reducido y con blur fuerte, bajo el velo de la vista.
      if (const auto blurred = greeter::loadBlurredWallpaper(m_wallpaperPath)) {
        m_wallpaperTexture = m_renderContext->textureManager().loadFromRgba(
            blurred->rgba.data(), blurred->width, blurred->height, false
        );
      }
      if (m_wallpaperTexture.id != 0) {
        m_wallpaper->setTextures(
            m_wallpaperTexture.id, {}, static_cast<float>(m_wallpaperTexture.width),
            static_cast<float>(m_wallpaperTexture.height), 0.0f, 0.0f
        );
        m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0f, TransitionParams{});
        m_wallpaper->setFillMode(m_wallpaperFillMode);
        m_wallpaper->setFillColor(m_wallpaperFillColor);
      } else {
        if (!useFillColor()) {
          m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
          m_hasWallpaper = false;
        }
      }
    } else {
      if (!useFillColor()) {
        m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
        m_hasWallpaper = false;
      }
    }
  } else if (!useFillColor()) {
    m_wallpaper->setTextures({}, {}, 0.0f, 0.0f, 0.0f, 0.0f);
  }

  m_wallpaperDirty = false;
}

void GreeterSurface::loadPreferences() {
  const auto prefs = greeter::loadGreeterPreferences();
  m_allowEmptyPassword = prefs.allowEmptyPassword;
  const auto initialSession = greeter::resolveInitialSessionName(prefs);

  if (initialSession.has_value()) {
    if (const auto index = greeter::findSessionIndex(m_sessions, *initialSession)) {
      m_selectedSession = *index;
    }
  }

  if (prefs.scheme.has_value()) {
    for (std::size_t i = 0; i < m_schemeNames.size(); ++i) {
      if (m_schemeNames[i] == *prefs.scheme) {
        m_selectedScheme = i;
        break;
      }
    }
  }
}

void GreeterSurface::savePreferences() const {
  greeter::GreeterPreferences prefs;
  if (m_selectedSession < m_sessions.size()) {
    prefs.session = m_sessions[m_selectedSession].name;
  }
  if (m_selectedScheme < m_schemeNames.size()) {
    prefs.scheme = m_schemeNames[m_selectedScheme];
  }

  if (!greeter::saveGreeterPreferences(prefs)) {
    kLog.warn("failed to save sync.toml (check permissions on {})", greeter::greeterSyncPath().string());
  }
}

bool GreeterSurface::handleNavigationKey(std::uint32_t sym, std::uint32_t /*utf32*/, std::uint32_t modifiers) {
  if (m_authenticating) {
    return false;
  }
  // F3 / Shift+F3: sesión siguiente / anterior. Todo lo demás va al campo de contraseña.
  if (KeySymbol::isF3(sym)) {
    stepSession((modifiers & KeyMod::Shift) != 0 ? m_sessions.size() - 1 : 1);
    return true;
  }
  return false;
}
