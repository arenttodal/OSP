#pragma once

#include "LessonTypes.h"

namespace osp::plugin
{

/**
    ANDOR OSP's ANDOR Learn catalog (shared/andor-learn): Info's explanations of OSP's own controls
    and a short Quick Start. OSP has no AUX D, no SUB and no FX routing view: nothing here speaks
    of them. Its targets are registered by OspAudioProcessorEditor::createLearn.
*/
andor::learn::Catalog ospLearnCatalog();

} // namespace osp::plugin
