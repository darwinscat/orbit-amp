// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace orbitamp
{

/** ONE OF SOMETHING FOR EACH PRODUCT IN A PROCESS — what was once a single function-local static, read
    once and kept: a product's settings store, its cached switches, its cabinet shelf, its renders. Made
    the first time a product asks for it, by `make`, and kept, by the product's NAME (Product.h): two
    processors of one amp share it, two amps never do. Thread-safe; a reader on the audio thread holds on
    to what it was handed rather than asking here every block. */
template <typename T>
class PerProduct
{
public:
    template <typename Make>
    T& of (const std::string& product, Make&& make)
    {
        const std::scoped_lock lock (mutex);
        auto& made = each[product];
        if (made == nullptr)
            made.reset (new T (make()));
        return *made;
    }

private:
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<T>> each;
};

} // namespace orbitamp
