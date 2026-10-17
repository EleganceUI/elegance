import { defineCollection } from 'astro:content';
import { docsLoader } from '@astrojs/starlight/loaders';
import { docsSchema } from '@astrojs/starlight/schema';
import { z } from 'astro/zod';

export const collections = {
	docs: defineCollection({
		loader: docsLoader(),
		schema: docsSchema({
			extend: z.object({
				// Spec pages (how/...): the specs this one builds on, by id without the "how/" prefix,
				// e.g. "framework/reactive". Checked at build time (src/graph.ts).
				depends: z.array(z.string()).optional(),
			}),
		}),
	}),
};
