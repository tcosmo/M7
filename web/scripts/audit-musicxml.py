#!/usr/bin/env python3
"""Audit a MusicXML file for metadata, signatures, and identifying information."""

import sys
import xml.etree.ElementTree as ET

def audit(filepath: str):
    tree = ET.parse(filepath)
    root = tree.getroot()
    findings = []

    def report(xpath: str, element: ET.Element):
        text = (element.text or "").strip()
        attribs = dict(element.attrib)
        if text or attribs:
            findings.append({
                "path": xpath,
                "tag": element.tag,
                "text": text if text else None,
                "attribs": attribs if attribs else None,
            })

    # <identification> block — the main metadata container
    for ident in root.iter("identification"):
        for child in ident:
            report("identification/" + child.tag, child)
            for grandchild in child:
                report(f"identification/{child.tag}/{grandchild.tag}", grandchild)

    # <credit> elements — often contain engraver/arranger info, footers
    for credit in root.iter("credit"):
        for cw in credit.iter("credit-words"):
            report("credit/credit-words", cw)

    # <work> block
    for work in root.iter("work"):
        for child in work:
            report("work/" + child.tag, child)

    # <movement-title>, <movement-number>
    for tag in ["movement-title", "movement-number"]:
        for el in root.iter(tag):
            report(tag, el)

    # <defaults> — can contain font info that fingerprints the source
    for defaults in root.iter("defaults"):
        for child in defaults:
            report("defaults/" + child.tag, child)
            for grandchild in child:
                report(f"defaults/{child.tag}/{grandchild.tag}", grandchild)

    # <print> first occurrence — page layout can be identifying
    for i, pr in enumerate(root.iter("print")):
        if i > 0:
            break
        for child in pr:
            report("print/" + child.tag, child)
            for grandchild in child:
                report(f"print/{child.tag}/{grandchild.tag}", grandchild)

    # <miscellaneous-field> anywhere
    for mf in root.iter("miscellaneous-field"):
        report("miscellaneous-field", mf)

    # Check for comments in raw XML
    with open(filepath, "r") as f:
        raw = f.read()
    import re
    comments = re.findall(r"<!--(.*?)-->", raw, re.DOTALL)
    for c in comments:
        c = c.strip()
        if c:
            findings.append({"path": "XML_COMMENT", "tag": "comment", "text": c, "attribs": None})

    # Report
    if not findings:
        print("No metadata found.")
        return

    print(f"Found {len(findings)} metadata entries:\n")
    for f in findings:
        path = f["path"]
        text = f["text"] or ""
        attribs = f["attribs"] or {}
        attr_str = f"  attribs={attribs}" if attribs else ""
        print(f"  [{path}] {text}{attr_str}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <file.musicxml>")
        sys.exit(1)
    audit(sys.argv[1])
