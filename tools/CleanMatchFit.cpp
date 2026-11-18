// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// CLEANMATCH <pack folder>... — fits every capture of each pack its crossover clean match, once, and writes it into
// the pack's rig.json as namz 5.2's `files[].clean_match` (NAMZ-FORMAT.md), so an amp that loads the pack has it at
// once instead of fitting it at every load (CapturedBlock::askFit). Run by hand on the tree's folders
// (resources/set/Devices/<name>.orbitrig) when a pack's models change; nobody else needs it.
//
// THE FIT IS THE AMP'S, call for call: the pack's manifest read as DeviceLibrary reads it, its first NAM stage as
// RigPlayer::load plays it, the player's table for that stage (AlignmentTable::fromDevice) as the start, the stage's
// own input level, and each DISTINCT file fitted alone at the rate the amp fits a model that states none
// (felitronics::rigplayer::measureCleanMatch, as CapturedBlock::fitOne calls it). A match already in the pack is no
// starting point — measureCleanMatch sets it aside — so a second run fits the same and changes nothing.
//
// EVERY FILE GETS ONE. Where the fit beats nothing (measureCleanMatch states no match for it), the match written is
// the file's own reading — its `input_lag_samples` and `polarity`, no all-passes — which a player taps the clean leg
// by anyway: the same sound, and the pack says "fitted" instead of "not measured", so an amp fits nothing for it.
//
// ONLY THE KEY MOVES. The packs are laid out by hand (namz's writer wraps their lines elsewhere), so the manifest is
// not written again: each file entry's text gets its `clean_match` as the entry's last member, on a line of its own
// under the entry's others (or the one it had is replaced), and before anything is written the tool proves that
// nothing else moved — the edited manifest loads, every entry reads back exactly the match intended, and the two
// documents are the same JSON once `clean_match` is taken out of both.

