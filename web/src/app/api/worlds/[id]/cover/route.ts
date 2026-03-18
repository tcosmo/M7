import { NextResponse } from "next/server";
import fs from "fs";
import path from "path";

const worldsDir = path.join(process.cwd(), "content", "worlds");
const contentDir = path.join(process.cwd(), "content");

export async function GET(
  _request: Request,
  { params }: { params: Promise<{ id: string }> }
) {
  const { id } = await params;
  const worldFile = path.join(worldsDir, `${id}.json`);

  if (!fs.existsSync(worldFile)) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }

  const world = JSON.parse(fs.readFileSync(worldFile, "utf-8"));
  const coverPath = path.resolve(path.dirname(worldFile), world.cover);

  if (!coverPath.startsWith(contentDir) || !fs.existsSync(coverPath)) {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }

  const data = fs.readFileSync(coverPath);
  return new NextResponse(data, {
    headers: { "Content-Type": "image/jpeg" },
  });
}
