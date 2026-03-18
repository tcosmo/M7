import { NextResponse } from "next/server";
import fs from "fs";
import path from "path";

const worldsDir = path.join(process.cwd(), "content", "worlds");
const contentDir = path.join(process.cwd(), "content");

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string; sectionId: string }> }
) {
  const { id, sectionId } = await params;
  const worldFile = path.join(worldsDir, `${id}.json`);

  if (!fs.existsSync(worldFile)) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }

  const world = JSON.parse(fs.readFileSync(worldFile, "utf-8"));
  const section = world.sections.find((s: { id: string }) => s.id === sectionId);

  if (!section?.score) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }

  const scorePath = path.resolve(path.dirname(worldFile), section.score);

  if (!scorePath.startsWith(contentDir) || !fs.existsSync(scorePath)) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }

  const data = fs.readFileSync(scorePath, "utf-8");
  return new NextResponse(data, {
    headers: { "Content-Type": "application/vnd.recordare.musicxml+xml" },
  });
}
