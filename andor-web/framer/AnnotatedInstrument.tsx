// AnnotatedInstrument — the hero's genuine capture with numbered markers pinned to real controls
// (coordinates in the capture's own editor pixels, e.g. 1448 × 1086). Each marker is a 44 px
// button that fires its own event, so in Framer you wire marker 1 to "Open Overlay → Your
// recording", marker 2 to the LIFE overlay, and so on. Markers fade in once the picture settles.
// andor-web framer bundle v1.0.0 (port of prototype/gallery/Annotated.tsx, bare mode)

import * as React from "react"
import { addPropertyControls, ControlType } from "framer"
import { color, font, motion } from "./ANDOR_Tokens.tsx"

const { useEffect, useId, useState } = React

interface Marker { n: number; label: string; x: number; y: number }

// Centres of the real components in the three-layer capture (web-08-hero-3L-bounds.json).
const DEFAULT_MARKERS: Marker[] = [
    { n: 1, label: "Your recording", x: 215, y: 300 },
    { n: 2, label: "LIFE", x: 128, y: 815 },
    { n: 3, label: "SYNTH + IMPRINT", x: 1336, y: 206 },
    { n: 4, label: "REIMAGINED", x: 884, y: 515 },
    { n: 5, label: "SPACE", x: 858, y: 815 },
    { n: 6, label: "Keyboard + arpeggiator", x: 1100, y: 965 },
]

/**
 * @framerSupportedLayoutWidth any-prefer-fixed
 * @framerSupportedLayoutHeight auto
 * @framerIntrinsicWidth 660
 */
export default function AnnotatedInstrument(props: any) {
    const { image, markers = DEFAULT_MARKERS, editorWidth = 1448, editorHeight = 1086, delay = 1500, sizes = "(max-width: 1100px) 94vw, 48vw" } = props
    const uid = useId().replace(/:/g, "")
    const [shown, setShown] = useState(false)
    useEffect(() => {
        const reduce = window.matchMedia("(prefers-reduced-motion: reduce)").matches
        const t = setTimeout(() => setShown(true), reduce ? 0 : delay)
        return () => clearTimeout(t)
    }, [delay])
    const fire = (i: number) => props[`onMarker${i + 1}`]?.()
    const css = `.ai-${uid} .ai-dot::before { content: ""; position: absolute; left: 9px; top: 9px; width: 26px; height: 26px; border-radius: 50%; background: ${color.text}; border: 2px solid ${color.bgElev}; box-sizing: border-box; box-shadow: 0 3px 10px rgba(36,39,35,0.35); transition: transform ${motion.d2}ms ${motion.easeOut}, background-color ${motion.d2}ms; }
.ai-${uid} .ai-dot:hover::before, .ai-${uid} .ai-dot:focus-visible::before { transform: scale(1.18); background: ${color.accentStrong}; }
.ai-${uid} .ai-dot:focus-visible { outline: 2px solid ${color.accentStrong}; outline-offset: -4px; }`
    return (
        <div className={`ai-${uid}`} style={{ position: "relative", width: "100%" }}>
            <style>{css}</style>
            {image?.src ? (
                <img src={image.src} srcSet={image.srcSet} sizes={sizes} alt={image.alt ?? "ANDOR OSP"} width={editorWidth} height={editorHeight} decoding="async" {...({ fetchpriority: "high" } as any)} draggable={false}
                    style={{ display: "block", width: "100%", height: "auto", borderRadius: 7, boxShadow: "0 0 0 1px rgba(36,39,35,0.07), 0 1px 1px rgba(46,38,28,.06), 0 6px 14px -6px rgba(46,38,28,.16), 0 28px 60px -22px rgba(46,38,28,.28), 0 70px 120px -50px rgba(46,38,28,.3)" }} />
            ) : (
                <div role="img" aria-label="Product image missing" style={{ aspectRatio: `${editorWidth} / ${editorHeight}`, border: `1px dashed ${color.borderStrong}`, borderRadius: 8, display: "grid", placeItems: "center", fontFamily: font.sans, color: color.text2 }}>Product image missing</div>
            )}
            {markers.map((m: Marker, i: number) => (
                <button key={i} type="button" className="ai-dot" onClick={() => fire(i)} aria-haspopup="dialog" aria-label={`${m.n}. ${m.label}: look closer`}
                    style={{ position: "absolute", left: `${(m.x / editorWidth) * 100}%`, top: `${(m.y / editorHeight) * 100}%`, width: 44, height: 44, margin: "-22px 0 0 -22px", padding: 0, border: 0, background: "none", borderRadius: "50%", cursor: "pointer",
                        opacity: shown ? 1 : 0, transition: `opacity ${motion.d3}ms ${motion.easeOut}` }}>
                    <span aria-hidden="true" style={{ position: "absolute", left: 9, top: 9, width: 26, height: 26, display: "grid", placeItems: "center", color: color.bg, fontFamily: font.mono, fontSize: 12, zIndex: 1 }}>{m.n}</span>
                </button>
            ))}
        </div>
    )
}

addPropertyControls(AnnotatedInstrument, {
    image: { type: ControlType.ResponsiveImage, title: "Capture" },
    editorWidth: { type: ControlType.Number, title: "Editor W", defaultValue: 1448, min: 100, max: 4000, step: 1, description: "The capture's editor size in 1x pixels. Marker coordinates use the same space." },
    editorHeight: { type: ControlType.Number, title: "Editor H", defaultValue: 1086, min: 100, max: 4000, step: 1 },
    markers: {
        type: ControlType.Array, title: "Markers", maxCount: 8,
        control: { type: ControlType.Object, controls: {
            n: { type: ControlType.Number, title: "Number", min: 1, max: 9, step: 1, displayStepper: true, defaultValue: 1 },
            label: { type: ControlType.String, title: "Label", defaultValue: "Your recording" },
            x: { type: ControlType.Number, title: "X", min: 0, max: 4000, step: 1, defaultValue: 215 },
            y: { type: ControlType.Number, title: "Y", min: 0, max: 4000, step: 1, defaultValue: 300 },
        } },
        defaultValue: DEFAULT_MARKERS,
    },
    delay: { type: ControlType.Number, title: "Markers after", defaultValue: 1500, min: 0, max: 5000, step: 50, unit: "ms" },
    onMarker1: { type: ControlType.EventHandler, title: "Marker 1" },
    onMarker2: { type: ControlType.EventHandler, title: "Marker 2" },
    onMarker3: { type: ControlType.EventHandler, title: "Marker 3" },
    onMarker4: { type: ControlType.EventHandler, title: "Marker 4" },
    onMarker5: { type: ControlType.EventHandler, title: "Marker 5" },
    onMarker6: { type: ControlType.EventHandler, title: "Marker 6" },
    onMarker7: { type: ControlType.EventHandler, title: "Marker 7" },
    onMarker8: { type: ControlType.EventHandler, title: "Marker 8" },
})
