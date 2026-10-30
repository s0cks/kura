// @ts-check
import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";
import fs from "node:fs";
import mermaid from "astro-mermaid";
import starlightImageZoom from "starlight-image-zoom";

import react from "@astrojs/react";

import tailwindcss from "@tailwindcss/vite";

const kura = JSON.parse(fs.readFileSync("./src/kura.tmLanguage.json", "utf-8"));

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
          href: "https://github.com/withastro/starlight",
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
          items: [{ autogenerate: { directory: "reference" } }],
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
