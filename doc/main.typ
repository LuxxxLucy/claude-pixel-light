#import "/vendor/dual-typst/src/lib.typ": (
  tufte, sidenote, marginnote, sidecite,
  main-figure, margin-figure, full-width-figure,
  epigraph, new-thought, full-width, sans, diagram,
)

#let _is-html = sys.inputs.at("target", default: "pdf") == "html"
#let _seo-title = "Driving the Chromebook Pixel lightbar from Claude Code"
#let _seo-desc = "A daemon that maps Claude Code hook events to the Chromebook Pixel lid lightbar, over the ChromeOS EC sysfs interface."
#let _seo-url = "https://luxxxlucy.github.io/claude-pixel-light/"
#let _seo-date = "2026-07-17"
#let _seo-head() = {
  html.elem("meta", attrs: (("name"): "description", ("content"): _seo-desc))[]
  html.elem("meta", attrs: (("name"): "author", ("content"): "Jialin Lu"))[]
  html.elem("link", attrs: (("rel"): "icon", ("type"): "image/svg+xml", ("href"): "assets/favicon.svg"))[]
  html.elem("link", attrs: (("rel"): "canonical", ("href"): _seo-url))[]
  html.elem("meta", attrs: (("property"): "og:type", ("content"): "article"))[]
  html.elem("meta", attrs: (("property"): "og:title", ("content"): _seo-title))[]
  html.elem("meta", attrs: (("property"): "og:description", ("content"): _seo-desc))[]
  html.elem("meta", attrs: (("property"): "og:url", ("content"): _seo-url))[]
  html.elem("meta", attrs: (("property"): "article:published_time", ("content"): _seo-date))[]
  html.elem("meta", attrs: (("name"): "twitter:card", ("content"): "summary_large_image"))[]
  html.elem("meta", attrs: (("name"): "twitter:title", ("content"): _seo-title))[]
  html.elem("meta", attrs: (("name"): "twitter:description", ("content"): _seo-desc))[]
  // Diagram image (direct figure child) is centred and keeps its aspect.
  html.elem("style")[#"article figure > img { display: block; margin-inline: auto; max-width: 100%; height: auto; }"]
}

#show: tufte.with(
  title: [Programming the 2013 Chromebook Pixel lid lightbar ],
  author: "Jialin Lu",
  date: "2026-07-17",
  abstract: [
  ],
  toc: false,
  style: "envision",
  head-extra: if _is-html { _seo-head() } else { none },
)

#marginnote[Code: \
  #link("https://github.com/LuxxxLucy/pixel-lights")[github.com/LuxxxLucy/pixel-lights]
  #link("https://github.com/LuxxxLucy/claude-pixel-light")[github.com/LuxxxLucy/claude-pixel-light]
]

This is rather a small thing that should not have something written for it, but then this indeed brings me some joy so I
just want to share here.

Essentially I got a Google's 2013 Chrome Pixel laptop from ebay, which is in a pretty decent shape, although it is quite old and the
spec is really barely working#sidenote[regarding this, see my complaint at the end], but coding some C within neovim.

// Laptop figure, HTML only: official press photo left, mine right, no caption.
#if _is-html {
  main-figure(
    html.elem("div", attrs: (("style"): "display:flex; flex-wrap:wrap; gap:1rem; align-items:flex-end; justify-content:center;"))[
      #html.elem("img", attrs: (("src"): "assets/pixel-2013.jpg", ("style"): "height:14rem; width:auto; max-width:100%;"))[]
      #html.elem("img", attrs: (("src"): "assets/pixel-mine.jpg", ("style"): "height:14rem; width:auto; max-width:100%;"))[]
    ],
  )
}

Left: the 2013 Google Chromebook Pixel. Right: What I got.

Even it is from 2013, it has a surprisingly modern-ish edgy look, and what is more is that it also has a programmable lightbar in the lid,
and it is really, really, fancy.

// Demo GIFs (external <img>, so the animation is preserved), HTML only.
#let _demo(src) = html.elem("img", attrs: (
  ("src"): src, ("alt"): "lightbar demo",
  ("style"): "display:block; width:100%; height:auto; margin:0.5rem 0;",
))[]
#if _is-html [
  #_demo("assets/demo-1.gif")
  #_demo("assets/demo-2.gif")
]

The 2013 Chromebook pixel has a lid lightbar, a strip of 4 RGB LEDS on the back of the lid,
Each LED has 4 channel RGB 0-255 and an additional brightness scaler.

#main-figure(
  image("assets/lightbar.svg"),
  caption: [The lightbar is a strip of 4 RGB LEDs on the back of the lid, indices 0 to 3 left to right.],
)

I vibe-coded a #link("https://github.com/LuxxxLucy/pixel-lights")[very basic lib] that controls it from the ChromeOS EC controller and runs a daemon, and then #link("https://github.com/LuxxxLucy/claude-pixel-light")[configure claude code hooks]
to update the lid light, which pipes messages to the daemon to update.

Oh my, this brings me too much joy. Can I call myself practising physical AI now?

Though the machine is indeed too old, 4GB RAM with mobile level CPU (by 2013 standard), to be smooth I even start tweaking `vm.swappiness` for the limited memory.

#figure(
  image("assets/ram-usage.jpg"),
  caption: [Some Claude Code processes already eat about 1.1 GB, starting a browser would be a real tough job],
)

No dear Claude, you are not the culprit, and I am a clown.
