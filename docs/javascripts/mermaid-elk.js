/*
 * Mermaid diagrams drawn with the ELK layout engine (docs/guides/writing-docs.md, "Diagrams").
 *
 * Material for MkDocs draws a ```mermaid fence with its own copy of Mermaid, fetched from a CDN, and that copy has
 * no ELK. Material uses a global `mermaid` when one exists, so this script puts one there: a small stand-in that
 * loads a pinned Mermaid and the ELK layouts (@mermaid-js/layout-elk) from jsDelivr the first time a diagram is
 * drawn, registers the layouts, and hands Material's calls on. A diagram asks for ELK in its own front matter
 * (`config: {layout: elk}`). Material's themeCSS keys the colours to the palette, so light and dark both work.
 * The stand-in is installed only on a page that has a diagram, so every other page loads nothing extra.
 *
 * The versions are pinned; to upgrade, pick releases at least two weeks old and look at every diagram again.
 */
(function () {
  "use strict";

  var MERMAID = "https://cdn.jsdelivr.net/npm/mermaid@11.17.2/dist/mermaid.esm.min.mjs";
  var ELK = "https://cdn.jsdelivr.net/npm/@mermaid-js/layout-elk@0.2.3/dist/mermaid-layout-elk.esm.min.mjs";

  // Only a page with a diagram needs Mermaid; this script runs after the page's content, so the fences are there.
  if (!document.querySelector("pre.mermaid")) {
    return;
  }

  var MIN_WIDTH = 640;

  var settings = {};
  var ready = null;

  // Loads Mermaid and the ELK layouts once, registers the layouts and applies the settings Material asked for.
  function load() {
    if (!ready) {
      ready = Promise.all([import(MERMAID), import(ELK)]).then(function (modules) {
        var mermaid = modules[0].default;
        mermaid.registerLayoutLoaders(modules[1].default);
        mermaid.initialize(settings);
        return mermaid;
      });
    }
    return ready;
  }

  // The part of Mermaid's interface that Material calls: `initialize` with its settings, then `render` per diagram.
  window.mermaid = {
    initialize: function (config) {
      settings = config;
    },
    render: function (id, text) {
      return load().then(function (mermaid) {
        return mermaid.render(id, text).then(function (result) {
          // Keep the text readable on a narrow screen: the picture stops shrinking at MIN_WIDTH and the page
          // scrolls it sideways (docs/stylesheets/extra.css) instead.
          result.svg = result.svg.replace(/<svg([^>]*?) style="/, '<svg$1 style="min-width: ' + MIN_WIDTH + "px; ");
          return result;
        });
      });
    },
  };
})();
