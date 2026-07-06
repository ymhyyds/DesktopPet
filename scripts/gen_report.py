# -*- coding: utf-8 -*-
"""Generate drag analysis report as Word document on desktop."""
from docx import Document
from docx.shared import Pt, Inches, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn
import os

doc = Document()

# styles
style = doc.styles['Normal']
font = style.font
font.name = 'Arial'
font.size = Pt(10)

def heading(text, level=1):
    h = doc.add_heading(text, level=level)
    return h

def code_block(text):
    p = doc.add_paragraph()
    run = p.add_run(text)
    run.font.name = 'Consolas'
    run.font.size = Pt(9)
    return p

doc.add_paragraph()
title = doc.add_heading('DesktopPet model drag failure & teleport - Read-only Audit Report', level=0)
title.alignment = WD_ALIGN_PARAGRAPH.CENTER

meta = doc.add_paragraph()
meta.alignment = WD_ALIGN_PARAGRAPH.CENTER
meta.add_run('Date: 2026-07-06').font.color.rgb = RGBColor(0x66, 0x66, 0x66)
meta.add_run('\nBranch: fix/model-drag-window-bounds, based on commit 77c4c66')

doc.add_page_break()

# ============================================
heading('1. Current Drag Call Chain', 1)

code_block("""\
Mouse press
  |
  v (Z-order top)
InputProxyWindow::eventFilter()
  +- isInteractivePoint(proxyLocalPoint)
       +- m_preciseRegion.contains(pt)
       +- hitTestAt(petPx, petPy, outIsHead)
       +- alphaTestAt(petPx, petPy)
  +- fail -> cleanupDrag() + return true
  +- pass -> continue dispatch
              |
              v
InputProxyWindow::mousePressEvent()              // InputProxyWindow.cpp:244
  +- isInteractivePoint() guard
  +- hitTestAt + alphaTestAt -> record head/body
  +- m_dragStartOffset = event->pos()
  +- m_pressStartPos = event->pos()
              |
              v
InputProxyWindow::mouseMoveEvent()               // InputProxyWindow.cpp:274
  +- m_dragging == false
  |   +- manhattanLength > kDragThreshold(6px)
  |        +- m_dragging = true, grabMouse()
  |        +- handleDragMove(event)
  +- m_dragging == true
       +- handleDragMove(event)
              |
              v
InputProxyWindow::handleDragMove()               // InputProxyWindow.cpp:385
  +- newGlobal = event->globalPosition().toPoint() - m_dragStartOffset
  +- m_petWindow->move(newGlobal)
  +- setGeometry(m_petWindow->geometry())
              |
              v (triggers PetWindow::moveEvent -> updateSpeechBubblePosition)
              v (eventFilter catches PetWindow::QEvent::Move -> syncGeometry)
              |
              v
InputProxyWindow::syncGeometry()                 // InputProxyWindow.cpp:123
  +- if PetWindow exceeds availableGeometry -> clip proxy window
  +- set m_geoOffsetX/Y != 0
  +- setGeometry(clippedRect)
              |
              v
InputProxyWindow::mouseReleaseEvent()            // InputProxyWindow.cpp:303
  +- cleanupDrag() -> releaseMouse()
  +- emit dragFinished()
              |
              v
App::dragFinished lambda                         // App.cpp:377
  +- m_windowManager->trySnapToEdge(m_petWindow.get())
  +- m_inputProxyWindow->syncGeometry()""")

# ============================================
heading('2. Relationship: PetWindow / InputProxyWindow / viewport', 1)

