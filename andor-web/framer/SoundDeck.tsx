// SoundDeck — the Sounds section's deck of dark, tilted cards. The current card is flat and
// carries a working AudioComparison; neighbours lean back in perspective and can be brought
// forward. Phones get a plain swipeable row with every card visible (no 3D, nothing hidden).
// andor-web framer bundle v1.0.0 (port of prototype/osp/sections.tsx · Sounds)

import * as React from "react"
import { addPropertyControls, ControlType } from "framer"
import { ComparisonPlayer, Version } from "./AudioComparison.tsx"
import { color, font, motion } from "./ANDOR_Tokens.tsx"

const { useEffect, useId, useState } = React

interface Item { name: string; line: string; label1?: string; label2?: string; label3?: string; file1?: string; file2?: string; file3?: string; peaks?: string }

function useNarrow(px: number) {
    const [narrow, set] = useState(false)
    useEffect(() => {
        const m = window.matchMedia(`(max-width: ${px}px)`)
        const on = () => set(m.matches)
        on()
        m.addEventListener("change", on)
        return () => m.removeEventListener("change", on)
    }, [px])
    return narrow
}

/**
 * @framerSupportedLayoutWidth stretch
 * @framerSupportedLayoutHeight auto
 */
export default function SoundDeck({ items = [], start = 0, pendingText = "", rowBelow = 640 }: { items?: Item[]; start?: number; pendingText?: string; rowBelow?: number }) {
    const uid = useId().replace(/:/g, "")
    const n = items.length
    const [sel, setSel] = useState(Math.min(Math.max(0, start), Math.max(0, n - 1)))
    const row = useNarrow(rowBelow)
    if (!n) return <div style={{ padding: 24, color: color.darkMuted, fontFamily: font.sans }}>Add sound examples in the Items control.</div>

    const versions = (it: Item): Version[] => [
        { label: it.label1 || "Original", sources: it.file1 ? [it.file1] : [] },
        { label: it.label2 || "OSP", sources: it.file2 ? [it.file2] : [] },
        { label: it.label3 || "Reimagined", sources: it.file3 ? [it.file3] : [] },
    ]
    const peaks = (s?: string) => { try { const a = JSON.parse(s || "[]"); return Array.isArray(a) ? a.map(Number) : [] } catch { return [] } }
    const css = `.sd-${uid} button:focus-visible { outline: 2px solid ${color.darkAccent}; outline-offset: 3px; }
.sd-${uid} .sd-track::-webkit-scrollbar { display: none; }
@media (prefers-reduced-motion: reduce) { .sd-${uid} .sd-card { transition: none !important; } }`

    const cardW = "min(440px, 78vw)"
    return (
        <div className={`sd-${uid}`} role="group" aria-roledescription="carousel" aria-label="Sound examples" style={{ fontFamily: font.sans, color: color.darkText, width: "100%", perspective: row ? undefined : 1600 }}>
            <style>{css}</style>
            <div className="sd-track" style={row
                ? { display: "flex", gap: 12, overflowX: "auto", scrollSnapType: "x mandatory", padding: "4px 20px 8px", scrollbarWidth: "none" }
                : { position: "relative", height: "clamp(360px, 32vw, 440px)" }}>
                {items.map((it, i) => {
                    const off = i - sel
                    const abs = Math.abs(off)
                    const current = off === 0
                    const hidden = !row && abs > 1
                    const style: React.CSSProperties = row
                        ? { flex: "0 0 84vw", scrollSnapAlign: "center", minHeight: 340 }
                        : { position: "absolute", left: "50%", top: 0, width: cardW, height: "100%", marginLeft: `calc(${cardW} / -2)`,
                            transform: `translateX(${off * 96}%) rotateY(${off * -14}deg) scale(${1 - abs * 0.1})`, opacity: 1 - abs * 0.45, zIndex: 10 - abs,
                            transition: `transform ${motion.d4}ms ${motion.easeSoft}, opacity ${motion.d4}ms ${motion.easeSoft}`, pointerEvents: hidden ? "none" : undefined }
                    return (
                        <article key={it.name + i} className="sd-card" aria-roledescription="slide" aria-label={`${i + 1} of ${n}: ${it.name}`} aria-hidden={hidden || undefined} style={style}>
                            <div style={{ height: "100%", display: "flex", flexDirection: "column", borderRadius: 28, padding: "26px 26px 22px",
                                background: current ? "linear-gradient(180deg,#333d37,#202723)" : "linear-gradient(180deg,#2c3530,#1c2220)",
                                boxShadow: current ? "0 0 0 1px rgba(244,241,233,0.14), 0 50px 100px -40px rgba(0,0,0,0.8)" : "0 0 0 1px rgba(244,241,233,0.08), 0 40px 80px -40px rgba(0,0,0,0.7)",
                                filter: !row && !current ? "saturate(0.8) brightness(0.85)" : undefined }}>
                                <p style={{ margin: 0, fontFamily: font.mono, fontSize: 11, letterSpacing: "0.06em", textTransform: "uppercase", color: color.darkMuted }}>{String(i + 1).padStart(2, "0")} / {it.name}</p>
                                <h3 style={{ margin: "auto 0 0", fontSize: "clamp(32px, 2.4vw + 18px, 48px)", fontWeight: 500, letterSpacing: "-0.04em", lineHeight: 1 }}>{it.name}</h3>
                                <p style={{ margin: "10px 0 0", color: color.darkText2 }}>{it.line}</p>
                                {current ? (
                                    <div style={{ marginTop: 22, paddingTop: 18, borderTop: "1px solid rgba(244,241,233,0.12)" }}>
                                        <ComparisonPlayer title={it.name} source={`Source / ${it.name}`} versions={versions(it)} peaks={peaks(it.peaks)} theme="dark" pendingText={pendingText} />
                                    </div>
                                ) : (
                                    <button type="button" onClick={() => setSel(i)} tabIndex={row || abs === 1 ? 0 : -1}
                                        style={{ marginTop: 22, alignSelf: "flex-start", minHeight: 44, padding: "0 18px", borderRadius: 999, border: "1px solid rgba(244,241,233,0.3)", background: "transparent", color: color.darkText, cursor: "pointer", font: "inherit", fontSize: 14 }}>
                                        {row ? `Listen to ${it.name}` : `Show ${it.name}`}
                                    </button>
                                )}
                            </div>
                        </article>
                    )
                })}
            </div>
            {!row && (
                <div style={{ display: "flex", alignItems: "center", justifyContent: "center", gap: 22, marginTop: 28 }}>
                    <button type="button" aria-label="Previous example" onClick={() => setSel((sel - 1 + n) % n)} style={arrow}>←</button>
                    <span aria-live="polite" style={{ fontFamily: font.mono, fontSize: 12, color: color.darkMuted, minWidth: "4ch", textAlign: "center" }}>{sel + 1} / {n}</span>
                    <button type="button" aria-label="Next example" onClick={() => setSel((sel + 1) % n)} style={arrow}>→</button>
                </div>
            )}
        </div>
    )
}

