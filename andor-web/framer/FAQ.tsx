// FAQ — questions on native <details>/<summary>: keyboard, screen readers and find-in-page work
// without script. Content comes from the property controls (or a CMS-bound list in Framer).
// andor-web framer bundle v1.0.0 (port of components/interactive/Accordion.tsx)

import * as React from "react"
import { addPropertyControls, ControlType } from "framer"
import { palette, font, motion, Theme } from "./ANDOR_Tokens.tsx"

const { useId } = React

/**
 * @framerSupportedLayoutWidth stretch
 * @framerSupportedLayoutHeight auto
 */
export default function FAQ({ items = [] as { q: string; a: string }[], theme = "light" as Theme, openFirst = false }) {
    const uid = useId().replace(/:/g, "")
    const p = palette(theme)
    const css = `.fq-${uid} summary { list-style: none; } .fq-${uid} summary::-webkit-details-marker { display: none; }
.fq-${uid} summary:focus-visible { outline: 2px solid ${p.focus}; outline-offset: 4px; border-radius: 6px; }
.fq-${uid} .fq-i::before, .fq-${uid} .fq-i::after { content: ""; position: absolute; left: 50%; top: 50%; width: 11px; height: 1.5px; background: currentColor; translate: -50% -50%; transition: rotate ${motion.d2}ms ${motion.easeOut}; }
.fq-${uid} .fq-i::after { rotate: 90deg; } .fq-${uid} details[open] .fq-i::after { rotate: 0deg; }`
    return (
        <div className={`fq-${uid}`} style={{ fontFamily: font.sans, color: p.text, width: "100%" }}>
            <style>{css}</style>
            {items.map((it, i) => (
                <details key={it.q + i} open={openFirst && i === 0} style={{ borderTop: `1px solid ${theme === "dark" ? "#49554B" : "#D8D9D1"}`, borderBottom: i === items.length - 1 ? `1px solid ${theme === "dark" ? "#49554B" : "#D8D9D1"}` : undefined }}>
                    <summary style={{ display: "flex", justifyContent: "space-between", alignItems: "center", gap: 20, minHeight: 64, cursor: "pointer", fontSize: 18, fontWeight: 520, letterSpacing: "-0.01em" }}>
                        <span>{it.q}</span>
                        <span className="fq-i" aria-hidden="true" style={{ position: "relative", flex: "none", width: 28, height: 28, borderRadius: "50%", border: `1px solid ${theme === "dark" ? "#7d897e" : "#858C82"}` }} />
                    </summary>
                    <div style={{ padding: "0 0 22px", color: p.text2, lineHeight: 1.55, maxWidth: "62ch", whiteSpace: "pre-line" }}>{it.a}</div>
                </details>
            ))}
        </div>
    )
}

addPropertyControls(FAQ, {
    items: {
        type: ControlType.Array, title: "Questions",
        control: { type: ControlType.Object, controls: {
            q: { type: ControlType.String, title: "Question", defaultValue: "Does it work with my DAW?" },
            a: { type: ControlType.String, title: "Answer", displayTextArea: true, defaultValue: "ANDOR OSP is being built as AU, VST3 and Standalone for macOS." },
        } },
    },
    theme: { type: ControlType.Enum, title: "Theme", options: ["light", "dark"], optionTitles: ["Light", "Dark"], displaySegmentedControl: true, defaultValue: "light" },
    openFirst: { type: ControlType.Boolean, title: "Open first", defaultValue: false },
})