code_block("""\
Screen Z-order (top to bottom):
+------------------------------------------+
|  InputProxyWindow (QWidget)              |  <- mouse input receiver
|  +- setMask(m_preciseRegion)             |  <- model drawable area only
|  +- m_geoOffsetX/Y offset when clipped   |  <- *** KEY ISSUE ***
+------------------------------------------+
|  PetWindow (QWidget)                     |  <- OpenGL rendering
|  +- nativeEventFilter -> HTTRANSPARENT   |  <- full window mouse-through
|  +- Qt::WindowTransparentForInput        |
|  +- Live2DView (QOpenGLWidget)           |  <- child, fills PetWindow
|       +- glViewport(0,0, pxW*DPR, pxH*DPR)
|       +- projection: SetWidth(2)->Scale->ScaleRelative(comp)
+------------------------------------------+
|  Desktop                                 |
+------------------------------------------+

Geometry relationship:
  PetWindow.geometry() == m_live2DView.geometry() == rect()
  InputProxyWindow.geometry() = PetWindow.geometry() clipped to availableGeometry
  When clipped -> m_geoOffsetX/Y = (clipped.x - petGeo.x, clipped.y - petGeo.y)""")

# ============================================
heading('3. Drag Anchor Formula', 1)

heading('3.1 Press anchor record', 2)
doc.add_paragraph('mousePressEvent (InputProxyWindow.cpp:270):')
code_block('m_dragStartOffset = event->pos()    // proxy window local coordinates (logical px)')

heading('3.2 Drag position calculation', 2)
doc.add_paragraph('handleDragMove (InputProxyWindow.cpp:385):')
code_block("""\
newGlobal = event->globalPosition().toPoint() - m_dragStartOffset
// = global mouse pos - offset relative to proxy window top-left at press
// = expected proxy window top-left in global coordinates""")

heading('3.3 Derivation', 2)
doc.add_paragraph('On press:')
code_block("""\
globalPressPos = proxyOrigin + m_dragStartOffset
// proxyOrigin = InputProxyWindow screen position""")
doc.add_paragraph('During drag:')
code_block("""\
newGlobal = globalCurrentPos - m_dragStartOffset
          = globalCurrentPos - (globalPressPos - proxyOrigin)
          = proxyOrigin + (globalCurrentPos - globalPressPos)""")

heading('3.4 Deviation when m_geoOffset != 0', 2)
doc.add_paragraph('When proxy window is NOT clipped (m_geoOffsetX/Y = 0):')
code_block("""\
proxyOrigin == petWindowOrigin  ->  CORRECT""")
doc.add_paragraph('When proxy window IS clipped (m_geoOffsetX/Y != 0):')
code_block("""\
proxyOrigin = petWindowOrigin + (m_geoOffsetX, m_geoOffsetY)
newGlobal = petWindowOrigin + (m_geoOffsetX, m_geoOffsetY) + mouseDelta
PetWindow.move(newGlobal)
         = correct_position + (m_geoOffsetX, m_geoOffsetY)     // WRONG!""")

p = doc.add_paragraph()
run = p.add_run('Conclusion: Each mouseMove accumulates m_geoOffset drift, causing continuous teleport.')
run.bold = True
run.font.color.rgb = RGBColor(0xCC, 0x00, 0x00)

# ============================================
heading('4. Screen Boundary Limiting', 1)

heading('4.1 WindowManager::trySnapToEdge() - called after drag ends', 2)
code_block("""\
screenRect = screen->availableGeometry()
pos +/- width/height within 24px of edge -> snap to edge""")

heading('4.2 InputProxyWindow::syncGeometry() - called on every moveEvent', 2)
code_block("""\
screen = screenAt(petGeo.center())
available = screen->availableGeometry()
intersected = petGeo.intersected(available)
if (intersected != petGeo):
    m_geoOffsetX = intersected.x() - petGeo.x()
    m_geoOffsetY = intersected.y() - petGeo.y()
    proxyGeo = intersected  // proxy window shrinks""")

heading('4.3 Key Conflict', 2)
p = doc.add_paragraph()
run = p.add_run(
    'trySnapToEdge only runs AFTER drag ends (snap only, no position clamp). '
    'But syncGeometry runs DURING drag via moveEvent -> clips proxy window in real-time -> sets m_geoOffset. '
    'After clipping, proxy window may be smaller than visible model area -> next press misses.')
run.font.color.rgb = RGBColor(0xCC, 0x00, 0x00)

