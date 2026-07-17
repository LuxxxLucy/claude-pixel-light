// The closed lid seen from the back, with the 4-LED lightbar strip near the top.
// Rendered to assets/lightbar.svg by build.sh, included in main.typ via image().
// Standalone so it works on any typst version (html target lacks html.frame).
#set page(width: auto, height: auto, margin: 4pt)
#import "@preview/cetz:0.4.2"

#let dr = cetz.draw
#let sk  = (paint: rgb("#555555"), thickness: 0.9pt)
#let sk2 = (paint: rgb("#555555"), thickness: 0.6pt)

#cetz.canvas(length: 1cm, {
  // the lid (back of the closed laptop), landscape ~3:2
  dr.rect((0, 0), (8.4, 5.6), radius: 0.35, stroke: sk)

  // the lightbar: a thin strip of 4 LEDs, centred near the top edge
  let cy = 5.05
  let xs = (3.45, 3.95, 4.45, 4.95)
  // the strip housing over the LEDs
  dr.rect((3.15, cy - 0.2), (5.25, cy + 0.2), radius: 0.06, stroke: sk2)
  for (n, x) in xs.enumerate() {
    dr.rect((x - 0.18, cy - 0.11), (x + 0.18, cy + 0.11), radius: 0.03, stroke: sk2)
    dr.content((x, cy - 0.45), text(size: 5.5pt, fill: rgb("#555555"))[#n])
  }
})
