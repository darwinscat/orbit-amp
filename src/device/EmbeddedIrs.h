// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <map>

namespace orbitamp::device
{

/** The player's own cabinet IRs, carried by CONTENT rather than by where they sat on disk.

    A preset or a session that uses an IR of the player's own takes the IR with it — the file's
    bytes, whole — so it sounds the same on another machine, or after the file has been renamed,
    moved or thrown away. The library folder is only where an IR is picked FROM.

    THE BYTES NEVER RIDE IN THE LIVE TREE. The history snapshots that tree on every tick and keeps
    a copy per register and per undo step; a megabyte living there would be copied and compared all
    day. The tree carries a KEY into this store instead, and the bytes join the tree only on the
    way to disk (`pack`) and leave it again on the way back (`unpack`).

    Nothing is ever dropped from the store while the plugin runs: an undo step can still name an IR
    the player moved away from, and the steps are not ours to look inside. It holds what was picked
    in one sitting — small files, a handful of them. And because nothing leaves, a pointer `find`
    hands out stays good for the store's whole life.

    Locked inside: a host may save a session from whatever thread it likes, while the message thread
    is adding the IR just picked. */
class EmbeddedIrs
{
public:
    /** The node a saved tree carries its IRs in, one child per IR. */
    static constexpr const char* treeType  = "EmbeddedIRs";
    static constexpr const char* entryType = "IR";

    /** A cabinet IR is a second or two of audio. Anything past this is not one, and embedding it
        would put megabytes into every preset and every register of a session. */
    static constexpr juce::int64 maxBytes = 8 * 1024 * 1024;

    /** The key is the content's: the same bytes always get the same key, wherever they came from,
        so an IR saved in four registers is embedded once. FNV-1a over the bytes plus their length —
        an identity for files a player picked, not a defence against anyone. */
    static juce::String keyOf (const juce::MemoryBlock& bytes)
    {
        juce::uint64 h = 0xcbf29ce484222325ull;
        const auto* p = static_cast<const juce::uint8*> (bytes.getData());

        for (size_t i = 0; i < bytes.getSize(); ++i)
        {
            h ^= p[i];
            h *= 0x100000001b3ull;
        }

        return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16)
               + "-" + juce::String ((juce::int64) bytes.getSize());
    }

    /** Keeps the bytes and returns their key; empty for nothing, or for more than an IR can be. */
    juce::String add (juce::MemoryBlock bytes)
    {
        if (bytes.getSize() == 0 || (juce::int64) bytes.getSize() > maxBytes)
            return {};

        auto key = keyOf (bytes);

        const juce::ScopedLock sl (lock);
        store.try_emplace (key, std::move (bytes));
        return key;
    }

    const juce::MemoryBlock* find (const juce::String& key) const
    {
        const juce::ScopedLock sl (lock);
        const auto it = store.find (key);
        return it != store.end() ? &it->second : nullptr;
    }

    /** The IRs behind these keys, as a node to append to a tree on its way to disk. Keys the store
        does not hold are skipped; so are repeats. Invalid when there is nothing to carry. */
    juce::ValueTree pack (const juce::StringArray& keys) const
    {
        juce::ValueTree node (treeType);
        juce::StringArray done;

        for (const auto& key : keys)
            if (const auto* bytes = find (key); bytes != nullptr && ! done.contains (key))
            {
                // A MemoryBlock property is written as base64 by ValueTree's own XML, and read back
                // as a MemoryBlock — the one binary spelling JUCE already round-trips.
                juce::ValueTree ir (entryType);
                ir.setProperty ("data", juce::var (*bytes), nullptr);
                node.appendChild (ir, nullptr);
                done.add (key);
            }

        return node.getNumChildren() > 0 ? node : juce::ValueTree();
    }

    /** Takes every embedded IR out of `tree`'s top level into the store and removes the node, so
        what is left is a plain state again. Each IR is keyed by what it IS, never by what the file
        claims, so a doctored key cannot make one IR answer for another. */
    void unpack (juce::ValueTree& tree)
    {
        for (int i = tree.getNumChildren(); --i >= 0;)
        {
            const auto node = tree.getChild (i);
            if (! node.hasType (treeType))
                continue;

            for (const auto& ir : node)
                if (const auto* bytes = ir.getProperty ("data").getBinaryData())
                    add (*bytes);

            tree.removeChild (i, nullptr);
        }
    }

private:
    juce::CriticalSection lock;
    std::map<juce::String, juce::MemoryBlock> store;
};

} // namespace orbitamp::device
