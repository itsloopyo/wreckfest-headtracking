// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The one log line that tells a user which keys their config ended up on. What
// this locks is the contract that holds on every keyboard layout: a name is
// always produced. What each key is CALLED is the layout's business, so nothing
// here asserts a particular string - that would pass on one machine and fail on
// the next.

#include "hotkey_names.h"

#include "test_support.h"

#include <cstdio>
#include <string>

using namespace wf_ht;
using wf_test::Check;

namespace {

// Every key the shipped INI binds by default, plus the ends of the range the
// config layer will accept.
const int kKeysToName[] = {
    0x23,  // End, the default toggle
    0x21,  // Page Up, the default mode cycle
    0x59,  // Y, the toggle chord
    0x47,  // G, the mode cycle chord
    0x01, 0x70, 0xFE,
};

void EveryKeyGetsAName() {
    std::printf("Every bindable key names itself\n");

    for (int vk : kKeysToName) {
        const std::string name = HotkeyName(vk);
        Check(!name.empty(), "the key produces a non-empty name");
    }
}

void AnUnnameableKeyFallsBackToItsCode() {
    // 0x07 is undefined in the virtual key table, so no layout has a name for
    // it. The code is what the user typed into the INI, so it is what gets
    // reported back.
    std::printf("A key with no name reports its code\n");

    const std::string name = HotkeyName(0x07);
    Check(!name.empty(), "an unnameable key still produces something");
    Check(name.rfind("0x", 0) == 0, "and that something is the hex code");
}

}  // namespace

int main() {
    std::printf("Wreckfest head tracking - hotkey name tests\n");
    std::printf("=======================================================\n");
    EveryKeyGetsAName();
    AnUnnameableKeyFallsBackToItsCode();
    return wf_test::Summary("hotkey names");
}
