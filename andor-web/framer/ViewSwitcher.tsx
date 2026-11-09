// ViewSwitcher — choose between views of the same thing (REIMAGINED modes, SYNTH states, layer
// counts). An ARIA tablist with arrow keys, Home and End; views crossfade in one grid cell so a
// switch never moves the layout. Layouts: "side" (selector + intro left, picture right) and
// "stacked" (pills above the picture). Below the breakpoint, "side" stacks and a list becomes a
// scrollable row of pills.
// andor-web framer bundle v1.0.0 (port of components/interactive/Segmented.tsx + prototype views)

import * as React from "react"
import { addPropertyControls, ControlType } from "framer"
import { palette, font, motion, Theme } from "./ANDOR_Tokens.tsx"

const { useEffect, useId, useRef, useState } = React

interface Img { src: string; srcSet?: string; alt?: string }
interface Item { label: string; hint?: string; image?: Img; text?: string }

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
export default function ViewSwitcher(props: any) {
    const { items = [] as Item[], initial = 0, label = "View", layout = "side", selector = "list", theme = "light" as Theme, plate = true,
        textPlacement = "stage", pending = "", imageMaxWidth = 520, stackBelow = 900, intro, caption = "" } = props
    const uid = useId().replace(/:/g, "")
    const p = palette(theme)
    const narrow = useNarrow(stackBelow)
    const [index, setIndex] = useState(Math.min(initial, Math.max(0, items.length - 1)))
    const tabs = useRef<HTMLDivElement>(null)
    if (!items.length) return <div style={{ padding: 24, fontFamily: font.sans, color: p.muted }}>Add views in the Items control.</div>
    const current = items[index]
    const side = layout === "side" && !narrow
    const asList = selector === "list" && !narrow

    const move = (i: number) => {
        const n = (i + items.length) % items.length
        setIndex(n)
        ;(tabs.current?.children[n] as HTMLElement | undefined)?.focus()
    }
    const onKey = (e: React.KeyboardEvent) => {
        const k: Record<string, number> = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 }
        if (e.key in k) { e.preventDefault(); move(index + k[e.key]) }
        if (e.key === "Home") { e.preventDefault(); move(0) }
        if (e.key === "End") { e.preventDefault(); move(items.length - 1) }
    }
    const css = `.vs-${uid} [role=tab]:focus-visible { outline: 2px solid ${p.focus}; outline-offset: 3px; }
.vs-${uid} [role=tab][aria-selected=false]:hover { color: ${p.text}; }
.vs-${uid} .vs-row::-webkit-scrollbar { display: none; }
@media (prefers-reduced-motion: reduce) { .vs-${uid} .vs-view { transition: none !important; } }`

    const selectorEl = (
        <div ref={tabs} role="tablist" aria-label={label} onKeyDown={onKey} className={asList ? "" : "vs-row"}
            style={asList
                ? { display: "flex", flexDirection: "column", gap: 2, marginTop: side ? 32 : 0 }
                : { display: "flex", gap: 6, flexWrap: selector === "list" ? "nowrap" : "wrap", overflowX: selector === "list" ? "auto" : undefined, scrollbarWidth: "none" as any, marginTop: side ? 28 : 0 }}>
            {items.map((it: Item, i: number) => {
                const on = i === index
                return (
                    <button key={it.label + i} type="button" role="tab" id={`${uid}-tab-${i}`} aria-selected={on} aria-controls={`${uid}-panel`} tabIndex={on ? 0 : -1} onClick={() => setIndex(i)}
                        style={asList
                            ? { display: "grid", gap: 4, textAlign: "left", padding: "16px 20px", border: 0, borderRadius: 18, cursor: "pointer", font: "inherit",
                                background: on ? p.surface : "transparent", boxShadow: on ? "0 0 0 1px rgba(36,39,35,0.06), 0 12px 30px -20px rgba(46,38,28,0.35)" : "none", color: on ? p.text : p.text2,
                                transition: `background-color ${motion.d2}ms ${motion.easeInOut}` }
                            : { flex: "none", minHeight: 42, padding: "0 18px", border: 0, borderRadius: 999, cursor: "pointer", fontFamily: font.mono, fontSize: 12, letterSpacing: "0.04em", textTransform: "uppercase",
                                background: on ? p.text : p.chip, color: on ? p.bg : p.text2, transition: `background-color ${motion.d2}ms ${motion.easeInOut}, color ${motion.d2}ms ${motion.easeInOut}` }}>
                        {asList ? (
                            <>
                                <span style={{ fontSize: 22, fontWeight: 500, letterSpacing: "-0.02em" }}>{it.label}</span>
                                {it.hint && <span style={{ fontSize: 14, color: p.muted }}>{it.hint}</span>}
                            </>
                        ) : it.label}
                    </button>
                )
            })}
        </div>
    )
    const textEl = (current.text || pending) && (
        <div>
            {current.text && <p style={{ margin: "18px 0 0", fontSize: 19, lineHeight: 1.45, color: textPlacement === "stage" ? p.text : p.text2, maxWidth: "46ch", minHeight: "2.9em" }}>{current.text}</p>}
            {pending && <p style={{ margin: "14px 0 0", fontFamily: font.mono, fontSize: 11, letterSpacing: "0.06em", textTransform: "uppercase", color: p.muted }}>○ {pending}</p>}
        </div>
    )
    const stage = (
        <div id={`${uid}-panel`} role="tabpanel" aria-labelledby={`${uid}-tab-${index}`}>
            <div style={plate
                ? { borderRadius: 28, background: theme === "dark" ? "rgba(244,241,233,0.06)" : "radial-gradient(80% 90% at 50% 20%, #fbfaf7, #efede7)", padding: "clamp(14px, 2.4vw, 32px)", display: "grid", placeItems: "center", minHeight: side ? "clamp(320px, 34vw, 480px)" : undefined }
                : { display: "grid" }}>
                <div style={{ display: "grid", width: "100%", maxWidth: plate ? imageMaxWidth : undefined }}>
                    {items.map((it: Item, i: number) => it.image?.src ? (
                        <img key={i} className="vs-view" src={it.image.src} srcSet={it.image.srcSet} sizes={side ? "(max-width: 900px) 90vw, 56vw" : "90vw"} alt={it.image.alt ?? ""} loading="lazy" decoding="async"
                            aria-hidden={i !== index} draggable={false}
                            style={{ gridArea: "1 / 1", width: "100%", height: "auto", borderRadius: plate ? 12 : 14, opacity: i === index ? 1 : 0, visibility: i === index ? "visible" : "hidden",
                                boxShadow: theme === "dark" ? "0 0 0 1px rgba(244,241,233,0.12), 0 40px 90px -40px rgba(0,0,0,0.8)" : "0 0 0 1px rgba(36,39,35,0.07), 0 18px 40px -22px rgba(46,38,28,0.45)",
                                transition: `opacity ${motion.d3}ms ${motion.easeInOut}${i === index ? "" : `, visibility 0s linear ${motion.d3}ms`}` }} />
                    ) : (
                        <div key={i} role="img" aria-label={`Image missing: ${it.label}`} style={{ gridArea: "1 / 1", aspectRatio: "4 / 3", border: `1px dashed ${p.muted}`, borderRadius: 12, display: i === index ? "grid" : "none", placeItems: "center", color: p.muted, fontSize: 13 }}>Image missing: {it.label}</div>
                    ))}
                </div>
            </div>
            {caption && <p style={{ margin: "14px 0 0", fontFamily: font.mono, fontSize: 11, letterSpacing: "0.06em", textTransform: "uppercase", color: p.muted }}>{caption}</p>}
            {textPlacement === "stage" && textEl}
        </div>
    )
    return (
        <div className={`vs-${uid}`} style={{ fontFamily: font.sans, color: p.text, width: "100%" }}>
            <style>{css}</style>
            {side ? (
                <div style={{ display: "grid", gridTemplateColumns: "minmax(0, 5fr) minmax(0, 7fr)", gap: "clamp(32px, 5vw, 80px)", alignItems: "center" }}>
                    <div>{intro}{selectorEl}{textPlacement === "selector" && textEl}</div>
                    {stage}
                </div>
            ) : (
                <div style={{ display: "grid", gap: 24 }}>
                    {intro}
                    <div>{selectorEl}{textPlacement === "selector" && textEl}</div>
                    {stage}
                </div>
            )}
        </div>
    )
}