#include <felitronics/nam/NamStage.h>
#include <felitronics/rigplayer/AlignmentTable.h>
#include <namz_rig_load.h>
#include <namz_rig_write.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace rp = felitronics::rigplayer;

    // The rate a capture that states none is fitted at: CapturedBlock::fitRate.
    constexpr double fitRate = 48000.0;

    bool readFile (const fs::path& p, std::string& out)
    {
        std::ifstream in (p, std::ios::binary);
        if (! in) return false;
        out.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
        return true;
    }

    // ---- the manifest's text, read only as far as an edit needs ----

    std::size_t skipSpace (const std::string& t, std::size_t i)
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i;
        return i;
    }

    // From an opening quote to just past its closing one.
    std::size_t skipString (const std::string& t, std::size_t i)
    {
        for (++i; i < t.size(); ++i)
        {
            if (t[i] == '\\') { ++i; continue; }
            if (t[i] == '"') return i + 1;
        }
        return std::string::npos;
    }

    // From the first character of a value to just past it.
    std::size_t skipValue (const std::string& t, std::size_t i)
    {
        if (i >= t.size()) return std::string::npos;
        if (t[i] == '"') return skipString (t, i);
        if (t[i] == '{' || t[i] == '[')
        {
            int depth = 0;
            for (; i < t.size(); ++i)
            {
                if (t[i] == '"') { i = skipString (t, i); if (i == std::string::npos) return i; --i; continue; }
                if (t[i] == '{' || t[i] == '[') ++depth;
                if (t[i] == '}' || t[i] == ']') { if (--depth == 0) return i + 1; }
            }
            return std::string::npos;
        }
        while (i < t.size() && t[i] != ',' && t[i] != '}' && t[i] != ']' && t[i] != ' ' && t[i] != '\n' && t[i] != '\r') ++i;
        return i;
    }

    struct Member { std::string key; std::size_t valueStart = 0, valueEnd = 0; };

    // The members of the object whose `{` is at `open`; empty when the text there is not one.
    std::vector<Member> membersOf (const std::string& t, std::size_t open, std::size_t& close)
    {
        std::vector<Member> out;
        std::size_t i = skipSpace (t, open + 1);
        while (i < t.size() && t[i] == '"')
        {
            const auto keyEnd = skipString (t, i);
            if (keyEnd == std::string::npos) return {};
            Member m;
            m.key = nlohmann::json::parse (t.substr (i, keyEnd - i)).get<std::string>();
            i = skipSpace (t, keyEnd);
            if (i >= t.size() || t[i] != ':') return {};
            m.valueStart = skipSpace (t, i + 1);
            m.valueEnd = skipValue (t, m.valueStart);
            if (m.valueEnd == std::string::npos) return {};
            out.push_back (m);
            i = skipSpace (t, m.valueEnd);
            if (i < t.size() && t[i] == ',') i = skipSpace (t, i + 1);
        }
        if (i >= t.size() || t[i] != '}') return {};
        close = i;
        return out;
    }

    // ---- the match ----

    // A match as the pack states it, spelled as namz's writer spells a leaf: `{ "lag_samples": 10, "polarity": -1,
    // "allpass_hz": [39.0, 132.2] }`, its numbers as nlohmann writes them (the shortest that reads back the same double).
    std::string spell (const namz::rig::FileEntry::CleanMatch& m)
    {
        std::string s = "{ \"lag_samples\": " + std::to_string (m.lagSamples) + ", \"polarity\": " + std::to_string (m.polarity);
        if (! m.allpassHz.empty())
        {
            s += ", \"allpass_hz\": [";
            for (std::size_t k = 0; k < m.allpassHz.size(); ++k)
                s += (k > 0 ? ", " : "") + nlohmann::json (m.allpassHz[k]).dump();
            s += "]";
        }
        return s + " }";
    }

    // What the format reads (NAMZ-FORMAT.md, clean_match): a lag 0 to 48000, a polarity of one, at most two corners,
    // each finite and at least 1 Hz. A match outside it would be dropped by every reader, so it is never written.
    bool readable (const namz::rig::FileEntry::CleanMatch& m)
    {
        if (m.lagSamples < 0 || m.lagSamples > namz::rig::kMaxLagSamples) return false;
        if (m.polarity != 1 && m.polarity != -1) return false;
        if (m.allpassHz.size() > 2) return false;
        for (const double hz : m.allpassHz)
            if (! std::isfinite (hz) || hz < namz::rig::kMinCleanMatchHz) return false;
        return true;
    }

    bool same (const std::optional<namz::rig::FileEntry::CleanMatch>& a, const namz::rig::FileEntry::CleanMatch& b)
    {
        return a && a->lagSamples == b.lagSamples && a->polarity == b.polarity && a->allpassHz == b.allpassHz;
    }

    const namz::rig::Stage* firstNam (const namz::rig::Rig& rig)
    {
        for (const auto& st : rig.chain)
            if (st.kind == namz::rig::StageKind::Nam) return &st;
        return nullptr;
    }

    // The document with every file entry's `clean_match` taken out: what must not have moved.
    nlohmann::json withoutMatches (nlohmann::json j)
    {
        if (j.contains ("chain") && j["chain"].is_array())
            for (auto& st : j["chain"])
                if (st.is_object() && st.contains ("files") && st["files"].is_array())
                    for (auto& f : st["files"])
                        if (f.is_object()) f.erase ("clean_match");
        return j;
    }

    int fail (const fs::path& pack, const std::string& why)
    {
        std::fprintf (stderr, "cleanmatch: %s: %s\n", pack.filename().string().c_str(), why.c_str());
        return 1;
    }

    int fitPack (const fs::path& pack)
    {
        const auto manifestPath = pack / "rig.json";
        std::string text;
        if (! readFile (manifestPath, text)) return fail (pack, "no rig.json");

        bool ok = false;
        const auto rig = namz::rig::loadRigManifest (text, &ok);
        const auto* stage = firstNam (rig);
        if (! ok || stage == nullptr || stage->device.files.empty()) return fail (pack, "no NAM stage with files");

        const rp::ModelSource source = [&pack] (const std::string& id) -> std::vector<std::byte>
        {
            std::string bytes;
            if (! readFile (pack / id, bytes)) return {};
            const auto* p = reinterpret_cast<const std::byte*> (bytes.data());
            return std::vector<std::byte> (p, p + bytes.size());
        };

        // The player's table for this stage, as RigPlayer::load makes it and CapturedBlock hands it to the fit; and the
        // same without the pack's matches, for the readings an unfitted file states.
        const auto start = rp::AlignmentTable::fromDevice (stage->device);
        auto readings = start;
        readings.cleanMatchByFile.clear();

        std::map<std::string, namz::rig::FileEntry::CleanMatch> want;
        std::printf ("%s (input_db %.2f)\n", pack.filename().string().c_str(), stage->inputDb);
        for (const auto& f : stage->device.files)
        {
            if (f.id.empty()) return fail (pack, "a file entry with no file");
            if (want.count (f.id) > 0) continue;

            // The bytes must load: a file the fit could not read must not be stated as fitted.
            const auto bytes = source (f.id);
            felitronics::nam::NamStage probe;
            probe.prepare (fitRate, 512);
            if (bytes.empty() || ! probe.loadModelFromMemory (bytes.data(), bytes.size()))
                return fail (pack, f.id + " does not load");
            const double ownRate = probe.modelSampleRate();

            namz::rig::Device one = stage->device;
            one.files = { f };
            const auto found = rp::measureCleanMatch (one, source, start, stage->inputDb, fitRate);

            namz::rig::FileEntry::CleanMatch m;
            const char* how = "fitted";
            if (const auto hit = found.find (f.id); hit != found.end())
            {
                m.lagSamples = hit->second.lag;
                m.polarity = hit->second.inverted ? -1 : 1;
                for (const double hz : hit->second.allpassHz)
                    if (hz > 0.0) m.allpassHz.push_back (hz);
            }
            else
            {
                // Nothing beats the file's own reading: that is its match. It must be stated whole, as the player
                // would tap the clean leg by it — an absolute lag and a polarity, not a lag against another file.
                if (! f.inputLagSamples || ! f.polarity) return fail (pack, f.id + ": no fit and no input_lag_samples / polarity to state");
                m.lagSamples = readings.lagOf (f.id, fitRate, fitRate);
                const auto inv = readings.invertedByFile.find (f.id);
                m.polarity = inv != readings.invertedByFile.end() && inv->second ? -1 : 1;
                how = "its reading";
            }
            if (! readable (m)) return fail (pack, f.id + ": the match is outside what the format reads: " + spell (m));

            std::printf ("  %-32s %6.0f Hz  %-12s %s\n", f.id.c_str(), ownRate > 0.0 ? ownRate : fitRate, how, spell (m).c_str());
            if (! (ownRate > 0.0)) std::printf ("    (states no rate: fitted at %.0f Hz, as the amp fits it)\n", fitRate);
            want[f.id] = m;
        }

        // Every entry of the stage, in the text: `{ "file": "<id>", ...`. One stage, every entry found, none twice.
        struct Edit { std::size_t at, cut; std::string put; };
        std::vector<Edit> edits;
        std::size_t entries = 0;
        for (std::size_t at = text.find ("\"file\""); at != std::string::npos; at = text.find ("\"file\"", at + 1))
        {
            std::size_t open = at;
            while (open > 0 && (text[open - 1] == ' ' || text[open - 1] == '\n' || text[open - 1] == '\r' || text[open - 1] == '\t')) --open;
            if (open == 0 || text[open - 1] != '{') continue;   // `"file"` not as an entry's first key: not an entry
            --open;
            std::size_t close = 0;
            const auto members = membersOf (text, open, close);
            if (members.empty() || members.front().key != "file") return fail (pack, "a file entry the tool cannot read");
            const auto id = nlohmann::json::parse (text.substr (members.front().valueStart,
                                                                members.front().valueEnd - members.front().valueStart)).get<std::string>();
            const auto m = want.find (id);
            if (m == want.end()) return fail (pack, "an entry outside the first NAM stage: " + id);
            ++entries;

            const auto spelled = spell (m->second);
            const Member* had = nullptr;
            for (const auto& mm : members) if (mm.key == "clean_match") had = &mm;
            if (had != nullptr)
                edits.push_back ({ had->valueStart, had->valueEnd - had->valueStart, spelled });
            else
            {
                const auto lineStart = text.rfind ('\n', open);
                const std::size_t column = open - (lineStart == std::string::npos ? 0 : lineStart + 1);
                edits.push_back ({ members.back().valueEnd, 0,
                                   ",\n" + std::string (column + 2, ' ') + "\"clean_match\": " + spelled });
            }
        }
        if (entries != stage->device.files.size())
            return fail (pack, "found " + std::to_string (entries) + " file entries in the text, the stage has "
                                   + std::to_string (stage->device.files.size()));

        std::string edited = text;
        for (auto e = edits.rbegin(); e != edits.rend(); ++e)
            edited.replace (e->at, e->cut, e->put);

        // THE PROOF, before a byte is written. It loads; every entry reads back the match intended; namz reads the
        // same pack as the original with the matches set; and with `clean_match` out of both, the same JSON.
        bool okAfter = false;
        const auto after = namz::rig::loadRigManifest (edited, &okAfter);
        const auto* stageAfter = firstNam (after);
        if (! okAfter || stageAfter == nullptr || stageAfter->device.files.size() != stage->device.files.size())
            return fail (pack, "the edited manifest does not load");
        for (const auto& f : stageAfter->device.files)
            if (! same (f.cleanMatch, want.at (f.id))) return fail (pack, f.id + ": its match does not read back");

        auto expected = rig;
        for (auto& st : expected.chain)
            if (&st - expected.chain.data() == stage - rig.chain.data())
                for (auto& f : st.device.files) f.cleanMatch = want.at (f.id);
        if (namz::rig::writeManifest (expected) != namz::rig::writeManifest (after))
            return fail (pack, "namz reads more than the matches moved");
        if (withoutMatches (nlohmann::json::parse (text)) != withoutMatches (nlohmann::json::parse (edited)))
            return fail (pack, "the JSON moved beyond clean_match");

        if (edited == text)
        {
            std::printf ("  unchanged\n");
            return 0;
        }
        std::ofstream out (manifestPath, std::ios::binary | std::ios::trunc);
        out << edited;
        if (! out) return fail (pack, "could not write rig.json");
        std::printf ("  written: %zu entries, %zu bytes -> %zu\n", entries, text.size(), edited.size());
        return 0;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: cleanmatch <pack folder>...\n");
        return 2;
    }
    int failed = 0;
    for (int i = 1; i < argc; ++i)
        failed += fitPack (fs::path (argv[i]));
    return failed == 0 ? 0 : 1;
}
