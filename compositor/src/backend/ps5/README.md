# PS5 backend boundary

This directory is reserved for the real wlroots PS5 backend. No PS5 display or
input backend is implemented or selected by the current compositor. The PS5
build currently offers the same headless and nested-Wayland backends as the host;
a successful cross-link is not evidence of console rendering or input.

The future backend must implement wlroots backend/output/input contracts using
VideoOut for output and scePad/sceKeyboard/sceMouse for input. Start with CPU
pixman composition into linear shared buffers, convert damaged regions to the
PS5 64KB_R_X layout, register buffers with VideoOut RegisterBuffers2, and drive
frame callbacks from flip completion/equeue rather than a free-running timer.
Do not introduce DRM/session dependencies or claim GPU support from the pixman
path. Follow PLAN.md M1 hardware probes before assuming buffer mapping,
VideoOut coexistence, process-shared memory or synchronization behavior.

Keep platform ownership, VideoOut buffer lifecycle and pad event translation
here; protocol globals, xdg-shell, seat policy and window management remain in
the platform-independent compositor. The real backend must propagate failures
and own teardown for buffers, event queues and input handles. Future direct PS5
launch also needs `/system_tmp` runtime/socket setup and the common/ps5log pull
logger because elfldr supplies no argv/environment.
