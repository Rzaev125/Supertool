# Presentation support

Inputs: PPTX, ODP and legacy PowerPoint 97–2003 PPT. Outputs: PDF, PPTX, ODP,
or a folder containing PNG/JPG for every slide. Hidden slides are included in
PDF/image exports; notes pages are excluded. PNG/JPG files use the existing
`page-0001` naming and the PDF DPI setting (144 by default), and JPEG respects
the quality preset. PDF/images are static slide renderings; transitions,
animations, audio/video playback and speaker notes are not represented there.

The existing LibreOffice Impress module performs conversion in a fresh private
profile with macros disabled and untrusted referer links blocked. Update-check
jobs are disabled in this profile. No user Office profile is read or changed.
Imports are forced to the corresponding Impress filter, so a DOCX renamed as
PPTX is not silently converted as a text document. PNG/JPG exports first produce
an internal PDF and use the existing bounded PDFium page renderer; PDFium is
therefore additionally required for image exports. No new binaries are needed.

PPTX/ODP packages are checked before invoking LibreOffice: bounded central
directory, matching local entries, no encryption/multipart/ZIP64, distinct
entry names, required presentation parts and stored ODP presentation mimetype.
Files and total expanded ZIP entries are limited to 1 GiB; the central directory
is limited to 16 MiB. VBA project ZIP entries and ODF Basic/Scripts entries are rejected. Legacy
PPT must be an OLE compound file with a `PowerPoint Document` stream; visible
VBA storages are rejected and execution remains disabled in the private profile.
Corrupt, encrypted or unsupported presentations fail without publishing output.

LibreOffice has a 120-second limit for each presentation conversion and inherits
the worker Job Object. The private staging folder is removed on completion or
cancellation. Originals and existing results are never overwritten. Font
substitution, complex diagrams, embedded media and some PowerPoint features can
render differently from Microsoft Office; editable PPTX/ODP conversion is not
a guarantee of feature-perfect fidelity. Use a copy and inspect important decks.

The filter names are documented by [LibreOffice](https://help.libreoffice.org/latest/en-US/text/shared/guide/convertfilters.html):
`impress_pdf_Export`, `Impress MS PowerPoint 2007 XML`, `impress8` and
`MS PowerPoint 97`. Headless conversion uses a hidden, read-only, silent view
([upstream dispatch code](https://raw.githubusercontent.com/LibreOffice/core/master/desktop/source/app/dispatchwatcher.cxx)).

`tests/presentation_tests.py` creates real PPTX fixtures using python-pptx,
checks two slides including a hidden slide through PDF/PNG/JPG, verifies order,
text, pixels and dimensions, exports PPTX/ODP, round-trips ODP, imports a generated
legacy PPT, and checks malformed/disguised/macro-containing input, missing
modules, collisions and source preservation. Python is only a test dependency.