# ============================================
heading('5. Top 3 Root Causes', 1)

heading('ROOT CAUSE #1 (PRIMARY): m_geoOffset != 0 causes drag anchor drift -> "teleport"', 2)
doc.add_paragraph(
    'When PetWindow nears screen edge (taskbar, multi-monitor boundary), syncGeometry() clips '
    'InputProxyWindow to availableGeometry. m_geoOffsetX/Y records the delta between PetWindow '
    'origin and clipped proxy origin.')
doc.add_paragraph(
    'But m_dragStartOffset is relative to the CLIPPED proxy origin, while m_petWindow->move() '
    'moves PetWindow. The formula newGlobal = globalPos - m_dragStartOffset assumes '
    'proxyOrigin == petWindowOrigin. When m_geoOffsetX/Y != 0, each handleDragMove shifts '
    'PetWindow by (m_geoOffsetX, m_geoOffsetY).')
p = doc.add_paragraph()
run = p.add_run('Matches symptom: ')
run.bold = True
p.add_run(
    '"model teleports slightly below original position when starting drag near edge" - '
    'exactly PetWindow bottom edge clipped by taskbar, causing Y offset.')

heading('ROOT CAUSE #2 (SEVERE): syncGeometry clips proxy DURING drag -> "drag dead"', 2)
doc.add_paragraph(
    'Each handleDragMove -> m_petWindow->move() triggers QEvent::Move. '
    'eventFilter catches this -> calls syncGeometry(). '
    'If near edge: proxy window shrinks -> m_preciseRegion also recomputed in updateInputMask(). '
    'Clipped proxy no longer covers mouse cursor area.')
doc.add_paragraph(
    'Next mouse press: cursor is over PetWindow area but NOT over proxy window area -> '
    'PetWindow has HTTRANSPARENT -> event falls through to desktop.')
p = doc.add_paragraph()
run = p.add_run('Matches symptom: ')
run.bold = True
p.add_run('"After dragging model near edge, subsequent drag attempts produce no response".')

heading('ROOT CAUSE #3: Model tight bounds variance -> "abnormally tall window"', 2)
doc.add_paragraph(
    'applyTightSize() uses model tight bounds aspect ratio to compute window height. '
    'Model A (seated) may be moderate height; Model B (standing/long hair/ribbons) '
    'may have extremely large tight bounds. Tall windows more easily trigger screen boundary '
    'clipping -> m_geoOffset activates -> triggers RC#1/RC#2.')
doc.add_paragraph(
    'After model switch, tight bounds update is delayed (stableBoundsReady at 500ms), '
    'during which window size may be in a transitional state.')
p = doc.add_paragraph()
run = p.add_run('Matches symptom: ')
run.bold = True
p.add_run('"Inconsistent behavior across different models", "some models have abnormally tall transparent window".')

# ============================================
heading('6. Hypothesis Exclusion Summary', 1)

table = doc.add_table(rows=7, cols=3)
table.style = 'Light Grid Accent 1'
hdr = table.rows[0].cells
hdr[0].text = 'Hypothesis'
hdr[1].text = 'Verdict'
hdr[2].text = 'Reasoning'

data = [
    ('frameGeometry vs geometry mixing',
     'EXCLUDED',
     'PetWindow is FramelessWindowHint, frameGeo == geo; code consistently uses geometry()'),
    ('Qt / Win32 coordinate mixing (HiDPI)',
     'EXCLUDED',
     'All Qt APIs use logical pixels; devicePixelRatio only used in GL viewport, not drag code'),
    ('Window resize during drag rewrites dragOffset',
     'MINOR',
     'Model switch/scale could resize between drags but does not change recorded m_dragStartOffset'),
    ('Model switch does not sync',
     'EXCLUDED',
     'modelSizeLoaded / stableBoundsReady / modelScaleChanged all connect sync+mask update'),
    ('InputProxyWindow Z-order below PetWindow',
     'EXCLUDED',
     'InputProxyWindow shown after PetWindow, Z-order is above'),
    ('isInteractivePoint over-filtering',
     'MINOR',
     'm_preciseRegion based on per-Drawable AABB; during animation may be slightly smaller than visual area'),
]

