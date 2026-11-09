// DeepDive — the content of a feature deep-dive, placed inside a native Framer Overlay. Holds
// every feature (so previous/next walk them in order) and opens at `Start at`. Tabs (ARIA tablist,
// arrow keys) switch between up to five genuine captures per feature. Escape and the close button
// call `On Close` (wire it to the overlay's Close). Focus moves into the sheet and stays there.
// andor-web framer bundle v1.0.0 (port of components/interactive/FeatureModal.tsx)

import * as React from "react"
import { addPropertyControls, ControlType } from "framer"
import { color, font, motion } from "./ANDOR_Tokens.tsx"

const { useEffect, useId, useRef, useState } = React

interface Img { src: string; srcSet?: string; alt?: string }
interface Feature {
    id: string; n: number; kicker: string; title: string; body: string; facts?: string; media?: "popover" | "panel"
    [key: string]: any // v1Label, v1Image, v1Text … v5*
}
const viewsOf = (f: Feature) =>
    [1, 2, 3, 4, 5].map((i) => ({ label: f[`v${i}Label`] as string, image: f[`v${i}Image`] as Img | undefined, text: f[`v${i}Text`] as string | undefined })).filter((v) => v.label || v.image?.src)

function useWide(px: number) {
    const [wide, set] = useState(true)
    useEffect(() => {
        const m = window.matchMedia(`(min-width: ${px}px)`)
        const on = () => set(m.matches)
        on()
        m.addEventListener("change", on)
        return () => m.removeEventListener("change", on)
    }, [px])
    return wide
}

/**
 * @framerSupportedLayoutWidth any-prefer-fixed
 * @framerSupportedLayoutHeight auto
 * @framerIntrinsicWidth 1040
 */
