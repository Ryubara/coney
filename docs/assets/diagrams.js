// Draws the site's Mermaid diagrams with the ELK layout: right-angle lines routed around the boxes, groups kept
// together. Material's own Mermaid has no ELK, so ```mermaid blocks come out as <div class="coney-diagram"> (see
// mkdocs.yml) and are drawn here instead, in the scheme's colours, again whenever the scheme changes.
import mermaid from "https://cdn.jsdelivr.net/npm/mermaid@12.0.0/dist/mermaid.esm.min.mjs";
import elkLayouts from "https://cdn.jsdelivr.net/npm/@mermaid-js/layout-elk@1.0.0/dist/mermaid-layout-elk.esm.min.mjs";

mermaid.registerLayoutLoaders(elkLayouts);

const COLOURS = {
  default: {
    background: "#ffffff",
    primaryColor: "#ffffff",
    primaryBorderColor: "#c8202c",
    primaryTextColor: "#1d1a1b",
    lineColor: "#6b6466",
    clusterBkg: "#f6f3f3",
    clusterBorder: "#cfc7c8",
    titleColor: "#1d1a1b",
    edgeLabelBackground: "#ffffff",
  },
  slate: {
    background: "#1e2129",
    primaryColor: "#2b2526",
    primaryBorderColor: "#c8202c",
    primaryTextColor: "#ececec",
    lineColor: "#a9a3a4",
    clusterBkg: "#25262d",
    clusterBorder: "#4a4a55",
    titleColor: "#d8d8d8",
    edgeLabelBackground: "#1e2129",
  },
};

function scheme() {
  return document.body.getAttribute("data-md-color-scheme") === "slate" ? "slate" : "default";
}

// The diagram's text as written: the browser has parsed its <br/> line breaks into elements, so read the HTML and
// decode it, keeping those tags as text for Mermaid.
function source(el) {
  const decoder = document.createElement("textarea");
  decoder.innerHTML = el.innerHTML;
  return decoder.value;
}

let drawn = 0;

async function drawAll() {
  const colours = COLOURS[scheme()];
  mermaid.initialize({
    startOnLoad: false,
    securityLevel: "strict",
    theme: "base",
    layout: "elk",
    look: "classic",
    themeVariables: { ...colours, fontFamily: "var(--md-text-font-family, sans-serif)", fontSize: "14px" },
    flowchart: { curve: "linear", padding: 14, nodeSpacing: 30, rankSpacing: 40, htmlLabels: true },
    elk: { mergeEdges: false, nodePlacementStrategy: "NETWORK_SIMPLEX" },
  });
  for (const el of document.querySelectorAll("div.coney-diagram")) {
    el.dataset.source ??= source(el);
    const { svg } = await mermaid.render(`coney-diagram-${drawn++}`, el.dataset.source);
    el.innerHTML = svg;
  }
}

drawAll();
new MutationObserver(drawAll).observe(document.body, { attributes: true, attributeFilter: ["data-md-color-scheme"] });
