// One shared AudioContext and one active player across the whole site: when a player starts,
// every other registered player is told to pause. Nothing is created until the first user gesture
// (no autoplay), and nothing here decodes audio on page load.

type Pauser = () => void;

class GlobalAudioControllerImpl {
  private ctx: AudioContext | null = null;
  private players = new Map<string, Pauser>();
  private active: string | null = null;

  /** Lazily creates (or resumes) the context. Call only from a user gesture. */
  async context(): Promise<AudioContext> {
    if (!this.ctx) {
      const Ctor = window.AudioContext ?? (window as any).webkitAudioContext;
      if (!Ctor) throw new Error('Web Audio is not supported in this browser.');
      this.ctx = new Ctor({ latencyHint: 'interactive' });
    }
    if (this.ctx.state === 'suspended') await this.ctx.resume();
    return this.ctx;
  }

  register(id: string, pause: Pauser) {
    this.players.set(id, pause);
    return () => {
      this.players.delete(id);
      if (this.active === id) this.active = null;
    };
  }

  /** Claims playback for `id`, pausing whoever was playing. */
  claim(id: string) {
    if (this.active && this.active !== id) this.players.get(this.active)?.();
    this.active = id;
  }

  release(id: string) {
    if (this.active === id) this.active = null;
  }

  get activeId() {
    return this.active;
  }
}

export const GlobalAudioController = new GlobalAudioControllerImpl();
