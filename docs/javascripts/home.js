/*
 * The home page's scroll effect (overrides/home.html, docs/stylesheets/home.css).
 *
 * Writes how far the hero has scrolled, 0 at the top to 1 once it is past, into --coney-p on the root element; the
 * stylesheet turns that number into the drifting art, the growing blur, the fading text and the header's colour.
 * Nothing else happens here: no library, one passive scroll listener, one write per frame. The script does not
 * check prefers-reduced-motion because the stylesheet does: it switches off everything that moves and keeps what
 * only changes a colour.
 */
(function () {
  "use strict";

  var root = document.documentElement;
  var stop = null;

  // Starts the effect for the hero on the page now showing, and stops the one for the page before it.
  function start() {
    if (stop) {
      stop();
      stop = null;
    }
    var hero = document.querySelector("[data-coney-hero]");
    if (!hero) {
      return;
    }

    var queued = false;

    function write() {
      queued = false;
      var progress = window.scrollY / (hero.offsetHeight || 1);
      root.style.setProperty("--coney-p", Math.min(Math.max(progress, 0), 1).toFixed(3));
    }

    function queue() {
      if (!queued) {
        queued = true;
        window.requestAnimationFrame(write);
      }
    }

    window.addEventListener("scroll", queue, { passive: true });
    window.addEventListener("resize", queue, { passive: true });
    write();

    stop = function () {
      window.removeEventListener("scroll", queue);
      window.removeEventListener("resize", queue);
      root.style.removeProperty("--coney-p");
    };
  }

  // Material's document$ fires on the first load and again after each instant navigation, when that is on.
  if (window.document$ && typeof window.document$.subscribe === "function") {
    window.document$.subscribe(start);
  } else if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", start);
  } else {
    start();
  }
})();
