// ANDOR design tokens for Framer code components.
// Source of truth: andor-web/shared/tokens/tokens.css. Keep the two in step (framer:check verifies).
// andor-web framer bundle v1.0.0 · repo commit: see docs/FRAMER_BUILD_GUIDE.md §9

export const ANDOR_VERSION = "1.0.0"

export const color = {
    bg: "#F7F6F2",
    bg2: "#EDEBE5",
    bgElev: "#FCFBF8",
    text: "#242723",
    text2: "#5C635A",
    textMuted: "#656C63",
    textQuiet: "#878C83",
    accent: "#BD653D",
    accentStrong: "#A4532E",
    border: "#D8D9D1",
    borderStrong: "#858C82",
    darkBg: "#222924",
    darkElev: "#303A32",
    darkText: "#F4F1E9",
    darkText2: "#C4CEC1",
    darkMuted: "#9AA597",
    darkAccent: "#D08A63",
    darkBorder: "#49554B",
}

export type Theme = "light" | "dark"

/** The colours a component uses, for its theme. A component switches theme as a whole. */
export function palette(theme: Theme) {
    return theme === "dark"
        ? { bg: color.darkBg, surface: color.darkElev, text: color.darkText, text2: color.darkText2, muted: color.darkMuted, accent: color.darkAccent, line: "rgba(244,241,233,0.2)", chip: "rgba(244,241,233,0.08)", focus: color.darkAccent }
        : { bg: color.bg, surface: color.bgElev, text: color.text, text2: color.text2, muted: color.textMuted, accent: color.accentStrong, line: "rgba(36,39,35,0.16)", chip: "rgba(36,39,35,0.06)", focus: color.accentStrong }
}

export const font = {
    sans: '"Inter Tight", "Inter Tight Variable", ui-sans-serif, system-ui, -apple-system, "Segoe UI", sans-serif',
    mono: '"Geist Mono", ui-monospace, "SF Mono", Menlo, monospace',
}

export const motion = {
    easeOut: "cubic-bezier(0.22, 1, 0.36, 1)",
    easeInOut: "cubic-bezier(0.65, 0, 0.35, 1)",
    easeSoft: "cubic-bezier(0.33, 0, 0.15, 1)",
    d1: 160,
    d2: 280,
    d3: 520,
    d4: 900,
    d5: 1400,
}

/** Uppercase technical label (Geist Mono). */
export const monoLabel = { fontFamily: font.mono, fontSize: 12, letterSpacing: "0.04em", textTransform: "uppercase" as const, fontWeight: 500 }
