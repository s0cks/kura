// @ts-check
import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";
import fs from "node:fs";

import react from "@astrojs/react";

const kura = JSON.parse(fs.readFileSync("./src/kura.tmLanguage.json", "utf-8"));

// https://astro.build/config
export default defineConfig({
  site: "https://s0cks.github.io",
  base: "/kura",
  integrations: [
    starlight({
      title: "Kura",
      expressiveCode: {
        shiki: {
          langs: [kura],
        },
      },
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
            {
              label: "Introduction",
              slug: "introduction",
            },
            {
              label: "Status",
              slug: "status",
            },
          ],
        },
        {
          label: "Reference",
          items: [
            {
              autogenerate: { directory: "reference" },
            },
          ],
        },
        {
          label: "Developers",
          items: [
            {
              label: "Building",
              slug: "building",
            },
          ],
        },
      ],
    }),
    react(),
  ],
});