export default function DeepDive(props: any) {
    const { items = [] as Feature[], startAt = "", onClose } = props
    const uid = useId().replace(/:/g, "")
    const startIndex = Math.max(0, items.findIndex((f: Feature) => f.id === startAt))
    const [index, setIndex] = useState(startIndex)
    const [view, setView] = useState(0)
    const sheet = useRef<HTMLDivElement>(null)
    const wide = useWide(860)
    useEffect(() => { setIndex(startIndex) }, [startIndex])
    useEffect(() => { setView(0) }, [index])
    useEffect(() => { sheet.current?.querySelector<HTMLElement>("[data-close]")?.focus() }, [])
    if (!items.length) return <div style={{ padding: 24, fontFamily: font.sans }}>Add features in the Items control.</div>

    const f: Feature = items[index]
    const views = viewsOf(f)
    const v: { label?: string; text?: string } = views[Math.min(view, views.length - 1)] ?? { label: "" }
    const popover = (f.media ?? "popover") === "popover"
    const go = (d: number) => setIndex((index + d + items.length) % items.length)
    const prev = items[(index - 1 + items.length) % items.length]
    const next = items[(index + 1) % items.length]
    const facts = (f.facts || "").split("·").map((s: string) => s.trim()).filter(Boolean)

    // Keep keyboard focus inside the sheet while it is open; Escape closes.
    const onKeyDown = (e: React.KeyboardEvent) => {
        if (e.key === "Escape") { e.stopPropagation(); onClose?.() }
        if (e.key !== "Tab") return
        const nodes = sheet.current!.querySelectorAll<HTMLElement>('button:not([disabled]), [href], [tabindex]:not([tabindex="-1"])')
        if (!nodes.length) return
        const first = nodes[0], last = nodes[nodes.length - 1]
        if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus() }
        else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus() }
    }
    const onTabsKey = (e: React.KeyboardEvent) => {
        const d = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0
        if (!d) return
        e.preventDefault()
        const n = (view + d + views.length) % views.length
        setView(n)
        ;((e.currentTarget as HTMLElement).children[n] as HTMLElement | undefined)?.focus()
    }
    const css = `.dd-${uid} button:focus-visible { outline: 2px solid ${color.accentStrong}; outline-offset: 3px; }
.dd-${uid} .dd-step:hover, .dd-${uid} [role=tab][aria-selected=false]:hover { color: ${color.text}; }
@keyframes dd-in-${uid} { from { opacity: 0; transform: translateY(14px) scale(0.98); } }
@media (prefers-reduced-motion: reduce) { .dd-${uid} { animation: none !important; } .dd-${uid} .dd-view { transition: none !important; } }`

    const mono = { fontFamily: font.mono, fontSize: 12, letterSpacing: "0.04em", textTransform: "uppercase" as const }
    const media = (
        <div id={`${uid}-panel`} role={views.length > 1 ? "tabpanel" : undefined} aria-labelledby={views.length > 1 ? `${uid}-tab-${view}` : undefined}
            style={{ display: "grid", placeItems: "center", borderRadius: 22, background: `radial-gradient(70% 80% at 50% 30%, #fdfcf9, ${color.bg2})`, padding: "clamp(18px, 3vw, 40px)", minHeight: popover && wide ? 360 : undefined }}>
            <div style={{ display: "grid", width: "100%", maxWidth: popover ? 420 : undefined }}>
                {views.map((x, i) => x.image?.src ? (
                    <img key={i} className="dd-view" src={x.image.src} srcSet={x.image.srcSet} sizes={popover ? "(max-width: 640px) 86vw, 420px" : "(max-width: 640px) 92vw, 960px"}
                        alt={x.image.alt ?? ""} aria-hidden={i !== view} draggable={false}
                        style={{ gridArea: "1 / 1", width: "100%", height: "auto", borderRadius: popover ? 14 : 10, opacity: i === view ? 1 : 0, visibility: i === view ? "visible" : "hidden",
                            boxShadow: "0 0 0 1px rgba(36,39,35,0.08), 0 24px 50px -24px rgba(46,38,28,0.45)", transition: `opacity ${motion.d3}ms ${motion.easeInOut}${i === view ? "" : `, visibility 0s linear ${motion.d3}ms`}` }} />
                ) : (
                    <div key={i} role="img" aria-label={`Image missing: ${x.label}`} style={{ gridArea: "1 / 1", display: i === view ? "grid" : "none", aspectRatio: "4 / 3", placeItems: "center", border: `1px dashed ${color.borderStrong}`, borderRadius: 12, color: color.text2, fontSize: 13 }}>Image missing: {x.label}</div>
                ))}
            </div>
        </div>
    )
    const copy = (
        <div style={{ display: "grid", gap: 12, alignContent: "start", maxWidth: "68ch" }}>
            <p style={{ margin: 0, fontSize: 19, lineHeight: 1.45, letterSpacing: "-0.01em" }}>{v.text || f.body}</p>
            {v.text && <p style={{ margin: 0, fontSize: 14, color: color.text2, lineHeight: 1.5 }}>{f.body}</p>}
            {facts.length > 0 && (
                <ul style={{ listStyle: "none", margin: "4px 0 0", padding: 0, display: "flex", flexWrap: "wrap", gap: 8 }}>
                    {facts.map((x: string) => <li key={x} style={{ ...mono, fontSize: 11, padding: "7px 12px", borderRadius: 999, background: color.bg2, color: color.text2 }}>{x}</li>)}
                </ul>
            )}
        </div>
    )
    return (
        <div ref={sheet} className={`dd-${uid}`} role="dialog" aria-modal="true" aria-labelledby={`${uid}-title`} onKeyDown={onKeyDown}
            style={{ boxSizing: "border-box", width: "100%", maxHeight: "calc(100svh - 48px)", overflow: "auto", overscrollBehavior: "contain", fontFamily: font.sans, color: color.text,
                background: color.bgElev, borderRadius: wide ? 30 : "26px 26px 0 0", padding: wide ? "clamp(22px, 3vw, 40px)" : "20px 18px 18px", display: "grid", gap: 22,
                boxShadow: "0 0 0 1px rgba(36,39,35,0.06), 0 40px 120px -30px rgba(20,24,21,0.55)", animation: `dd-in-${uid} ${motion.d3}ms ${motion.easeOut}` }}>
            <style>{css}</style>
            <header style={{ display: "flex", alignItems: "flex-start", justifyContent: "space-between", gap: 20 }}>
                <div>
                    <p style={{ ...mono, margin: "0 0 10px", color: color.textMuted, display: "flex", alignItems: "center", gap: 10 }}>
                        <span aria-hidden="true" style={{ display: "inline-grid", placeItems: "center", minWidth: 20, height: 20, borderRadius: 10, background: color.text, color: color.bg, fontSize: 11 }}>{f.n}</span>{f.kicker}
                    </p>
                    <h2 id={`${uid}-title`} style={{ margin: 0, fontSize: "clamp(28px, 1.6vw + 20px, 44px)", fontWeight: 500, letterSpacing: "-0.035em", lineHeight: 1.04 }}>{f.title}</h2>
                </div>
                <button type="button" data-close onClick={() => onClose?.()} aria-label="Close"
                    style={{ flex: "none", width: 48, height: 48, borderRadius: "50%", border: 0, background: color.bg2, color: color.text, display: "grid", placeItems: "center", cursor: "pointer" }}>
                    <svg viewBox="0 0 18 18" width="18" height="18" aria-hidden="true"><path d="M4 4l10 10M14 4L4 14" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" /></svg>
                </button>
            </header>
            {views.length > 1 && (
                <div role="tablist" aria-label={`${f.title} views`} onKeyDown={onTabsKey} style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
                    {views.map((x, i) => (
                        <button key={x.label + i} type="button" role="tab" id={`${uid}-tab-${i}`} aria-selected={i === view} aria-controls={`${uid}-panel`} tabIndex={i === view ? 0 : -1} onClick={() => setView(i)}
                            style={{ ...mono, minHeight: 40, padding: "0 16px", border: 0, borderRadius: 999, cursor: "pointer", background: i === view ? color.text : color.bg2, color: i === view ? color.bg : color.text2,
                                transition: `background-color ${motion.d2}ms ${motion.easeInOut}, color ${motion.d2}ms ${motion.easeInOut}` }}>{x.label}</button>
                    ))}
                </div>
            )}
            <div style={popover && wide ? { display: "grid", gridTemplateColumns: "minmax(0, 1.15fr) minmax(0, 1fr)", alignItems: "center", gap: "clamp(24px, 3vw, 44px)" } : { display: "grid", gap: 22 }}>
                {media}{copy}
            </div>
            <footer style={{ display: "flex", alignItems: "center", justifyContent: "space-between", gap: 12, paddingTop: 18, borderTop: `1px solid ${color.border}` }}>
                <button type="button" className="dd-step" onClick={() => go(-1)} style={step}><span aria-hidden="true">←</span> {prev.title}</button>
                <span style={{ ...mono, color: color.textMuted }}>{index + 1} / {items.length}</span>
                <button type="button" className="dd-step" onClick={() => go(1)} style={step}>{next.title} <span aria-hidden="true">→</span></button>
            </footer>
        </div>
    )
}

