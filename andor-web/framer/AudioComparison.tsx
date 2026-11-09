// AudioComparison — synchronised listening between aligned versions of one recording
// (e.g. ORIGINAL / OSP / REIMAGINED). Every version is decoded and started together on one clock;
// switching only crossfades gains, so the playhead never jumps and versions never overlap audibly.
// Audio loads on the first press of play, never on page load. No autoplay.
// andor-web framer bundle v1.0.0 (port of components/audio/AudioComparison.tsx)

import * as React from "react"
import { addPropertyControls, ControlType } from "framer"
import { AudioController } from "./ANDOR_AudioController.tsx"
import { palette, font, motion, Theme } from "./ANDOR_Tokens.tsx"

const { useCallback, useEffect, useId, useMemo, useRef, useState } = React

type Status = "pending" | "idle" | "loading" | "ready" | "playing" | "error"
export interface Version { label: string; sources: string[] }
export interface PlayerProps {
    title: string
    source: string
    versions: Version[]
    peaks?: number[]
    theme?: Theme
    layout?: "compact" | "stack"
    pendingText?: string
}

const FADE = 0.03
const fmt = (s: number) => {
    if (!isFinite(s) || s < 0) s = 0
    return `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`
}

async function decodeFirst(ctx: AudioContext, sources: string[], signal: AbortSignal) {
    let last: unknown = null
    for (const src of sources) {
        try {
            const res = await fetch(src, { signal })
            if (!res.ok) throw new Error(`${res.status} ${src}`)
            return await ctx.decodeAudioData(await res.arrayBuffer())
        } catch (e) {
            if ((e as Error).name === "AbortError") throw e
            last = e // unsupported format or missing file: try the next encoding
        }
    }
    throw last ?? new Error("No playable format.")
}

