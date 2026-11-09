// One shared AudioContext and one active player across the whole site. When a player starts,
// every other registered player is told to pause. Nothing is created before a user gesture (no
// autoplay) and nothing is decoded on page load. Imported by AudioComparison and SoundDeck, so
// exclusivity holds across every audio component on the page.
// andor-web framer bundle v1.0.0

type Pauser = () => void

class AudioControllerImpl {
    private ctx: AudioContext | null = null
    private players = new Map<string, Pauser>()
    private active: string | null = null

    async context(): Promise<AudioContext> {
        if (!this.ctx) {
            const Ctor = (window as any).AudioContext ?? (window as any).webkitAudioContext
            if (!Ctor) throw new Error("Web Audio is not supported in this browser.")
            this.ctx = new Ctor({ latencyHint: "interactive" })
        }
        if (this.ctx!.state === "suspended") await this.ctx!.resume()
        return this.ctx!
    }

    register(id: string, pause: Pauser) {
        this.players.set(id, pause)
        return () => {
            this.players.delete(id)
            if (this.active === id) this.active = null
        }
    }

    claim(id: string) {
        if (this.active && this.active !== id) this.players.get(this.active)?.()
        this.active = id
    }

    release(id: string) {
        if (this.active === id) this.active = null
    }
}

// A single instance per page, even if the module is evaluated twice by the host.
const KEY = "__andorAudioController"
export const AudioController: AudioControllerImpl =
    typeof window !== "undefined" ? ((window as any)[KEY] ??= new AudioControllerImpl()) : new AudioControllerImpl()