const step: React.CSSProperties = { minHeight: 44, border: 0, background: "none", padding: 0, cursor: "pointer", fontWeight: 520, color: color.text2, font: "inherit" }

const viewControls: Record<string, any> = {}
for (const i of [1, 2, 3, 4, 5]) {
    viewControls[`v${i}Label`] = { type: ControlType.String, title: `View ${i}` }
    viewControls[`v${i}Image`] = { type: ControlType.ResponsiveImage, title: `View ${i} image` }
    viewControls[`v${i}Text`] = { type: ControlType.String, title: `View ${i} text`, displayTextArea: true }
}

addPropertyControls(DeepDive, {
    startAt: { type: ControlType.String, title: "Start at (id)", defaultValue: "life", description: "The feature this overlay opens on, e.g. life, reimagined, synth." },
    onClose: { type: ControlType.EventHandler, title: "On Close" },
    items: {
        type: ControlType.Array, title: "Features",
        control: {
            type: ControlType.Object,
            controls: {
                id: { type: ControlType.String, title: "Id", defaultValue: "life" },
                n: { type: ControlType.Number, title: "Number", min: 1, max: 20, step: 1, displayStepper: true, defaultValue: 1 },
                kicker: { type: ControlType.String, title: "Kicker" },
                title: { type: ControlType.String, title: "Title" },
                body: { type: ControlType.String, title: "Body", displayTextArea: true },
                facts: { type: ControlType.String, title: "Facts", description: "Separate with ·  e.g. Takes 2–16 · Pitch · Tone" },
                media: { type: ControlType.Enum, title: "Media", options: ["popover", "panel"], optionTitles: ["Popover", "Panel"], displaySegmentedControl: true, defaultValue: "popover" },
                ...viewControls,
            },
        },
    },
})
