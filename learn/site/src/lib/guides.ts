import { getCollection, type CollectionEntry } from 'astro:content';
import { groups } from '../data/groups';
export type Guide = CollectionEntry<'guides'>;
export const slugOf = (g: Guide) => g.data.id.split('.').pop()!;
export const urlOf = (g: Guide, base = import.meta.env.BASE_URL) => `${base}osp/${g.data.group}/${slugOf(g)}/`;
export async function allGuides() {
  const gi = (g: Guide) => groups.findIndex((x) => x.slug === g.data.group);
  return (await getCollection('guides')).sort((a, b) => gi(a) - gi(b) || a.data.order - b.data.order);
}
export async function byId() {
  const m = new Map<string, Guide>();
  for (const g of await allGuides()) m.set(g.data.id, g);
  return m;
}