const arrow: React.CSSProperties = { width: 48, height: 48, borderRadius: "50%", border: "1px solid rgba(244,241,233,0.3)", background: "transparent", color: color.darkText, cursor: "pointer", fontSize: 18 }
const audioTypes = ["mp3", "m4a", "aac", "ogg", "opus", "webm", "wav", "flac"]

addPropertyControls(SoundDeck, {
    items: {
        type: ControlType.Array, title: "Items",
        control: {
            type: ControlType.Object,
            controls: {
                name: { type: ControlType.String, title: "Name", defaultValue: "Nyckelharpa" },
                line: { type: ControlType.String, title: "Line", defaultValue: "A single bowed note." },
                label1: { type: ControlType.String, title: "Version 1", defaultValue: "Original" },
                file1: { type: ControlType.File, title: "V1 file", allowedFileTypes: audioTypes },
                label2: { type: ControlType.String, title: "Version 2", defaultValue: "OSP" },
                file2: { type: ControlType.File, title: "V2 file", allowedFileTypes: audioTypes },
                label3: { type: ControlType.String, title: "Version 3", defaultValue: "Reimagined" },
                file3: { type: ControlType.File, title: "V3 file", allowedFileTypes: audioTypes },
                peaks: { type: ControlType.String, title: "Peaks (JSON)", displayTextArea: true },
            },
        },
        defaultValue: [
            { name: "Cello", line: "One long, low tone." },
            { name: "Voice", line: "A sung vowel." },
            { name: "Nyckelharpa", line: "A single bowed note." },
            { name: "Analog synth", line: "An old synth, one key." },
            { name: "Plucked string", line: "A short pluck, held for ever." },
        ],
    },
    start: { type: ControlType.Number, title: "Start at", min: 0, max: 20, step: 1, displayStepper: true, defaultValue: 2, description: "Index of the card shown first (0 = first)." },
    rowBelow: { type: ControlType.Number, title: "Row below", min: 320, max: 1024, step: 1, unit: "px", defaultValue: 640, description: "Below this width the deck becomes a swipeable row." },
    pendingText: { type: ControlType.String, title: "Pending text", defaultValue: "" },
})
