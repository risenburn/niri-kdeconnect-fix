// Real WaylandInput against an in-process isolated protocol server.
#include "input_state.hpp"
#include "virtual-keyboard-unstable-v1-server-protocol.h"
#include "wayland_input.hpp"
#include "wlr-virtual-pointer-unstable-v1-server-protocol.h"
#include <QCoreApplication>
#include <QThread>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <linux/input-event-codes.h>
#include <mutex>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <wayland-server.h>
struct Key {
  uint32_t code, state;
};
struct Capture {
  std::mutex mutex;
  std::vector<Key> keys;
  std::vector<uint32_t> mods;
  uint32_t x = 0, y = 0, w = 0, h = 0;
  std::atomic<int> frames = 0;
};
Capture capture;
void destroy(wl_client *, wl_resource *r) { wl_resource_destroy(r); }
void keyboardMap(wl_client *, wl_resource *, uint32_t, int32_t fd, uint32_t) {
  close(fd);
}
void keyboardKey(wl_client *, wl_resource *, uint32_t, uint32_t key,
                 uint32_t state) {
  std::lock_guard lock(capture.mutex);
  capture.keys.push_back({key, state});
}
void keyboardMods(wl_client *, wl_resource *, uint32_t d, uint32_t, uint32_t,
                  uint32_t) {
  std::lock_guard lock(capture.mutex);
  capture.mods.push_back(d);
}
const struct zwp_virtual_keyboard_v1_interface keyboardImpl = {
    keyboardMap, keyboardKey, keyboardMods, destroy};
void newKeyboard(wl_client *c, wl_resource *, wl_resource *, uint32_t id) {
  auto *r = wl_resource_create(c, &zwp_virtual_keyboard_v1_interface, 1, id);
  wl_resource_set_implementation(r, &keyboardImpl, nullptr, nullptr);
}
const struct zwp_virtual_keyboard_manager_v1_interface keyboardManagerImpl = {
    newKeyboard};
void bindKeyboard(wl_client *c, void *, uint32_t v, uint32_t id) {
  auto *r =
      wl_resource_create(c, &zwp_virtual_keyboard_manager_v1_interface, v, id);
  wl_resource_set_implementation(r, &keyboardManagerImpl, nullptr, nullptr);
}
void motion(wl_client *, wl_resource *, uint32_t, wl_fixed_t, wl_fixed_t) {}
void absolute(wl_client *, wl_resource *, uint32_t, uint32_t x, uint32_t y,
              uint32_t w, uint32_t h) {
  std::lock_guard lock(capture.mutex);
  capture.x = x;
  capture.y = y;
  capture.w = w;
  capture.h = h;
}
void button(wl_client *, wl_resource *, uint32_t, uint32_t, uint32_t) {}
void axis(wl_client *, wl_resource *, uint32_t, uint32_t, wl_fixed_t) {}
void frame(wl_client *, wl_resource *) { ++capture.frames; }
void source(wl_client *, wl_resource *, uint32_t) {}
void stop(wl_client *, wl_resource *, uint32_t, uint32_t) {}
void discrete(wl_client *, wl_resource *, uint32_t, uint32_t, wl_fixed_t,
              int32_t) {}
const struct zwlr_virtual_pointer_v1_interface pointerImpl = {
    motion, absolute, button, axis, frame, source, stop, discrete, destroy};
void newPointer(wl_client *c, wl_resource *, wl_resource *, uint32_t id) {
  auto *r = wl_resource_create(c, &zwlr_virtual_pointer_v1_interface, 2, id);
  wl_resource_set_implementation(r, &pointerImpl, nullptr, nullptr);
}
void newPointerOutput(wl_client *c, wl_resource *r, wl_resource *seat,
                      wl_resource *, uint32_t id) {
  newPointer(c, r, seat, id);
}
const struct zwlr_virtual_pointer_manager_v1_interface pointerManagerImpl = {
    newPointer, destroy, newPointerOutput};
void bindPointer(wl_client *c, void *, uint32_t v, uint32_t id) {
  auto *r =
      wl_resource_create(c, &zwlr_virtual_pointer_manager_v1_interface, v, id);
  wl_resource_set_implementation(r, &pointerManagerImpl, nullptr, nullptr);
}
void bindSeat(wl_client *c, void *, uint32_t v, uint32_t id) {
  auto *r = wl_resource_create(c, &wl_seat_interface, v, id);
  wl_resource_set_implementation(r, nullptr, nullptr, nullptr);
  wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_POINTER |
                                   WL_SEAT_CAPABILITY_KEYBOARD);
}

