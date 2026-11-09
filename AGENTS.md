# Agent notes for BotWRecomp-macOS

This repository is an unofficial macOS/iOS native port of Breath of the Wild. The game files are
user-provided and are not part of the repository. macOS rendering currently goes through Cemu's
Latte GPU layer and its Metal renderer. Keep claims about device compatibility and fixes tied to
what has actually been reproduced.

## User's current goal

Improve the Mac build and keep the work reviewable and documented. The user has asked for broad
autonomous debugging and in-game verification. They have repeatedly emphasized that input problems
must be investigated in the active game and that debugging should continue instead of stopping at
the first obstacle. Be direct, give short progress updates in Italian, and do not make the user
repeat setup details already present here.

## Current test setup and observations

- Use paths relative to the checkout in scripts and docs. The game folder is external and should be
  selected with `BOTW_GAME` or a documented command argument.
- Local executable: `build/BotWRecomp`.
- User config: `~/Library/Application Support/BotWRecomp/settings.ini`.
- The config was set to `scale=1.0`. One process rendered at about 60 FPS in the tested scene; a
  previous run at a higher scale was around 26–28 FPS. These are scene-specific observations, not
  expected frame rates for the whole open world.
- Duplicate game processes caused confusing input/performance observations before; test with one
  running process and confirm the game window has focus.
- Dialogue navigation: digital D-pad directions are now mirrored into Left Stick Emulation bits
  (`VPAD_STICK_L_EMULATION_UP/DOWN/LEFT/RIGHT`) with repeat pulse handling (`VPAD_BUTTON_REPEAT`),
  enabling arrow keys and controller D-pad to navigate dialogue choice lists directly without walking Link.
- Mouse clicks & touch: corrected VPAD touch validity reporting (`VPAD_INVALID_X | VPAD_INVALID_Y = 3`
  when untouched) to prevent continuous ghost touch input at (0, 0), and ensured clean touch coordinates
  and down/release transitions.
- Water: the white-water repro was fixed and verified in game by restricting Metal depth-to-color
  surface copies to the requested source mip and slice. The relevant code is in
  `MetalRenderer.cpp::surfaceCopy_copySurfaceWithFormatConversion`.
- Render-target feedback: Metal has `CheckIfRenderPassNeedsFlush`; accurate barriers run when
  `vk_accurate_barriers` is enabled, with forced handling for known BotW lava and waterfall-foam
  shaders. Do not describe the option as unconditionally enabled; check its current default and
  preserve the special cases when investigating it.
- Other graphics reports: washed-out cyan/pink colors and texture-like corruption remain separate,
  unresolved observations. The water fix does not establish a fix for these issues.
- Previous crashes/stalls and input failures were reported at particular game moments. Record the
  exact scene, whether the process exited or hung, and preserve the runtime log when investigating.

## Renderer investigation

- `runtime/cemu/Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompilerEmitMSLHeader.hpp`,
  `LatteDecompilerEmitMSL.cpp`, and `runtime/cemu/Cafe/HW/Latte/Renderer/Metal/MetalRenderer.cpp`
  contain changes to separate Metal texture slots from sampler slots. This was motivated by
  [Cemu issue #2038](https://github.com/cemu-project/Cemu/issues/2038), which describes BotW
  shaders using more than 16 textures while remaining within the sampler limit. Treat that change
  as a binding compatibility fix; do not conflate it with the verified depth-copy water fix.
- The Metal render-target self-dependency handling and `CheckIfRenderPassNeedsFlush` implementation
  are present in `MetalRenderer.cpp`. The normal path is gated by `vk_accurate_barriers`; known BotW
  lava and waterfall-foam pixel shaders force the flush. Inspect backend semantics and configuration
  before changing this behavior.
- Do not apply an OpenGL-only `GX2DrawDone()` workaround to native Metal without checking the
  backend semantics. The Cemu [BotW compatibility notes](https://wiki.cemu.info/wiki/ALZP01)
  mention that workaround in the OpenGL context.
- The Metal synchronized ring allocator already clears its remembered command-buffer pointer when
  a command buffer finishes. Do not reapply that fix blindly; inspect the current allocator first.

## Preserving repository work

Before changing or resetting anything, inspect `git status`. Previous user work has included:

- Metal texture/sampler binding changes.
- Keyboard/controller input and KPAD changes, touch/pointer handling, and window/menu work.
- In-game menu, settings and overlay changes.
- H.264 runtime support (`runtime/src/misc/h264_hle.cpp`).
- Mac app icon assets and helper scripts.
- README updates and `tools/play.sh` changes. These changes may since have been committed; never
  infer that the working tree is disposable from a clean status.

Preserve all of this unless the user explicitly asks to discard it. Do not reset, clean, or overwrite
the user's changes. Check whether the current executable was rebuilt after source changes before
testing it. The user has asked to push all project changes; when doing so, inspect staged/untracked
files, commit them together as requested, and report the resulting commit and push status.

## Verification guidance

- The user explicitly asked for in-game debugging and testing; running the game and verifying the
  reported behavior is authorized. Do not add unrelated automated tests unless asked.
- Keep one process running. Confirm focus before keyboard input. A Terminal window taking focus can
  make an input patch look broken when no event reaches the game.
- For graphics comparisons, use the same save/location, resolution scale, renderer and camera view.
  Capture a scene with an unmistakable water surface; a cinematic with bright haze is not sufficient
  to diagnose white water.
- Report what was actually verified, what remains uncertain, and the source or code path behind any
  hypothesis. Do not call a candidate fix complete until the same repro is checked again.
- Keep README status aligned with observed behavior and clearly label unresolved issues.
- macOS CMake, the iOS build and the standalone shader tools should discover Homebrew with
  `brew --prefix`; do not add a hard-coded package-manager or developer-volume path. The all-in-one Mac
  builder packages the runtime binary inside `dist/BotWRecomp.app` and resolves the user's game path
  at first launch. Its deployment target follows the build host by default because Homebrew binary
  libraries can require newer macOS versions; only override it when every linked library supports
  the requested minimum.
