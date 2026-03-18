import { NextResponse } from "next/server";
import fs from "fs";
import path from "path";

const worldsDir = path.join(process.cwd(), "content", "worlds");
const contentDir = path.join(process.cwd(), "content");

function resolveRef(refPath: string, baseDir: string) {
  const resolved = path.resolve(baseDir, refPath);
  if (!resolved.startsWith(contentDir)) return null;
  if (!fs.existsSync(resolved)) return null;
  return resolved;
}

function loadJson(filePath: string) {
  return JSON.parse(fs.readFileSync(filePath, "utf-8"));
}

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> }
) {
  const { id } = await params;
  const worldFile = path.join(worldsDir, `${id}.json`);

  if (!fs.existsSync(worldFile)) {
    return NextResponse.json({ error: "World not found" }, { status: 404 });
  }

  const world = loadJson(worldFile);
  const worldBaseDir = path.dirname(worldFile);

  // Resolve cover to an API-servable path
  if (world.cover) {
    const coverPath = resolveRef(world.cover, worldBaseDir);
    if (coverPath) {
      world.cover = `/api/worlds/${id}/cover`;
    }
  }

  // Inline section data
  for (const section of world.sections) {
    const sectionBaseDir = worldBaseDir;

    if (section.score) {
      const scorePath = resolveRef(section.score, sectionBaseDir);
      if (scorePath) {
        section.score = `/api/worlds/${id}/sections/${section.id}/score`;
      }
    }

    if (section.beats) {
      const beatsPath = resolveRef(section.beats, sectionBaseDir);
      if (beatsPath) {
        section.beats = loadJson(beatsPath);
      }
    }

    if (section.sources) {
      const sourcesPath = resolveRef(section.sources, sectionBaseDir);
      if (sourcesPath) {
        const sources = loadJson(sourcesPath);
        // Resolve per-source beat files
        const sourcesBaseDir = path.dirname(sourcesPath);
        for (const ytSource of sources.youtube || []) {
          if (ytSource.beats) {
            const beatPath = resolveRef(ytSource.beats, sourcesBaseDir);
            if (beatPath) {
              ytSource.beats = loadJson(beatPath);
            }
          }
        }
        section.sources = sources;
      }
    }
  }

  return NextResponse.json(world);
}
