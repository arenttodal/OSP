# The shared ANDOR interface (shared/andor-ui): the components, the look and the rendered
# assets ANDOR OSP shows, for OSP and every product built from the ANDOR Instrument Shell.
#
# JUCE modules compile into each plugin target with that target's own configuration, so the
# interface is shared as sources, not as a prebuilt library: andor_ui_add_to (target) adds the
# sources, the include directory and the embedded assets (fonts, knob sprites, ceramic skin)
# to one plugin target. Every product therefore draws with the very same code and pixels.

set(ANDOR_UI_DIR ${CMAKE_CURRENT_LIST_DIR})

set(ANDOR_UI_SOURCES
    ${ANDOR_UI_DIR}/Source/InstrumentHost.cpp
    ${ANDOR_UI_DIR}/Source/AndorParameters.cpp
    ${ANDOR_UI_DIR}/Source/OspLookAndFeel.cpp
    ${ANDOR_UI_DIR}/Source/Design.cpp
    ${ANDOR_UI_DIR}/Source/SpriteKnobRenderer.cpp
    ${ANDOR_UI_DIR}/Source/CeramicSkin.cpp
    ${ANDOR_UI_DIR}/Source/ShapingPopups.cpp
    ${ANDOR_UI_DIR}/Source/MacroPopups.cpp
    ${ANDOR_UI_DIR}/Source/EngineCard.cpp
    ${ANDOR_UI_DIR}/Source/MainSections.cpp
    ${ANDOR_UI_DIR}/Source/ArpSection.cpp
    ${ANDOR_UI_DIR}/Source/ModulationPanel.cpp
    ${ANDOR_UI_DIR}/Source/EqEditor.cpp
    ${ANDOR_UI_DIR}/Source/NoteMapEditor.cpp
    ${ANDOR_UI_DIR}/Source/KeyDnaEditor.cpp
    ${ANDOR_UI_DIR}/Source/InstrumentFrame.cpp
    # The A/B/C sources: import, analysis and the playable model a card shows (any product
    # that plays dropped sounds; a background thread, never the audio thread).
    ${ANDOR_UI_DIR}/Source/InstrumentLoader.cpp)

# Embedded typefaces (SIL Open Font License: Resources/fonts/*-OFL.txt): Inter for the
# interface, with tabular figures as its default digits (values line up as they change), and
# Outfit for the ANDOR identity. Rendered controls and surfaces come from tools/blender-assets.
# One binary-data target serves every product (HEADER_NAME BinaryData.h, NAMESPACE BinaryData,
# as the components expect).
if(NOT TARGET AndorUiData)
    juce_add_binary_data(AndorUiData
        HEADER_NAME BinaryData.h
        NAMESPACE BinaryData
        SOURCES
            ${ANDOR_UI_DIR}/Resources/fonts/Inter-Light.ttf
            ${ANDOR_UI_DIR}/Resources/fonts/Inter-Regular.ttf
            ${ANDOR_UI_DIR}/Resources/fonts/Inter-Medium.ttf
            ${ANDOR_UI_DIR}/Resources/fonts/Inter-SemiBold.ttf
            ${ANDOR_UI_DIR}/Resources/fonts/Inter-Bold.ttf
            ${ANDOR_UI_DIR}/Resources/fonts/Inter-ExtraBold.ttf
            ${ANDOR_UI_DIR}/Resources/fonts/Outfit-ExtraBold.ttf
            ${ANDOR_UI_DIR}/Resources/fonts/Outfit-ExtraLight.ttf
            # One zip of frames per size; SpriteKnobRenderer registers every "<name>-<size>.zip".
            ${ANDOR_UI_DIR}/Resources/knobs/macro-ivory/macro-ivory-176.zip
            ${ANDOR_UI_DIR}/Resources/knobs/macro-ivory/macro-ivory-352.zip
            # The ceramic skin's rendered surfaces (tools/blender-assets/skin): CeramicSkin.
            ${ANDOR_UI_DIR}/Resources/skin/ceramic-skin.zip)
endif()

function(andor_ui_add_to target)
    target_sources(${target} PRIVATE ${ANDOR_UI_SOURCES})
    target_include_directories(${target} PRIVATE ${ANDOR_UI_DIR}/Source)
    target_link_libraries(${target} PRIVATE AndorUiData)
endfunction()
