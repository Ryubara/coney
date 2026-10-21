/*
 * The progress picture's tooltips (docs/progress/index.md, docs/stylesheets/progress-grid.css).
 *
 * The page already holds the picture as plain SVG with one tooltip per block, and every number in a table. This script
 * adds what a per-square element would cost too much for: it finds the square under the pointer from the data
 * attributes of the block it is over (origin, columns, count), loads progress/functions.json on the first hover
 * (names, addresses, sizes, states, the original's file and the Coney line carrying the @orig tag), and shows them in
 * one tooltip. A click or a tap pins the tooltip so its link can be followed; Escape or a click elsewhere lets go. It
 * does nothing without the table, so a failed download leaves the block tooltips. No library.
 */
(function () {
  "use strict";

  var SVG_NS = "http://www.w3.org/2000/svg";
  var STATES = {
    a: ["Audited", "cg-s-audited"],
    r: ["Reimplemented", "cg-s-reimplemented"],
    u: ["Understood", "cg-s-understood"],
    p: ["Researched", "cg-s-researched"],
    t: ["Untouched", "cg-s-untouched"],
  };

  function hex8(value) {
    var text = value.toString(16);
    return "0x" + "00000000".slice(text.length) + text;
  }

  // Turns the table's per-block arrays into absolute addresses and, for the picture by size, each function's first square.
  function prepare(data) {
    var unit = data.unit;
    data.blocks.forEach(function (block) {
      var address = 0;
      var first = 0;
      block.addresses = [];
      block.first = [];
      for (var i = 0; i < block.a.length; i++) {
        address += block.a[i];
        block.addresses.push(address);
        block.first.push(first);
        first += Math.max(1, Math.ceil(block.z[i] / unit));
      }
    });
    return data;
  }

  // The function at square `index` of `block`: the index itself, or by size the last function that starts at or before it.
  function functionAt(block, index, bySize) {
    if (!bySize) {
      return index < block.a.length ? index : -1;
    }
    var low = 0;
    var high = block.first.length - 1;
    while (low < high) {
      var middle = (low + high + 1) >> 1;
      if (block.first[middle] <= index) {
        low = middle;
      } else {
        high = middle - 1;
      }
    }
    return low;
  }

  // Appends a div with `text` to `parent`.
  function line(parent, className, text) {
    var element = document.createElement("div");
    if (className) {
      element.className = className;
    }
    element.textContent = text;
    parent.appendChild(element);
    return element;
  }

  // Starts the tooltips of one picture block of the page.
  function start(root) {
    var tip = root.querySelector(".cg-tip");
    var url = root.getAttribute("data-functions");
    var repo = root.getAttribute("data-repo") || "";
    var data = null;
    var loading = null;
    var pinned = false;
    var marker = null;
    var last = null;

    // The browser's own tooltip would show beside ours: keep the block text for the label and the gaps.
    var blocksBySvg = new Map();
    root.querySelectorAll("svg.cg").forEach(function (svg) {
      var list = [];
      svg.querySelectorAll(".cg-b").forEach(function (group) {
        var title = group.querySelector("title");
        var text = title ? title.textContent : "";
        if (title) {
          group.removeChild(title);
        }
        list.push({
          index: Number(group.getAttribute("data-b")),
          x: Number(group.getAttribute("data-x")),
          y: Number(group.getAttribute("data-y")),
          cols: Number(group.getAttribute("data-c")),
          count: Number(group.getAttribute("data-n")),
          title: text,
        });
      });
      blocksBySvg.set(svg, list);
    });

    // Fetches the per-function table once; a failure leaves `data` empty and the block text is shown instead.
    function load() {
      if (data) {
        return Promise.resolve(data);
      }
      if (!loading) {
        loading = fetch(url)
          .then(function (response) {
            return response.json();
          })
          .then(function (json) {
            data = prepare(json);
            return data;
          })
          .catch(function () {
            return null;
          });
      }
      return loading;
    }

    function hide() {
      pinned = false;
      tip.hidden = true;
      tip.removeAttribute("data-pinned");
      if (marker && marker.parentNode) {
        marker.parentNode.removeChild(marker);
      }
    }

    // The block and square of `svg` under a client position ({block, cell, pitch}; cell -1 on the label), or null.
    function locate(svg, clientX, clientY) {
      var box = svg.getBoundingClientRect();
      var view = svg.viewBox.baseVal;
      var pitch = Number(svg.getAttribute("data-pitch"));
      var x = ((clientX - box.left) * view.width) / box.width;
      var y = ((clientY - box.top) * view.height) / box.height;
      var list = blocksBySvg.get(svg);
      for (var i = 0; i < list.length; i++) {
        var block = list[i];
        var rows = Math.ceil(block.count / block.cols);
        var inside = x >= block.x && x < block.x + block.cols * pitch;
        if (inside && y >= block.y && y < block.y + rows * pitch) {
          var cell = Math.floor((y - block.y) / pitch) * block.cols + Math.floor((x - block.x) / pitch);
          if (cell < block.count) {
            return { block: block, cell: cell, pitch: pitch };
          }
        } else if (inside && y >= block.y - 17 && y < block.y) {
          return { block: block, cell: -1, pitch: pitch };
        }
      }
      return null;
    }

    // Outlines the square under the pointer.
    function mark(svg, hit) {
      if (!marker) {
        marker = document.createElementNS(SVG_NS, "rect");
        marker.setAttribute("class", "cg-hover");
      }
      var column = hit.cell % hit.block.cols;
      var row = Math.floor(hit.cell / hit.block.cols);
      marker.setAttribute("x", hit.block.x + column * hit.pitch - 0.5);
      marker.setAttribute("y", hit.block.y + row * hit.pitch - 0.5);
      marker.setAttribute("width", hit.pitch);
      marker.setAttribute("height", hit.pitch);
      svg.appendChild(marker);
    }

    // Writes the function's details (or, on a label or before the table has loaded, the block's text) into the tooltip.
    function fill(svg, hit) {
      tip.textContent = "";
      var info = data ? data.blocks[hit.block.index] : null;
      var bySize = svg.getAttribute("data-view") === "size";
      var i = info && hit.cell >= 0 ? functionAt(info, hit.cell, bySize) : -1;
      if (!info || i < 0) {
        line(tip, "cg-tip-title", hit.block.title);
        return;
      }
      line(tip, "cg-tip-name", info.n[i] || "FUN_" + hex8(info.addresses[i]).slice(2));
      line(tip, "", hex8(info.addresses[i]) + " · " + info.z[i].toLocaleString() + " bytes");
      var state = STATES[info.s.charAt(i)] || ["", ""];
      var row = line(tip, "", "");
      var key = document.createElement("span");
      key.className = "cg-key " + state[1];
      row.appendChild(key);
      row.appendChild(document.createTextNode(state[0]));
      var file = info.f[i] >= 0 ? data.files[info.f[i]] : "";
      if (file) {
        line(tip, "", "Original file (source map): " + file);
      }
      var coney = info.c[String(i)];
      if (coney !== undefined) {
        var where = data.coney[coney];
        var holder = line(tip, "", "Coney: ");
        if (repo) {
          var link = document.createElement("a");
          link.href = repo + "/blob/main/" + where.split(":")[0] + "#L" + where.split(":")[1];
          link.textContent = where;
          holder.appendChild(link);
        } else {
          holder.appendChild(document.createTextNode(where));
        }
      }
    }

    // Puts the tooltip beside the pointer, kept inside the picture's column.
    function place(clientX, clientY) {
      var box = root.getBoundingClientRect();
      tip.hidden = false;
      tip.style.left = Math.max(0, Math.min(clientX - box.left + 14, box.width - tip.offsetWidth)) + "px";
      tip.style.top = clientY - box.top + 18 + "px";
    }

    // Shows (and with `pin` keeps) the tooltip for the position, or hides it when nothing is there.
    function show(svg, clientX, clientY, pin) {
      var hit = locate(svg, clientX, clientY);
      if (!hit) {
        if (pin || !pinned) {
          hide();
        }
        return;
      }
      if (!data) {
        load().then(function () {
          if (data && last && !tip.hidden) {
            show(svg, last.x, last.y, pinned);
          }
        });
      }
      fill(svg, hit);
      if (hit.cell >= 0) {
        mark(svg, hit);
      } else if (marker && marker.parentNode) {
        marker.parentNode.removeChild(marker);
      }
      place(clientX, clientY);
      if (pin) {
        pinned = true;
        tip.setAttribute("data-pinned", "");
      }
    }

    root.querySelectorAll("svg.cg").forEach(function (svg) {
      svg.addEventListener("pointerenter", load);
      svg.addEventListener("pointermove", function (event) {
        last = { x: event.clientX, y: event.clientY };
        if (!pinned && event.pointerType !== "touch") {
          show(svg, event.clientX, event.clientY, false);
        }
      });
      svg.addEventListener("pointerleave", function () {
        if (!pinned) {
          hide();
        }
      });
      svg.addEventListener("click", function (event) {
        event.stopPropagation();
        last = { x: event.clientX, y: event.clientY };
        show(svg, event.clientX, event.clientY, true);
      });
    });
    document.addEventListener("click", function (event) {
      if (pinned && !tip.contains(event.target)) {
        hide();
      }
    });
    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape") {
        hide();
      }
    });
  }

  document.querySelectorAll(".progress-functions[data-functions]").forEach(start);
})();