for i, (hyp, verdict, note) in enumerate(data):
    row = table.rows[i+1].cells
    row[0].text = hyp
    row[1].text = verdict
    row[2].text = note

# ============================================
heading('7. Recommended Debug Diagnostics', 1)

debug_table = doc.add_table(rows=6, cols=3)
debug_table.style = 'Light Grid Accent 1'
dhdr = debug_table.rows[0].cells
dhdr[0].text = 'Location'
dhdr[1].text = 'Condition'
dhdr[2].text = 'Information'

debug_data = [
    ('InputProxyWindow::syncGeometry()',
     'qWarning when m_geoOffset != 0',
     'PetWindow geo, proxy geo before/after, offsetX/Y, availableGeometry'),
    ('InputProxyWindow::mousePressEvent()',
     'qInfo on each press',
     'petWindow geo, proxy geo, event->pos(), m_geoOffset, isInteractivePoint result'),
    ('InputProxyWindow::handleDragMove()',
     'qInfo on each move (while dragging)',
     'newGlobal, PetWindow pos before/after, m_geoOffset, dragStartOffset'),
    ('InputProxyWindow::isInteractivePoint()',
     'qWarning when preciseRegion does NOT contain point',
     'point, m_preciseRegion.boundingRect(), m_geoOffset, petPx/petPy'),
    ('PetWindow::applyTightSize()',
     'qInfo on each execution',
     'modelW/modelH, newW/newH, tightWidth/Height, aspect'),
]

for i, (loc, cond, info) in enumerate(debug_data):
    row = debug_table.rows[i+1].cells
    row[0].text = loc
    row[1].text = cond
    row[2].text = info

# ============================================
heading('8. Files to Modify', 1)

file_table = doc.add_table(rows=5, cols=2)
file_table.style = 'Light Grid Accent 1'
fhdr = file_table.rows[0].cells
fhdr[0].text = 'File'
fhdr[1].text = 'Changes'

file_data = [
    ('interaction/InputProxyWindow.cpp',
     '1) Fix handleDragMove coordinate calc when m_geoOffset != 0 (compensate offset)\n'
     '2) Suppress syncGeometry clipping during drag (use no-clip position sync)\n'
     '3) Add diagnostic logging'),
    ('interaction/InputProxyWindow.h',
     'Add m_dragging flag to suppress geometry clipping during drag'),
    ('pet/PetWindow.cpp',
     'Add diagnostic logging in applyTightSize()'),
    ('core/App.cpp',
     'Add diagnostic logging in dragFinished connection'),
]

for i, (file, changes) in enumerate(file_data):
    row = file_table.rows[i+1].cells
    row[0].text = file
    row[1].text = changes

doc.add_paragraph()
heading('Core Fix Direction', 2)
p = doc.add_paragraph()
p.add_run('Direction A: ').bold = True
p.add_run('In handleDragMove, add m_geoOffset compensation to m_dragStartOffset to correct PetWindow vs proxy origin mismatch.')
doc.add_paragraph()
p = doc.add_paragraph()
p.add_run('Direction B: ').bold = True
p.add_run('During drag, use move() to directly align both window topLefts; do NOT use clipping syncGeometry(). After drag ends, run clipped syncGeometry + snap-to-edge.')
doc.add_paragraph()
p = doc.add_paragraph()
p.add_run('Recommended (A+B combo): ').bold = True
p.add_run('Disable clipping in syncGeometry during drag (only align position), AND compensate existing m_geoOffset in the anchor formula.')

# Save
desktop = os.path.join(os.path.expanduser('~'), 'Desktop')
out_path = os.path.join(desktop, 'DesktopPet-Drag-Audit-Report.docx')
doc.save(out_path)
print(f'Report saved to: {out_path}')
