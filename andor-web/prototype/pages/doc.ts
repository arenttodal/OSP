import { Marked } from 'marked';

/** Support and legal pages: Markdown in content/pages/, the single source for the prototype and
 *  for Framer (FRAMER_BUILD_GUIDE §13). `[[text]]` marks something the owner must fill in. */
export interface DocPage {
  slug: string;
  title: string;
  kicker: string;
  summary: string;
  draft?: string;
  html: string;
  toc: { id: string; label: string }[];
  placeholders: number;
}

export const GROUPS = [
  { label: 'Support', pages: ['installation', 'documentation', 'contact'] },
  { label: 'Legal', pages: ['privacy', 'terms', 'licence'] },
] as const;

const RAW = import.meta.glob('../../content/pages/*.md', { query: '?raw', import: 'default', eager: true }) as Record<string, string>;

export const slugify = (s: string) => s.toLowerCase().replace(/[*`_]/g, '').replace(/&/g, 'and').replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');

const marked = new Marked({ gfm: true });
marked.use({
  renderer: {
    heading({ tokens, depth, text }) {
      return `<h${depth} id="${slugify(text)}">${this.parser.parseInline(tokens)}</h${depth}>\n`;
    },
  },
});

function frontmatter(src: string) {
  const m = /^---\n([\s\S]*?)\n---\n/.exec(src);
  const meta: Record<string, string> = {};
  if (!m) return { meta, body: src };
  for (const line of m[1].split('\n')) {
    const i = line.indexOf(':');
    if (i > 0) meta[line.slice(0, i).trim()] = line.slice(i + 1).trim();
  }
  return { meta, body: src.slice(m[0].length) };
}

/** Site-relative links in the Markdown (`/contact#x`) become prototype files (`contact.html#x`). */
const localLinks = (html: string) => html.replace(/href="\/([a-z-]+)(#[^"]*)?"/g, (_, page, hash = '') => `href="${page}.html${hash}"`);

/** Wide tables scroll inside their own focusable region instead of widening the page. */
const wrapTables = (html: string) => html.replace(/<table>/g, '<div class="doc-table" role="region" aria-label="Table" tabindex="0"><table>').replace(/<\/table>/g, '</table></div>');

export function loadPage(slug: string): DocPage {
  const src = RAW[`../../content/pages/${slug}.md`];
  if (!src) throw new Error(`No page content/pages/${slug}.md`);
  const { meta, body } = frontmatter(src);
  const toc = marked.lexer(body)
    .filter((t): t is Extract<typeof t, { type: 'heading' }> => t.type === 'heading' && (t as { depth: number }).depth === 2)
    .map((t) => ({ id: slugify(t.text), label: t.text.replace(/[*`]/g, '') }));
  const holes = new Set<string>();
  let html = marked.parse(body) as string;
  html = html.replace(/\[\[([\s\S]+?)\]\]/g, (_, x: string) => {
    const label = x.replace(/\s+/g, ' ').trim();
    holes.add(label);
    return `<mark class="ph">${label}</mark>`;
  });
  return {
    slug, title: meta.title ?? slug, kicker: meta.kicker ?? '', summary: meta.summary ?? '', draft: meta.draft,
    html: wrapTables(localLinks(html)), toc, placeholders: holes.size,
  };
}