/** The player itself (also used by SoundDeck). */
export function ComparisonPlayer({ title, source, versions, peaks = [], theme = "light", layout = "compact", pendingText }: PlayerProps) {
    const uid = useId().replace(/:/g, "")
    const p = palette(theme)
    const playable = versions.length > 0 && versions.every((v) => v.sources.length > 0)
    const [status, setStatus] = useState<Status>(playable ? "idle" : "pending")
    const [variant, setVariant] = useState(0)
    const [position, setPosition] = useState(0)
    const [duration, setDuration] = useState(0)
    const [error, setError] = useState<string | null>(null)
    const buffers = useRef<AudioBuffer[] | null>(null)
    const graph = useRef<{ sources: AudioBufferSourceNode[]; gains: GainNode[]; startedAt: number; offset: number } | null>(null)
    const abort = useRef<AbortController | null>(null)
    const raf = useRef(0)
    const offsetRef = useRef(0)
    const variantRef = useRef(0)
    variantRef.current = variant

    useEffect(() => { setStatus(playable ? "idle" : "pending"); buffers.current = null }, [playable, versions.map((v) => v.sources.join()).join("|")])

    const stopGraph = useCallback(() => {
        const g = graph.current
        if (!g) return
        g.sources.forEach((s) => { s.onended = null; try { s.stop() } catch {} s.disconnect() })
        g.gains.forEach((n) => n.disconnect())
        graph.current = null
        cancelAnimationFrame(raf.current)
    }, [])
    const currentTime = useCallback(() => {
        const g = graph.current
        if (!g) return offsetRef.current
        return Math.min(g.offset + (g.sources[0].context.currentTime - g.startedAt), duration || Infinity)
    }, [duration])
    const pause = useCallback(() => {
        offsetRef.current = currentTime()
        stopGraph()
        AudioController.release(uid)
        setStatus((s) => (s === "playing" ? "ready" : s))
    }, [currentTime, stopGraph, uid])

    // Register once, with a stable pointer to the latest pause: re-registering on every change of
    // `pause` would clear the controller's active player mid-playback and break exclusivity.
    const pauseRef = useRef(pause)
    pauseRef.current = pause
    useEffect(() => AudioController.register(uid, () => pauseRef.current()), [uid])
    useEffect(() => () => { abort.current?.abort(); stopGraph(); AudioController.release(uid) }, [stopGraph, uid])

    const tick = useCallback(() => { setPosition(currentTime()); raf.current = requestAnimationFrame(tick) }, [currentTime])
    const startAt = useCallback(async (offset: number) => {
        const ctx = await AudioController.context()
        stopGraph()
        const t0 = ctx.currentTime + 0.04
        const sources: AudioBufferSourceNode[] = []
        const gains: GainNode[] = []
        buffers.current!.forEach((b, i) => {
            const s = ctx.createBufferSource()
            s.buffer = b
            const g = ctx.createGain()
            g.gain.value = i === variantRef.current ? 1 : 0
            s.connect(g).connect(ctx.destination)
            s.start(t0, Math.min(offset, b.duration - 0.01))
            sources.push(s)
            gains.push(g)
        })
        sources[0].onended = () => { offsetRef.current = 0; stopGraph(); setPosition(0); setStatus("ready"); AudioController.release(uid) }
        graph.current = { sources, gains, startedAt: t0, offset }
        AudioController.claim(uid)
        setStatus("playing")
        raf.current = requestAnimationFrame(tick)
    }, [stopGraph, tick, uid])
    const load = useCallback(async () => {
        setStatus("loading")
        setError(null)
        abort.current = new AbortController()
        try {
            const ctx = await AudioController.context()
            const bufs = await Promise.all(versions.map((v) => decodeFirst(ctx, v.sources, abort.current!.signal)))
            buffers.current = bufs
            setDuration(Math.min(...bufs.map((b) => b.duration)))
            return true
        } catch (e) {
            if ((e as Error).name === "AbortError") return false
            setError("This example could not be loaded.")
            setStatus("error")
            return false
        }
    }, [versions])
    const toggle = useCallback(async () => {
        if (status === "pending" || status === "loading") return
        if (status === "playing") return pause()
        if (!buffers.current && !(await load())) return
        await startAt(offsetRef.current)
    }, [status, pause, load, startAt])
    const choose = useCallback((i: number) => {
        setVariant(i)
        const g = graph.current
        if (!g) return
        const now = g.sources[0].context.currentTime
        g.gains.forEach((n, k) => { n.gain.cancelScheduledValues(now); n.gain.setValueAtTime(n.gain.value, now); n.gain.linearRampToValueAtTime(k === i ? 1 : 0, now + FADE) })
    }, [])
    const seek = useCallback((t: number) => {
        const c = Math.max(0, Math.min(t, duration || 0))
        offsetRef.current = c
        setPosition(c)
        if (graph.current) startAt(c)
    }, [duration, startAt])

    const track = useRef<HTMLDivElement>(null)
    const group = useRef<HTMLDivElement>(null)
    const pending = status === "pending"
    const progress = duration ? position / duration : 0
    const onTrackKey = (e: React.KeyboardEvent) => {
        const step: Record<string, number> = { ArrowRight: 5, ArrowUp: 5, ArrowLeft: -5, ArrowDown: -5, PageUp: 15, PageDown: -15 }
        if (e.key in step) { e.preventDefault(); seek(position + step[e.key]) }
        if (e.key === "Home") { e.preventDefault(); seek(0) }
        if (e.key === "End") { e.preventDefault(); seek(duration - 0.5) }
    }
    const onVariantKey = (e: React.KeyboardEvent) => {
        const n = versions.length
        const d = e.key === "ArrowRight" || e.key === "ArrowDown" ? 1 : e.key === "ArrowLeft" || e.key === "ArrowUp" ? -1 : 0
        if (!d) return
        e.preventDefault()
        const i = (variant + d + n) % n
        choose(i)
        ;(group.current?.querySelectorAll("button")[i] as HTMLButtonElement | undefined)?.focus()
    }
    const css = useMemo(() => `
.ac-${uid} button:focus-visible, .ac-${uid} [role=slider]:focus-visible { outline: 2px solid ${p.focus}; outline-offset: 3px; }
.ac-${uid} .ac-play:not(:disabled):hover { transform: scale(1.04); }
.ac-${uid} .ac-v[aria-checked=false]:not(:disabled):hover { color: ${p.text}; }
@keyframes ac-spin-${uid} { to { transform: rotate(360deg); } }`, [uid, p.focus, p.text])

    const label = { fontFamily: font.mono, fontSize: 11, letterSpacing: "0.06em", textTransform: "uppercase" as const, fontWeight: 500, color: p.muted }
    const stack = layout === "stack"
    return (
        <section className={`ac-${uid}`} aria-label={`Listen: ${title}`} data-status={status} style={{ fontFamily: font.sans, color: p.text, width: "100%" }}>
            <style>{css}</style>
            <div style={{ display: "flex", alignItems: stack ? "flex-start" : "center", gap: 14 }}>
                <button type="button" className="ac-play" onClick={toggle} disabled={pending || status === "loading"}
                    aria-label={status === "playing" ? `Pause ${title}` : `Play ${title}, ${versions[variant]?.label ?? ""}`}
                    aria-describedby={pending ? `${uid}-pending` : undefined}
                    style={{ flex: "none", width: 48, height: 48, borderRadius: "50%", border: 0, cursor: pending ? "not-allowed" : "pointer", display: "grid", placeItems: "center", background: p.text, color: p.bg, opacity: pending ? 0.38 : 1, transition: `transform ${motion.d1}ms ${motion.easeOut}` }}>
                    {status === "loading" ? (
                        <span aria-hidden="true" style={{ width: 16, height: 16, borderRadius: "50%", border: "2px solid currentColor", borderRightColor: "transparent", animation: `ac-spin-${uid} .8s linear infinite` }} />
                    ) : status === "playing" ? (
                        <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true" fill="currentColor"><rect x="3.5" y="2.5" width="3" height="11" rx="1" /><rect x="9.5" y="2.5" width="3" height="11" rx="1" /></svg>
                    ) : (
                        <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true" fill="currentColor"><path d="M4.5 2.6v10.8c0 .5.5.8.9.5l8.1-5.4c.4-.3.4-.8 0-1.1L5.4 2.1c-.4-.3-.9 0-.9.5z" /></svg>
                    )}
                </button>
                <div style={{ flex: 1, minWidth: 0 }}>
                    <div style={{ display: "flex", justifyContent: "space-between", gap: 12, marginBottom: 6, ...label }}>
                        <span>{source}</span>
                        <span style={{ fontVariantNumeric: "tabular-nums" }} aria-hidden={pending}>{pending ? "" : `${fmt(position)} / ${fmt(duration)}`}</span>
                    </div>
                    <div ref={track} role="slider" tabIndex={pending ? -1 : 0} aria-label="Playback position" aria-valuemin={0} aria-valuemax={Math.round(duration)}
                        aria-valuenow={Math.round(position)} aria-valuetext={`${fmt(position)} of ${fmt(duration)}`} aria-disabled={pending || undefined}
                        onKeyDown={onTrackKey}
                        onPointerDown={(e) => { if (pending) return; const r = track.current!.getBoundingClientRect(); seek(((e.clientX - r.left) / r.width) * duration) }}
                        style={{ position: "relative", height: 28, display: "flex", alignItems: "center", cursor: pending ? "default" : "pointer", touchAction: "none", borderRadius: 4 }}>
                        {peaks.length ? (
                            <svg viewBox={`0 0 ${peaks.length} 100`} preserveAspectRatio="none" aria-hidden="true" style={{ width: "100%", height: "100%" }}>
                                {peaks.map((v, i) => <rect key={i} x={i + 0.15} width={0.7} y={50 - v * 48} height={Math.max(1.5, v * 96)} fill={i / peaks.length < progress ? p.accent : p.line} />)}
                            </svg>
                        ) : (
                            <div aria-hidden="true" style={{ position: "relative", width: "100%", height: 2, background: p.line, borderRadius: 2, overflow: "hidden" }}>
                                <div style={{ position: "absolute", inset: 0, background: p.accent, transformOrigin: "0 50%", transform: `scaleX(${progress})` }} />
                            </div>
                        )}
                    </div>
                </div>
            </div>
            <div ref={group} role="radiogroup" aria-label="Version" onKeyDown={onVariantKey}
                style={{ display: stack ? "flex" : "inline-flex", flexDirection: stack ? "column" : "row", gap: 2, marginTop: 12, padding: 3, borderRadius: stack ? 14 : 999, background: p.chip }}>
                {versions.map((v, i) => (
                    <button key={v.label + i} type="button" role="radio" className="ac-v" aria-checked={i === variant} tabIndex={i === variant ? 0 : -1} disabled={pending} onClick={() => choose(i)}
                        style={{ minHeight: 36, padding: "0 14px", border: 0, borderRadius: stack ? 11 : 999, cursor: pending ? "not-allowed" : "pointer", textAlign: stack ? "left" : "center",
                            ...label, color: i === variant ? p.text : p.text2, background: i === variant ? p.surface : "transparent",
                            boxShadow: i === variant ? "0 1px 2px rgba(36,39,35,0.12)" : "none", transition: `background-color ${motion.d2}ms ${motion.easeInOut}, color ${motion.d2}ms ${motion.easeInOut}` }}>
                        {v.label}
                    </button>
                ))}
            </div>
            {pending && <p id={`${uid}-pending`} style={{ margin: "10px 0 0", fontSize: 13, lineHeight: 1.45, color: p.text2, maxWidth: "44ch" }}>{pendingText || "Recordings in preparation — this player activates when the approved demo is rendered."}</p>}
            {error && (
                <p role="alert" style={{ margin: "10px 0 0", fontSize: 13, color: p.text }}>
                    {error} <button type="button" onClick={() => { buffers.current = null; setStatus("idle") }} style={{ background: "none", border: 0, padding: 0, color: "inherit", textDecoration: "underline", cursor: "pointer", font: "inherit" }}>Try again</button>
                </p>
            )}
        </section>
    )
}

