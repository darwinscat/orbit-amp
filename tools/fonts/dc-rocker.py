# Makes resources/fonts/DCRocker-Regular.ttf: New Rocker (Pablo Impallari, Brenda Gallo, Rodrigo Fuenzalida; SIL OFL 1.1)
# cut to a European Latin subset and renamed "DC Rocker" - the OFL reserves the name "New Rocker" for the font as its
# authors made it, so a modified copy carries another name. Only the names change; the outlines are the authors'.
#
#   python3 -m venv build/fontvenv && build/fontvenv/bin/pip install fonttools
#   build/fontvenv/bin/python tools/fonts/dc-rocker.py <NewRocker-Regular.ttf> resources/fonts/DCRocker-Regular.ttf
import sys
from fontTools import subset
from fontTools.ttLib import TTFont

source, target = sys.argv[1], sys.argv[2]

# Basic Latin, Latin-1 Supplement and Latin Extended-A, and the punctuation a face writes beyond them: dashes, quotes,
# the bullet, the ellipsis, the single guillemets and the euro.
ranges = [(0x20, 0x7E), (0xA0, 0xFF), (0x100, 0x17F)]
extra = [0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026, 0x2039, 0x203A, 0x20AC]
unicodes = [u for lo, hi in ranges for u in range(lo, hi + 1)] + extra

options = subset.Options()
options.layout_features = ["kern", "liga", "mark", "mkmk", "ccmp", "locl"]
options.name_IDs = ["*"]
options.name_languages = ["*"]
options.notdef_outline = True
options.hinting = False             # JUCE draws the outlines unhinted on every system: the instructions are dead weight
options.drop_tables += ["DSIG"]

font = TTFont(source)
sub = subset.Subsetter(options)
sub.populate(unicodes=unicodes)
sub.subset(font)

family, full, postscript = "DC Rocker", "DC Rocker Regular", "DCRocker-Regular"
for record in font["name"].names:
    text = None
    if record.nameID in (1, 16):
        text = family
    elif record.nameID in (2, 17):
        text = "Regular"
    elif record.nameID == 3:
        text = "DC Rocker Regular; subset of New Rocker Regular"
    elif record.nameID == 4:
        text = full
    elif record.nameID == 6:
        text = postscript
    elif record.nameID == 0:
        text = str(record) + " Subset and renamed DC Rocker (only the names changed)."
    if text is not None:
        record.string = text
if "CFF " in font:
    font["CFF "].cff.fontNames = [postscript]
font.save(target)
print(target, sum(1 for _ in font.getGlyphOrder()), "glyphs")
