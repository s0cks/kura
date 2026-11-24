// @ts-check
import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";
import fs from "node:fs";
import mermaid from "astro-mermaid";
import starlightImageZoom from "starlight-image-zoom";

import react from "@astrojs/react";

import tailwindcss from "@tailwindcss/vite";

const kura = JSON.parse(fs.readFileSync("./src/kura.tmLanguage.json", "utf-8"));

function nodeSlug(name, label = undefined) {
  return {
    label: label === undefined ? `#${name}` : label,
    slug: `reference/nodes/${name}`,
  };
}

// https://astro.build/config
export default defineConfig({
  site: "https://s0cks.github.io",
  base: "/kura",
  integrations: [
    mermaid({
      theme: "default",
      autoTheme: true,
    }),
    starlight({
      title: "Kura",
      customCss: ["./src/styles/global.css"],
      expressiveCode: {
        shiki: {
          langs: [kura],
        },
      },
      plugins: [starlightImageZoom()],
      social: [
        {
          icon: "github",
          label: "GitHub",
          href: "https://github.com/s0cks/kura",
        },
      ],
      sidebar: [
        {
          label: "Getting Started",
          items: [
            { label: "Introduction", slug: "introduction" },
            { label: "Getting Started", slug: "getting-started" },
            { label: "Status", slug: "status" },
          ],
        },
        {
          label: "Guides",
          items: [
            { label: "The MVU Model", slug: "guides/mvu" },
            { label: "UI Elements", slug: "guides/ui-elements" },
            { label: "Integration", slug: "integration" },
          ],
        },
        {
          label: "Reference",
          items: [
            { label: "Syntax", slug: "reference/syntax" },
            {
              label: "Primitives",
              collapsed: true,
              items: [
                { label: "Align", slug: "reference/types/align" },
                { label: "Color", slug: "reference/types/color" },
                { label: "Direction", slug: "reference/types/direction" },
                { label: "Justify", slug: "reference/types/justify" },
                { label: "Px", slug: "reference/types/px" },
                { label: "Vec", slug: "reference/types/vec" },
                { label: "Rect", slug: "reference/types/rect" },
              ],
            },
            { label: "Types", slug: "reference/types" },
            { label: "Control Flow", slug: "reference/control-flow" },
            { label: "Pipelines", slug: "reference/pipelines" },
            { label: "Modules", slug: "reference/modules" },
            { label: "Directives", slug: "reference/directives" },
            { label: "Tests", slug: "reference/tests" },
            { label: "C-Interop", slug: "reference/c-interop" },
            {
              label: "UI Primitives",
              collapsed: true,
              items: [
                nodeSlug("box", "Box (Base)"),
                nodeSlug("container", "Container"),
                nodeSlug("fragment"),
                nodeSlug("block"),
                nodeSlug("text"),
                nodeSlug("image"),
                nodeSlug("list"),
                nodeSlug("button"),
                nodeSlug("input"),
                nodeSlug("scroll"),
              ],
            },
          ],
        },
        {
          label: "Developers",
          items: [{ autogenerate: { directory: "developers" } }],
        },
      ],
    }),
    react(),
  ],
  vite: {
    plugins: [tailwindcss()],
  },
});
