// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// librw's <rw.h> has no include guard, so a test that takes it through two support headers breaks on redefinitions:
// the support headers and tests that share a translation unit include this one instead.

#include <rw.h>
