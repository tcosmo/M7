#!/usr/bin/env python3
"""
Remove "grey octave" ghost notes from P17 (Organo e Continuo) in Magnificat.musicxml.

These ghost notes are in voice 2 with <notehead>none</notehead>, along with
associated <backup>, <forward>, and hidden rest elements that form the voice 2 layer.

Strategy: In each measure of P17, identify the voice 2 block that starts after a <backup>
element and remove everything from that <backup> to the end of the voice 2 content
(including any <forward> elements that pad the remaining duration).
"""

import xml.etree.ElementTree as ET
import sys
import os

INPUT_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'resources', 'Magnificat', 'Magnificat.musicxml')
OUTPUT_FILE = INPUT_FILE  # overwrite in place

# Preserve the original header (xml declaration + DOCTYPE) that ElementTree would lose
ORIGINAL_HEADER = '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">\n'


def is_voice2_note(elem):
    """Check if a note element belongs to voice 2."""
    if elem.tag != 'note':
        return False
    voice = elem.find('voice')
    return voice is not None and voice.text == '2'


def is_ghost_backup(children, backup_idx):
    """
    Check if a backup element at the given index is followed by voice 2 ghost notes.
    A "ghost backup" is one where the next note element after it is a voice 2 note.
    """
    for j in range(backup_idx + 1, len(children)):
        child = children[j]
        if child.tag == 'note':
            voice = child.find('voice')
            if voice is not None and voice.text == '2':
                return True
            else:
                return False
        elif child.tag in ('forward', 'backup'):
            return False
    return False


def clean_measure(measure):
    """
    Remove the voice 2 ghost layer from a single measure.

    Removes:
    - <backup> elements that precede voice 2 content
    - All <note> elements with <voice>2</voice> (both notehead=none notes and hidden rests)
    - <forward> elements that are part of the voice 2 layer (i.e., appear after
      the backup and interspersed with or following voice 2 notes)
    """
    children = list(measure)
    to_remove = []
    in_voice2_block = False

    for i, child in enumerate(children):
        if child.tag == 'backup' and not in_voice2_block:
            if is_ghost_backup(children, i):
                in_voice2_block = True
                to_remove.append(child)
                continue

        if in_voice2_block:
            if child.tag == 'note':
                if is_voice2_note(child):
                    to_remove.append(child)
                else:
                    in_voice2_block = False
            elif child.tag == 'forward':
                to_remove.append(child)
            elif child.tag == 'backup':
                if is_ghost_backup(children, i):
                    to_remove.append(child)
                else:
                    in_voice2_block = False
            else:
                in_voice2_block = False

    for elem in to_remove:
        measure.remove(elem)

    return len(to_remove)


def main():
    # Read the original file to capture the header
    with open(INPUT_FILE, 'r', encoding='UTF-8') as f:
        original_first_line = f.readline()

    tree = ET.parse(INPUT_FILE)
    root = tree.getroot()

    # Find P17
    p17 = None
    for part in root.iter('part'):
        if part.get('id') == 'P17':
            p17 = part
            break

    if p17 is None:
        print("ERROR: Could not find part P17")
        sys.exit(1)

    # Count before
    notes_before = len(list(p17.iter('note')))
    backups_before = len(list(p17.iter('backup')))
    forwards_before = len(list(p17.iter('forward')))

    # Process each measure
    total_removed = 0
    measures_modified = 0
    for measure in p17.iter('measure'):
        removed = clean_measure(measure)
        if removed > 0:
            total_removed += removed
            measures_modified += 1

    # Count after
    notes_after = len(list(p17.iter('note')))
    backups_after = len(list(p17.iter('backup')))
    forwards_after = len(list(p17.iter('forward')))

    print(f"P17 Organo e Continuo - Ghost note removal:")
    print(f"  Measures modified: {measures_modified}")
    print(f"  Total elements removed: {total_removed}")
    print(f"  Notes: {notes_before} -> {notes_after} (removed {notes_before - notes_after})")
    print(f"  Backups: {backups_before} -> {backups_after} (removed {backups_before - backups_after})")
    print(f"  Forwards: {forwards_before} -> {forwards_after} (removed {forwards_before - forwards_after})")

    # Verify: no voice 2 notes should remain
    remaining_v2 = 0
    for note in p17.iter('note'):
        voice = note.find('voice')
        if voice is not None and voice.text == '2':
            remaining_v2 += 1
    print(f"  Remaining voice 2 notes: {remaining_v2}")

    if remaining_v2 > 0:
        print("  WARNING: Some voice 2 notes were not removed!")

    # Write to a temporary string via ElementTree, then fix the header
    # Do NOT call ET.indent() - the file is already properly indented and we want
    # to preserve the original formatting exactly.

    # Write without xml_declaration so we can provide our own header
    import io
    buf = io.BytesIO()
    tree.write(buf, encoding='UTF-8', xml_declaration=False)
    xml_bytes = buf.getvalue()
    xml_str = xml_bytes.decode('UTF-8')

    # Write output with proper header
    with open(OUTPUT_FILE, 'w', encoding='UTF-8') as f:
        f.write(ORIGINAL_HEADER)
        f.write(xml_str)
        # Ensure trailing newline
        if not xml_str.endswith('\n'):
            f.write('\n')

    print(f"\nFile written to: {OUTPUT_FILE}")


if __name__ == '__main__':
    main()
