#!/usr/bin/env python3
"""Strip identifying metadata from a MusicXML file."""

import sys
import xml.etree.ElementTree as ET

def strip(filepath: str, outpath: str | None = None):
    tree = ET.parse(filepath)
    root = tree.getroot()

    for ident in root.iter("identification"):
        # Remove <source>
        for source in ident.findall("source"):
            ident.remove(source)

        # Remove <miscellaneous> block
        for misc in ident.findall("miscellaneous"):
            ident.remove(misc)

        # Strip <encoding> children except <supports>
        for encoding in ident.findall("encoding"):
            for child in list(encoding):
                if child.tag != "supports":
                    encoding.remove(child)

    out = outpath or filepath
    tree.write(out, xml_declaration=True, encoding="UTF-8")
    print(f"Stripped → {out}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <file.musicxml> [output.musicxml]")
        sys.exit(1)
    strip(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