void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int stopServer(int fd, uint32_t, void *data) {
  char value;
  if (read(fd, &value, 1) == 1)
    wl_display_terminate(static_cast<wl_display *>(data));
  return 0;
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  int sockets[2], control[2];
  check(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) == 0,
        "socketpair");
  check(pipe(control) == 0, "control pipe");
  auto *server = wl_display_create();
  wl_global_create(server, &wl_seat_interface, 9, nullptr, bindSeat);
  wl_global_create(server, &zwp_virtual_keyboard_manager_v1_interface, 1,
                   nullptr, bindKeyboard);
  wl_global_create(server, &zwlr_virtual_pointer_manager_v1_interface, 2,
                   nullptr, bindPointer);
  wl_client_create(server, sockets[0]);
  auto *stopSource =
      wl_event_loop_add_fd(wl_display_get_event_loop(server), control[0],
                           WL_EVENT_READABLE, stopServer, server);
  setenv("WAYLAND_SOCKET", std::to_string(sockets[1]).c_str(), 1);
  std::thread thread([&] { wl_display_run(server); });
  int result = 0;
  try {
    nkcf::WaylandInput input;
    check(input.ensureReady(), "isolated input ready");
    const auto sync = [&] {
      const int before = capture.frames;
      check(input.pointerMotion(0, 0), "barrier motion");
      QElapsedTimer timer;
      timer.start();
      while (capture.frames == before && timer.elapsed() < 3000)
        QThread::msleep(1);
      check(capture.frames > before, "Wayland event barrier");
    };
    const auto released = [&] {
      sync();
      std::lock_guard lock(capture.mutex);
      check(!capture.mods.empty() && capture.mods.back() == 0,
            "no synthesized Shift remains pressed");
    };
    for (int repeats : {1, 2, 5}) {
      for (int i = 0; i < repeats; ++i)
        check(input.keyboardKeysym('A', true), "uppercase press/repeat");
      check(input.keyboardKeysym('A', false), "uppercase release");
      released();
    }
    check(input.keyboardKeysym('A', true), "first shifted keysym");
    check(input.keyboardKeysym('B', true), "second shifted keysym");
    check(input.keyboardKeysym('A', true), "repeat first shifted keysym");
    check(input.keyboardKeysym('A', false), "release first shifted keysym");
    sync();
    {
      std::lock_guard lock(capture.mutex);
      check(capture.mods.back() != 0, "other shifted keysym retains Shift");
    }
    check(input.keyboardKeysym('B', false), "release final shifted keysym");
    released();
    for (int i = 0; i < 3; ++i) {
      check(input.keyboardKeysym('A', true), "balanced press");
      check(input.keyboardKeysym('A', false), "balanced release");
    }
    released();
    for (bool releaseRawFirst : {false, true}) {
      nkcf::InputState state(input);
      int rawOwner, symbolOwner;
      check(state.keyboardKeycode(&rawOwner, KEY_LEFTSHIFT, true),
            "raw owner Shift");
      check(state.keyboardKeycode(&rawOwner, KEY_A, true), "raw owner A");
      check(state.keyboardKeysym(&symbolOwner, 'A', true),
            "symbol owner uppercase A");
      check(state.keyboardKeysym(&symbolOwner, 'A', true),
            "symbol owner repeat");
      state.releaseOwner(releaseRawFirst ? &rawOwner : &symbolOwner);
      sync();
      {
        std::lock_guard lock(capture.mutex);
        check(capture.mods.back() != 0,
              "remaining owner keeps real XKB Shift depressed");
      }
      state.releaseOwner(releaseRawFirst ? &symbolOwner : &rawOwner);
      released();
    }
    {
      nkcf::InputState state(input);
      int owner;
      std::size_t before;
      {
        std::lock_guard lock(capture.mutex);
        before = capture.keys.size();
      }
      for (int i = 0; i < 3; ++i)
        check(state.keyboardKeysym(&owner, 'A', true),
              "owned uppercase repeat");
      check(state.keyboardKeysym(&owner, 'A', false),
            "owned uppercase release");
      released();
      {
        std::lock_guard lock(capture.mutex);
        const std::vector<Key> expected{{KEY_LEFTSHIFT, 1}, {KEY_A, 1},
                                        {KEY_A, 1},         {KEY_A, 1},
                                        {KEY_A, 0},         {KEY_LEFTSHIFT, 0}};
        check(capture.keys.size() == before + expected.size(),
              "owned keysym repeats reach Wayland");
        for (std::size_t i = 0; i < expected.size(); ++i)
          check(capture.keys[before + i].code == expected[i].code &&
                    capture.keys[before + i].state == expected[i].state,
                "owned keysym repeat event ordering");
      }
    }
    for (uint32_t modifier : {KEY_LEFTSHIFT, KEY_LEFTCTRL, KEY_LEFTALT}) {
      nkcf::InputState state(input);
      int owner;
      for (int i = 0; i < 3; ++i)
        check(state.keyboardKeycode(&owner, modifier, true),
              "owned modifier repeat");
      check(state.keyboardKeycode(&owner, modifier, false),
            "owned modifier release");
      released();
    }
    std::cout << "Shifted keysym repeat and release tests passed on isolated "
                 "Wayland server\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  check(write(control[1], "q", 1) == 1, "stop server");
  thread.join();
  wl_event_source_remove(stopSource);
  wl_display_destroy_clients(server);
  wl_display_destroy(server);
  close(control[0]);
  close(control[1]);
  return result;
}
