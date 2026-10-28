#pragma once

#include "core/AudioData.h"
#include "engine/AtlasSet.h"
#include "model/NoteAtlas.h"

#include <memory>
#include <string>
#include <vector>

namespace osp::research::atlasfixtures
{

/**
    Prepared Note Atlases on deterministic synthetic recordings (unified engine Phase 3): each
    recording is generated from fixed seeds, each atlas has deliberately assigned regions (no
    analysis). They stand in for the real-world cases in the automated tests; private
    recordings are only used locally (docs/research/unified-andor/phase-3-report.md).

      sung-four        a sung phrase, four distinct pitches with short gaps
      repeated-note    three recordings of the same note (round robin)
      velocity-layers  one key recorded soft (two takes) and loud (two takes)
      bowed-phrase     a short bowed (saw + bow noise, swelling) phrase, three notes
      silence-gaps     plucked notes with long silences between them
      legato           four notes joined without gaps: boundaries on adjacent frames
      stereo-wide      notes from a recording with substantial stereo information
      out-of-tune      notes 35 cents sharp, 40 cents flat, 12 cents sharp
      missing-notes    only C3, C4, C5 recorded (FILL's job)
      boundary-probe   a 440 Hz note right next to a 3 kHz note (grains must not leak)
*/
struct Fixture
{
    std::string name;
    std::string description;
    std::vector<atlasset::SourceAudio> audio;   ///< one per atlas source
    atlas::NoteAtlas atlas;
};

std::vector<std::string> names();
/** Builds a fixture by name (empty atlas for an unknown name). */
Fixture make (const std::string& name);
/** `count` notes cycling over `keys` keys from C3 (round robins on repeated keys), each
    `noteSeconds` long: the memory and lookup benchmarks' atlases. */
Fixture manyRegions (int count, double noteSeconds, int keys = 12, int channels = 2);

/** "sha256:" of the audio's float32 samples (channel by channel): an in-memory asset's identity. */
std::string contentHashOf (const AudioData& audio);

/** A source record for `audio` (identity, rate, channels, length). */
atlas::SourceAsset assetFor (const std::string& id, const std::string& name, const AudioData& audio,
                             atlas::AssetOrigin origin = atlas::AssetOrigin::fileImport);

/** The fixture's set with the default build options (continuation on). */
atlasset::BuildResult build (const Fixture& fixture, const atlasset::BuildOptions& options = {});

} // namespace osp::research::atlasfixtures
