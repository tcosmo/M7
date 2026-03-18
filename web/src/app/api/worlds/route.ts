import { NextResponse } from "next/server";
import fs from "fs";
import path from "path";

const worldsDir = path.join(process.cwd(), "content", "worlds");

export async function GET() {
  const files = fs.readdirSync(worldsDir).filter((f) => f.endsWith(".json"));
  const worlds = files.map((f) => {
    const data = JSON.parse(fs.readFileSync(path.join(worldsDir, f), "utf-8"));
    return {
      id: data.id,
      order: data.order,
      title: data.title,
      composer: data.composer,
      catalogue: data.catalogue,
      sections: data.sections.map((s: { id: string; title: string; levels: unknown[] }) => ({
        id: s.id,
        title: s.title,
        levelCount: s.levels.length,
      })),
    };
  });

  worlds.sort((a, b) => a.order - b.order);
  return NextResponse.json(worlds);
}