const parsePeaks = (s?: string) => {
    try { const a = JSON.parse(s || "[]"); return Array.isArray(a) ? a.map(Number).filter((n) => isFinite(n)) : [] } catch { return [] }
}

/**
 * @framerSupportedLayoutWidth any-prefer-fixed
 * @framerSupportedLayoutHeight auto
 * @framerIntrinsicWidth 400
 */
export default function AudioComparison(props: any) {
    const { title = "One recording, three ways", source = "Source recording / 01", theme = "light", layout = "compact", pendingText = "", peaks = "",
        label1 = "Original", label2 = "OSP", label3 = "Reimagined", file1, file2, file3, fallback1, fallback2, fallback3 } = props
    const versions: Version[] = [
        { label: label1, sources: [file1, fallback1].filter(Boolean) },
        { label: label2, sources: [file2, fallback2].filter(Boolean) },
        { label: label3, sources: [file3, fallback3].filter(Boolean) },
    ].filter((v) => v.label)
    return <ComparisonPlayer title={title} source={source} versions={versions} peaks={parsePeaks(peaks)} theme={theme} layout={layout} pendingText={pendingText} />
}


const audioTypes = ["mp3", "m4a", "aac", "ogg", "opus", "webm", "wav", "flac"]
addPropertyControls(AudioComparison, {
    title: { type: ControlType.String, title: "Title", defaultValue: "One recording, three ways" },
    source: { type: ControlType.String, title: "Source label", defaultValue: "Source recording / 01" },
    theme: { type: ControlType.Enum, title: "Theme", options: ["light", "dark"], optionTitles: ["Light", "Dark"], displaySegmentedControl: true, defaultValue: "light" },
    layout: { type: ControlType.Enum, title: "Layout", options: ["compact", "stack"], optionTitles: ["Row", "Stack"], displaySegmentedControl: true, defaultValue: "compact" },
    label1: { type: ControlType.String, title: "Version 1", defaultValue: "Original" },
    file1: { type: ControlType.File, title: "Version 1 file", allowedFileTypes: audioTypes },
    fallback1: { type: ControlType.File, title: "V1 fallback", allowedFileTypes: audioTypes },
    label2: { type: ControlType.String, title: "Version 2", defaultValue: "OSP" },
    file2: { type: ControlType.File, title: "Version 2 file", allowedFileTypes: audioTypes },
    fallback2: { type: ControlType.File, title: "V2 fallback", allowedFileTypes: audioTypes },
    label3: { type: ControlType.String, title: "Version 3", defaultValue: "Reimagined" },
    file3: { type: ControlType.File, title: "Version 3 file", allowedFileTypes: audioTypes },
    fallback3: { type: ControlType.File, title: "V3 fallback", allowedFileTypes: audioTypes },
    peaks: { type: ControlType.String, title: "Peaks (JSON)", displayTextArea: true, description: "Paste the output of tools/peaks.mjs. Leave empty for a plain line." },
    pendingText: { type: ControlType.String, title: "Pending text", description: "Shown while no files are set." },
})
