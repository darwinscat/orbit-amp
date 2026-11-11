// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CabTables.h"

#include <BinaryData.h>
#include <GuitarCabData.h>
#include <panel/GuitarCatalog.h>

#include <algorithm>

namespace orbitamp::device
{

namespace
{
    bool holds (const cabir::Panel::Catalog& c, const std::string& box)
    {
        return std::any_of (c.cabs.begin(), c.cabs.end(), [&box] (const cabir::Panel::Cab& cab) { return cab.name.toStdString() == box; });
    }
}

CabKind kindOf (const std::string& box)
{
    if (holds (cabir::Panel::bassCatalog(), box))
        return CabKind::bass;
    if (holds (cabir::guitarCatalog(), box))
        return CabKind::guitar;
    return CabKind::none;
}

const cabir::synth::Table& tableOf (CabKind kind)
{
    static const auto bass = cabir::synth::parseTable (std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize));
    static const auto guitar = cabir::synth::parseTable (std::string (GuitarCabData::guitarcabinets_txt,
                                                                      (size_t) GuitarCabData::guitarcabinets_txtSize));
    return kind == CabKind::guitar ? guitar : bass;
}

const cabir::Panel::Catalog& catalogOf (CabKind kind)
{
    return kind == CabKind::guitar ? cabir::guitarCatalog() : cabir::Panel::bassCatalog();
}

std::optional<cabir::State> knownCabinet (const std::string& toml)
{
    auto s = cabir::fromToml (toml);
    if (! s)
        return std::nullopt;
    const auto kind = kindOf (s->box);
    if (kind == CabKind::none)
        return std::nullopt;
    const auto& mics = catalogOf (kind).mics;
    const auto knownMic = [&mics] (const cabir::Mic& m) { return m.model.empty() || mics.contains (cabir::ui::String (m.model)); };
    if (! knownMic (s->a) || ! knownMic (s->b))
        return std::nullopt;
    return s;
}

void drawCabinetsOf (CabKind kind)
{
    if (kind != CabKind::none)
        cabir::Panel::useCatalog (catalogOf (kind));
}

} // namespace orbitamp::device