addPropertyControls(ViewSwitcher, {
    intro: { type: ControlType.ComponentInstance, title: "Intro (slot)" },
    label: { type: ControlType.String, title: "A11y label", defaultValue: "View", description: "Read by screen readers for the tab list, e.g. “REIMAGINED mode”." },
    items: {
        type: ControlType.Array, title: "Views",
        control: {
            type: ControlType.Object,
            controls: {
                label: { type: ControlType.String, title: "Label", defaultValue: "Mosaic" },
                hint: { type: ControlType.String, title: "Hint (list)" },
                image: { type: ControlType.ResponsiveImage, title: "Image" },
                text: { type: ControlType.String, title: "Text", displayTextArea: true },
            },
        },
    },
    initial: { type: ControlType.Number, title: "Start at", min: 0, max: 10, step: 1, displayStepper: true, defaultValue: 0 },
    layout: { type: ControlType.Enum, title: "Layout", options: ["side", "stacked"], optionTitles: ["Side", "Stacked"], displaySegmentedControl: true, defaultValue: "side" },
    selector: { type: ControlType.Enum, title: "Selector", options: ["list", "pills"], optionTitles: ["List", "Pills"], displaySegmentedControl: true, defaultValue: "list" },
    textPlacement: { type: ControlType.Enum, title: "Text under", options: ["stage", "selector"], optionTitles: ["Picture", "Selector"], displaySegmentedControl: true, defaultValue: "stage" },
    theme: { type: ControlType.Enum, title: "Theme", options: ["light", "dark"], optionTitles: ["Light", "Dark"], displaySegmentedControl: true, defaultValue: "light" },
    plate: { type: ControlType.Boolean, title: "Plate", defaultValue: true, description: "A soft plate behind small pictures (popovers)." },
    imageMaxWidth: { type: ControlType.Number, title: "Max width", min: 200, max: 1600, step: 10, unit: "px", defaultValue: 520, hidden: (p: any) => !p.plate },
    caption: { type: ControlType.String, title: "Caption" },
    pending: { type: ControlType.String, title: "Pending", defaultValue: "", description: "E.g. “Sound example in preparation”." },
    stackBelow: { type: ControlType.Number, title: "Stack below", min: 320, max: 1440, step: 1, unit: "px", defaultValue: 900 },
})
