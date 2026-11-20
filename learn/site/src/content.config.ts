// The content model (docs/learn-studio/content-and-scenario-model.md): guides and reference
// articles in MDX with validated front matter. A build fails on a missing or malformed field.
import { defineCollection } from 'astro:content';
import { glob } from 'astro/loaders';
import { z } from 'astro/zod';

const verified = z.object({ commit: z.string().min(7), date: z.string(), notes: z.string().optional() });

const guides = defineCollection({
  loader: glob({ pattern: '**/*.mdx', base: './src/content/guides' }),
  schema: z.object({
    schemaVersion: z.literal(1),
    kind: z.literal('andor.learn.guide'),
    id: z.string().regex(/^osp\.guide\.[a-z-]+\.[a-z0-9-]+$/),
    product: z.literal('osp'),
    group: z.enum(['make-an-instrument', 'play-and-layer', 'synth-imprint', 'arpeggiator', 'granular-key-dna', 'shape-and-finish']),
    order: z.number(),
    title: z.string(),
    purpose: z.string(),
    tags: z.array(z.string()),
    synonyms: z.array(z.string()),
    controls: z.array(z.string()).default([]),
    learn: z.object({ lessons: z.array(z.string()).default([]), info: z.array(z.string()).default([]) }).default({ lessons: [], info: [] }),
    reference: z.string().optional(),
    related: z.array(z.string()).default([]),
    preconditions: z.array(z.string()).default([]),
    scene: z.string(),
    quick: z.boolean().default(false),
    verified,
    editorial: z.object({ hostVerification: z.string().optional() }).optional(),
    steps: z.array(z.object({
      id: z.string().regex(/^[a-z0-9-]+$/),
      instruction: z.string(),
      result: z.string(),
      targets: z.array(z.string()).default([]),
    })).min(1),
  }),
});

const reference = defineCollection({
  loader: glob({ pattern: '*.mdx', base: './src/content/reference' }),
  schema: z.object({
    schemaVersion: z.literal(1),
    kind: z.literal('andor.learn.reference'),
    id: z.string().regex(/^osp\.ref\.[a-z0-9-]+$/),
    product: z.literal('osp'),
    title: z.string(),
    function: z.string(),
    location: z.string(),
    parameters: z.array(z.object({ name: z.string(), values: z.string(), does: z.string() })).default([]),
    dependencies: z.array(z.string()).default([]),
    example: z.string().optional(),
    guides: z.array(z.string()).default([]),
    synonyms: z.array(z.string()).default([]),
    parameterIds: z.array(z.string()).default([]),
    verified,
  }),
});

export const collections = { guides, reference };
